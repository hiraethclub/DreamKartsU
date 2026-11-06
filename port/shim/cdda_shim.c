/* DKU shim: the CD drive, playing the music files as the disc's audio
   tracks (PORT_PLAN.md M4).

   The disc has one audio track per plat_music_dir()/NN.mp3 (tools/
   build_music.py, disc order from 01), or no disc at all when they are
   missing, which audio.c already handles as silent music. Track t starts
   at FAD t * TRACK_FADS, so the TOC and audio.c's sector-range plays map
   straight back to a file and a length.

   A worker thread decodes the playing song (dr_mp3) into a ring of stereo
   frames at the file's own rate, already at the song's level from
   port/music_levels.h with a limiter holding peaks under
   DKU_MUSIC_PEAK_DB (upstream's ffmpeg alimiter does the same when it
   presses the disc). The mixer pulls from the ring through music_fn,
   resampling linearly to AUDIO_MIX_RATE and scaled by the CD level
   (spu_cdda_volume, snd_shim.c).

   The play position only moves when the mixer pulls, by exactly what it
   pulls: a ring that runs dry plays silence and the position still
   advances, so the song ends (CD_STATUS_STANDBY, how audio.c's jukebox
   notices) after the same number of mixed frames whatever the decoder's
   speed. */
#include <dc/cdrom.h>
#ifdef __WIIU__
#include <coreinit/thread.h>
#include <coreinit/time.h>
#endif
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../audio_mix.h"
#include "../music_levels.h"
#include "../plat.h"
#include "music_tracks.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_SIMD
#include "dr_mp3.h"

_Static_assert(DKU_MUSIC_FILES == DK_MUSIC_TRACKS, "port/music_levels.h is stale: run tools/build_music.py");

#define TRACK_FADS 100000u   /* 22 minutes of sectors per track */
#define RING 32768u          /* frames, power of two: 0.7 s at 44.1 kHz */
#define CHUNK 2048u

static pthread_mutex_t ml = PTHREAD_MUTEX_INITIALIZER;
static pthread_t worker;
static bool worker_on, scanned, worker_stop;
static int ntracks;

/* Under ml. */
static int16_t ring[RING * 2];
static uint64_t written;      /* frames decoded into the ring since the play began */
static double rpos;           /* the mixer's position, in source frames */
static uint64_t end_frame;    /* the play stops here (sector range or end of file) */
static uint32_t src_rate = 44100;
static int want_track, gen;   /* a play order for the worker: track (1-based), its generation */
static uint32_t want_frames;  /* sector-range length in frames; 0: the whole track */
static bool playing, paused, decoded_all;

/* Frames that played as silence because the decoder fell behind (test log). */
volatile uint32_t dku_music_dry;

/* On the Wii U, wut's nanosleep in a second thread crashed Cemu (M4);
   OSSleepTicks is the system call underneath it anyway. */
static void sleep_ms(int ms) {
#ifdef __WIIU__
    OSSleepTicks(OSMillisecondsToTicks(ms));
#else
    struct timespec ts = {0, (long)ms * 1000000L};
    nanosleep(&ts, NULL);
#endif
}

static void music_path(char *out, size_t n, int track) {
    snprintf(out, n, "%s%02d.mp3", plat_music_dir(), track);
}

static void scan(void) {
    if(scanned) return;
    scanned = true;
    for(int t = 1; t <= DK_MUSIC_TRACKS; t++) {
        char p[512];
        music_path(p, sizeof(p), t);
        FILE *f = fopen(p, "rb");
        if(!f) break;
        fclose(f);
        ntracks = t;
    }
    if(ntracks < DK_MUSIC_TRACKS)
        printf("DKU music: %d of %d songs in %s (tools/build_music.py)\n", ntracks, DK_MUSIC_TRACKS,
               plat_music_dir());
}

/* Stereo-linked peak limiter: instant attack, 50 ms release. */
typedef struct {
    float gain, thr, r, rel;
} Limiter;

