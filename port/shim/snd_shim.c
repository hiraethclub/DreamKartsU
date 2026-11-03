/* DKU shim: KOS's sfxmgr and AICA channel updates on the port's mixer. */
#include <dc/sound/aica_comm.h>
#include <dc/sound/sfxmgr.h>
#include <dc/spu.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "../audio_mix.h"

#define MAX_SFX 256

typedef struct {
    int16_t *pcm;       /* native byte order */
    uint32_t len;       /* samples */
    uint32_t rate;
} Sample;

typedef struct {
    const Sample *s;
    double pos, step;   /* in samples */
    float gl, gr;
    int vol, pan;
    bool loop, playing, reserved;
    uint32_t loopstart, loopend;
} Chan;

static Sample samples[MAX_SFX + 1];   /* handle h is samples[h]; 0 is invalid */
static Chan chans[AUDIO_MIX_CHANNELS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int next_chan;
static audio_music_fn music_fn;
static float music_gain;

/* AICA-like levels: vol 0..255 linear, pan 0..255 constant power. */
static void chan_gains(Chan *c) {
    float v = (float)c->vol / 255.0f;
    float p = (float)c->pan / 255.0f;
    c->gl = v * cosf(p * 1.5707963f) * 1.41421356f * 0.5f;
    c->gr = v * sinf(p * 1.5707963f) * 1.41421356f * 0.5f;
}

static void chan_freq(Chan *c, int freq) {
    c->step = (double)freq / AUDIO_MIX_RATE;
}

void audio_mix_init(void) {}

void audio_mix_set_music(audio_music_fn fn) {
    pthread_mutex_lock(&lock);
    music_fn = fn;
    pthread_mutex_unlock(&lock);
}

void audio_mix_music_gain(float gain) { music_gain = gain; }

void audio_mix(int16_t *out, int n) {
    int32_t acc[2 * 1024];
    pthread_mutex_lock(&lock);
    while(n > 0) {
        int m = n > 1024 ? 1024 : n;
        memset(acc, 0, sizeof(int32_t) * 2 * (size_t)m);
        for(int i = 0; i < AUDIO_MIX_CHANNELS; i++) {
            Chan *c = &chans[i];
            if(!c->playing || !c->s) continue;
            uint32_t end = c->loop && c->loopend > c->loopstart ? c->loopend : c->s->len;
            for(int k = 0; k < m; k++) {
                uint32_t p = (uint32_t)c->pos;
                if(p >= end) {
                    if(!c->loop) {
                        c->playing = false;
                        break;
                    }
                    c->pos -= end - c->loopstart;
                    p = (uint32_t)c->pos;
                }
                float smp = c->s->pcm[p];
                acc[2 * k] += (int32_t)(smp * c->gl);
                acc[2 * k + 1] += (int32_t)(smp * c->gr);
                c->pos += c->step;
            }
        }
        if(music_fn) music_fn(acc, m, music_gain);
        for(int k = 0; k < 2 * m; k++) {
            int32_t v = acc[k];
            out[k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        out += 2 * m;
        n -= m;
    }
    pthread_mutex_unlock(&lock);
}

int snd_init(void) {
    audio_mix_init();
    return 0;
}

void snd_shutdown(void) { snd_sfx_unload_all(); }

size_t snd_mem_available(void) { return 2u << 20; }

/* The game batches updates between stop and start so they land in the same
   AICA tick; the mixer's lock already makes each update atomic. */
void snd_sh4_to_aica_stop(void) {}
void snd_sh4_to_aica_start(void) {}

void snd_sh4_to_aica(void *packet, uint32_t size) {
    (void)size;
    const aica_cmd_t *cmd = packet;
    if(cmd->cmd != AICA_CMD_CHAN || cmd->cmd_id >= AUDIO_MIX_CHANNELS) return;
    const aica_channel_t *ch = (const aica_channel_t *)cmd->cmd_data;
    pthread_mutex_lock(&lock);
    Chan *c = &chans[cmd->cmd_id];
    if((ch->cmd & AICA_CH_CMD_MASK) == AICA_CH_CMD_STOP) c->playing = false;
    else if((ch->cmd & AICA_CH_CMD_MASK) == AICA_CH_CMD_UPDATE) {
        if(ch->cmd & AICA_CH_UPDATE_SET_FREQ) chan_freq(c, (int)ch->freq);
        if(ch->cmd & AICA_CH_UPDATE_SET_VOL) c->vol = (int)ch->vol;
        if(ch->cmd & AICA_CH_UPDATE_SET_PAN) c->pan = (int)ch->pan;
        chan_gains(c);
    }
    pthread_mutex_unlock(&lock);
}

sfxhnd_t snd_sfx_load_raw_buf(char *buf, size_t len, uint32_t rate, uint16_t bitsize, uint16_t channels) {
    if(bitsize != 16 || channels != 1) return SFXHND_INVALID;
    for(int h = 1; h <= MAX_SFX; h++) {
        if(samples[h].pcm) continue;
        uint32_t n = (uint32_t)(len / 2);
        int16_t *pcm = malloc(len ? len : 2);
        if(!pcm) return SFXHND_INVALID;
        /* The pack is little-endian PCM, as the AICA reads it. */
        const uint8_t *b = (const uint8_t *)buf;
        for(uint32_t i = 0; i < n; i++) pcm[i] = (int16_t)(b[2 * i] | b[2 * i + 1] << 8);
        samples[h] = (Sample){pcm, n, rate};
        return (sfxhnd_t)h;
    }
    return SFXHND_INVALID;
}

void snd_sfx_unload(sfxhnd_t idx) {
    if(idx == SFXHND_INVALID || idx > MAX_SFX) return;
    pthread_mutex_lock(&lock);
    for(int i = 0; i < AUDIO_MIX_CHANNELS; i++)
        if(chans[i].s == &samples[idx]) chans[i].playing = false;
    free(samples[idx].pcm);
    samples[idx] = (Sample){0};
    pthread_mutex_unlock(&lock);
}

void snd_sfx_unload_all(void) {
    for(int h = 1; h <= MAX_SFX; h++) snd_sfx_unload((sfxhnd_t)h);
}

/* Round-robin over the channels nobody reserved, as KOS. */
int snd_sfx_play_ex(sfx_play_data_t *d) {
    if(d->idx == SFXHND_INVALID || d->idx > MAX_SFX || !samples[d->idx].pcm) return -1;
    pthread_mutex_lock(&lock);
    int chn = d->chn;
    if(chn < 0) {
        for(int tries = 0; tries < AUDIO_MIX_CHANNELS; tries++) {
            int c = next_chan;
            next_chan = (next_chan + 1) % AUDIO_MIX_CHANNELS;
            if(!chans[c].reserved) {
                chn = c;
                break;
            }
        }
    }
    if(chn < 0 || chn >= AUDIO_MIX_CHANNELS) {
        pthread_mutex_unlock(&lock);
        return -1;
    }
    Chan *c = &chans[chn];
    c->s = &samples[d->idx];
    c->pos = 0;
    chan_freq(c, d->freq ? d->freq : (int)c->s->rate);
    c->vol = d->vol;
    c->pan = d->pan;
    c->loop = d->loop != 0;
    c->loopstart = d->loopstart;
    c->loopend = d->loopend;
    c->playing = true;
    chan_gains(c);
    pthread_mutex_unlock(&lock);
    return chn;
}

int snd_sfx_play_chn(int chn, sfxhnd_t idx, int vol, int pan) {
    sfx_play_data_t d = {.chn = chn, .idx = idx, .vol = vol, .pan = pan};
    return snd_sfx_play_ex(&d);
}

int snd_sfx_play(sfxhnd_t idx, int vol, int pan) { return snd_sfx_play_chn(-1, idx, vol, pan); }

void snd_sfx_stop(int chn) {
    if(chn < 0 || chn >= AUDIO_MIX_CHANNELS) return;
    pthread_mutex_lock(&lock);
    chans[chn].playing = false;
    pthread_mutex_unlock(&lock);
}

/* Reserved channels keep playing, as KOS. */
void snd_sfx_stop_all(void) {
    pthread_mutex_lock(&lock);
    for(int i = 0; i < AUDIO_MIX_CHANNELS; i++)
        if(!chans[i].reserved) chans[i].playing = false;
    pthread_mutex_unlock(&lock);
}

int snd_sfx_chn_alloc(void) {
    pthread_mutex_lock(&lock);
    for(int i = AUDIO_MIX_CHANNELS - 1; i >= 0; i--)
        if(!chans[i].reserved) {
            chans[i].reserved = true;
            pthread_mutex_unlock(&lock);
            return i;
        }
    pthread_mutex_unlock(&lock);
    return -1;
}

void snd_sfx_chn_free(int chn) {
    if(chn < 0 || chn >= AUDIO_MIX_CHANNELS) return;
    pthread_mutex_lock(&lock);
    chans[chn].reserved = false;
    chans[chn].playing = false;
    pthread_mutex_unlock(&lock);
}

/* 4-bit CD level, 3 dB a step: 15 is full, 0 off. */
void spu_cdda_volume(int left_volume, int right_volume) {
    int v = left_volume > right_volume ? left_volume : right_volume;
    audio_mix_music_gain(v <= 0 ? 0.0f : powf(10.0f, -3.0f * (float)(15 - v) / 20.0f));
}