static void limit(Limiter *l, const float *in, int16_t *out, unsigned frames, unsigned ch) {
    for(unsigned i = 0; i < frames; i++) {
        float a = in[i * ch] * l->gain, b = ch > 1 ? in[i * ch + 1] * l->gain : a;
        float pk = fmaxf(fabsf(a), fabsf(b));
        float target = pk > l->thr ? l->thr / pk : 1.0f;
        l->r = target < l->r ? target : target + (l->r - target) * l->rel;
        a *= l->r * 32767.0f;
        b *= l->r * 32767.0f;
        out[2 * i] = (int16_t)(a > 32767.0f ? 32767.0f : a < -32768.0f ? -32768.0f : a);
        out[2 * i + 1] = (int16_t)(b > 32767.0f ? 32767.0f : b < -32768.0f ? -32768.0f : b);
    }
}

static void *worker_main(void *arg) {
    (void)arg;
#ifdef __WIIU__
    /* Off core 1, where the game runs, and away from the sound mixer on core 0. */
    OSSetThreadAffinity(OSGetCurrentThread(), OS_THREAD_ATTRIB_AFFINITY_CPU2);
#endif
    static drmp3 mp3;
    static float pcm[CHUNK * 2];
    static int16_t out[CHUNK * 2];
    bool open = false;
    int my_gen = -1;
    Limiter lim = {0};
    for(;;) {
        pthread_mutex_lock(&ml);
        if(worker_stop) {
            pthread_mutex_unlock(&ml);
            break;
        }
        int g = gen, track = want_track;
        uint32_t frames = want_frames;
        uint64_t at = (uint64_t)rpos;   /* past `written` after the ring ran dry */
        bool room = written <= at || written - at + CHUNK <= RING;
        bool done = decoded_all;
        pthread_mutex_unlock(&ml);
        if(g != my_gen) {
            my_gen = g;
            if(open) drmp3_uninit(&mp3);
            open = false;
            if(track) {
                char p[512];
                music_path(p, sizeof(p), track);
                open = drmp3_init_file(&mp3, p, NULL);
                if(!open) printf("DKU music: cannot decode %s\n", p);
                lim = (Limiter){powf(10.0f, dku_music_gain_db[track - 1] / 20.0f),
                                powf(10.0f, DKU_MUSIC_PEAK_DB / 20.0f), 1.0f,
                                expf(-1.0f / (0.05f * (float)(open ? mp3.sampleRate : 44100)))};
                pthread_mutex_lock(&ml);
                if(gen == g) {
                    src_rate = open ? mp3.sampleRate : 44100;
                    /* The file's end, found by decoding to it, may come sooner. */
                    end_frame = frames ? (uint64_t)frames * src_rate / 75u : UINT64_MAX;
                    decoded_all = !open;
                    if(!open) playing = false;
                }
                pthread_mutex_unlock(&ml);
            }
            continue;
        }
        if(!open || done || !room) {
            sleep_ms(open && !done ? 2 : 10);
            continue;
        }
        unsigned ch = mp3.channels > 1 ? 2 : 1;
        drmp3_uint64 got = drmp3_read_pcm_frames_f32(&mp3, CHUNK, pcm);
        limit(&lim, pcm, out, (unsigned)got, ch);
        pthread_mutex_lock(&ml);
        if(gen == my_gen) {
            for(drmp3_uint64 i = 0; i < got; i++) {
                uint32_t at = (uint32_t)((written + i) & (RING - 1));
                ring[2 * at] = out[2 * i];
                ring[2 * at + 1] = out[2 * i + 1];
            }
            written += got;
            if(got < CHUNK) end_frame = written < end_frame ? written : end_frame;
            if(written >= end_frame) decoded_all = true;
        }
        pthread_mutex_unlock(&ml);
    }
    if(open) drmp3_uninit(&mp3);
    return NULL;
}

/* The mixer's pull (audio_mix, under its lock). */
static void music_fn(int32_t *acc, int n, float gain) {
    pthread_mutex_lock(&ml);
    if(!playing || paused || (!written && !decoded_all)) {   /* still opening the file */
        pthread_mutex_unlock(&ml);
        return;
    }
    double step = (double)src_rate / AUDIO_MIX_RATE;
    for(int k = 0; k < n; k++) {
        if(rpos >= (double)end_frame) {
            playing = false;
            break;
        }
        uint64_t i = (uint64_t)rpos;
        if(i + 1 < written && i + RING > written) {
            float f = (float)(rpos - (double)i);
            uint32_t a = (uint32_t)(i & (RING - 1)), b = (uint32_t)((i + 1) & (RING - 1));
            float l = ring[2 * a] + (ring[2 * b] - ring[2 * a]) * f;
            float r = ring[2 * a + 1] + (ring[2 * b + 1] - ring[2 * a + 1]) * f;
            acc[2 * k] += (int32_t)(l * gain);
            acc[2 * k + 1] += (int32_t)(r * gain);
        }
        else if(!decoded_all) dku_music_dry++;
        rpos += step;
    }
    pthread_mutex_unlock(&ml);
}

int cdrom_reinit(void) { return ERR_OK; }

int cdrom_get_status(int *status, int *disc_type) {
    scan();
    int s = CD_STATUS_NO_DISC;
    if(ntracks) {
        pthread_mutex_lock(&ml);
        s = !playing ? CD_STATUS_STANDBY : paused ? CD_STATUS_PAUSED : CD_STATUS_PLAYING;
        pthread_mutex_unlock(&ml);
    }
    if(status) *status = s;
    if(disc_type) *disc_type = 0;
    return 0;
}

int cdrom_read_toc(cd_toc_t *toc, bool high_density) {
    (void)high_density;
    scan();
    memset(toc, 0, sizeof(*toc));
    if(!ntracks) return ERR_NO_DISC;
    for(int t = 1; t <= ntracks; t++) toc->entry[t - 1] = (uint32_t)t * TRACK_FADS;   /* control 0: audio */
    toc->first = 1u << 16;
    toc->last = (uint32_t)ntracks << 16;
    toc->leadout_sector = (uint32_t)(ntracks + 1) * TRACK_FADS;
    return ERR_OK;
}

int cdrom_cdda_play(uint32_t start, uint32_t end, uint32_t loops, int mode) {
    (void)loops;   /* audio.c always plays once (repeat 0) */
    scan();
    int track;
    uint32_t frames = 0;
    if(mode == CDDA_SECTORS) {
        track = (int)(start / TRACK_FADS);
        if(start % TRACK_FADS || end <= start) return ERR_SYS;
        frames = end - start;
    }
    else track = (int)start;
    if(track < 1 || track > ntracks) return ntracks ? ERR_SYS : ERR_NO_DISC;
    /* Before ml: the mixer takes its own lock and then ml (music_fn), so
       taking them the other way round here could deadlock. */
    audio_mix_set_music(music_fn);
    pthread_mutex_lock(&ml);
    if(!worker_on) {
        /* minimp3 decodes with a large scratch area on the stack. */
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 128 * 1024);
        worker_on = pthread_create(&worker, &attr, worker_main, NULL) == 0;
        pthread_attr_destroy(&attr);
        /* Joined by cdda_shutdown. */
    }
    if(worker_on) {
        gen++;
        want_track = track;
        want_frames = frames;
        written = 0;
        rpos = 0;
        end_frame = UINT64_MAX;   /* until the worker has opened the file */
        decoded_all = false;
        playing = true;
        paused = false;
    }
    pthread_mutex_unlock(&ml);
    return worker_on ? ERR_OK : ERR_SYS;
}

int cdrom_cdda_pause(void) {
    pthread_mutex_lock(&ml);
    paused = true;
    pthread_mutex_unlock(&ml);
    return ERR_OK;
}

int cdrom_cdda_resume(void) {
    pthread_mutex_lock(&ml);
    bool ok = playing;
    paused = false;
    pthread_mutex_unlock(&ml);
    return ok ? ERR_OK : ERR_SYS;
}

/* Stop the music and its thread, file closed, before the game exits: on the
   Wii U the file lives in the .wuhb, which goes away with the app. */
void cdda_shutdown(void) {
    audio_mix_set_music(NULL);
    pthread_mutex_lock(&ml);
    bool on = worker_on;
    worker_stop = true;
    playing = false;
    pthread_mutex_unlock(&ml);
    if(on) pthread_join(worker, NULL);
    pthread_mutex_lock(&ml);
    worker_on = false;
    worker_stop = false;
    pthread_mutex_unlock(&ml);
}
