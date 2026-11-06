/* DKU: Wii U sound out through AX (sndcore2), fed by the shared mixer.

   No SDL2 for the Wii U on this machine (PORT_PLAN.md M4), so AX directly:
   two LPCM16 voices, left and right, each looping over its own ring of
   RING samples at the 48 kHz renderer rate, mixed to the TV's and the
   GamePad's left and right channels. A thread reads where the left voice
   is playing and keeps the rings LEAD samples ahead of it from
   audio_mix(), flushing what it wrote from the data cache so the DSP
   sees it. The voices start together inside one AXVoiceBegin/End pair, so
   they stay in step.

   Autotest builds log how far AX has played, once a second of output for
   the first few seconds and then every ten ("DKU AX:"), so the Cemu check
   can tell the console's audio side really consumed the mix. */
#include <coreinit/cache.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <malloc.h>
#include <pthread.h>
#include <sndcore2/core.h>
#include <sndcore2/voice.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_mix.h"
#include "perf_wiiu.h"

#define RING 4096u   /* samples per channel: 85 ms */
#define LEAD 2048u   /* kept written ahead of the play position: 43 ms */
#define CHUNK 256u

static AXVoice *voice[2];
static int16_t *ring[2];
static pthread_t thread;
static volatile bool run, want_pause, parked;
static bool started;

volatile uint32_t dku_ax_underruns;

/* n <= CHUNK frames from the mixer into the rings at sample at. */
static void fill(uint32_t at, uint32_t n) {
    static int16_t mix[2 * CHUNK];
    audio_mix(mix, (int)n);
    for(uint32_t i = 0; i < n; i++) {
        ring[0][(at + i) % RING] = mix[2 * i];
        ring[1][(at + i) % RING] = mix[2 * i + 1];
    }
    for(int c = 0; c < 2; c++) {
        if(at + n <= RING) DCFlushRange(ring[c] + at, n * sizeof(int16_t));
        else {
            DCFlushRange(ring[c] + at, (RING - at) * sizeof(int16_t));
            DCFlushRange(ring[c], (at + n - RING) * sizeof(int16_t));
        }
    }
}

static void voices_state(AXVoiceState state) {
    AXVoiceBegin(voice[0]);
    AXVoiceBegin(voice[1]);
    for(int c = 0; c < 2; c++) {
        if(state == AX_VOICE_STATE_PLAYING) AXSetVoiceCurrentOffset(voice[c], 0);
        AXSetVoiceState(voice[c], state);
    }
    AXVoiceEnd(voice[1]);
    AXVoiceEnd(voice[0]);
}

static void *feeder(void *arg) {
    (void)arg;
    /* Off core 1, where the game runs. */
    OSSetThreadAffinity(OSGetCurrentThread(), OS_THREAD_ATTRIB_AFFINITY_CPU0);
    uint32_t wpos = LEAD;
#ifdef DK_AUTOTEST
    uint32_t last = 0;
    uint64_t played = 0, report = AUDIO_MIX_RATE;
#endif
    while(run) {
        /* In the background (the HOME menu): voices stopped, nothing mixed,
           so the music decoder soon idles too and touches no files. */
        if(want_pause) {
            if(!parked) {
                voices_state(AX_VOICE_STATE_STOPPED);
                parked = true;
            }
            OSSleepTicks(OSMillisecondsToTicks(10));
            continue;
        }
        if(parked) {
            for(uint32_t at = 0; at < LEAD; at += CHUNK) fill(at, CHUNK);
            voices_state(AX_VOICE_STATE_PLAYING);
            wpos = LEAD;
#ifdef DK_AUTOTEST
            last = 0;
#endif
            parked = false;
        }
        AXVoiceOffsets off;
        AXGetVoiceOffsets(voice[0], &off);
        uint32_t play = off.currentOffset % RING;
#ifdef DK_AUTOTEST
        played += (play + RING - last) % RING;
        last = play;
#endif
        uint32_t ahead = (wpos + RING - play) % RING;
        if(ahead > LEAD + CHUNK) {   /* overtaken (a stall): start afresh at the play position */
            ahead = 0;
            dku_ax_underruns++;
        }
        if(!ahead) wpos = play;
        while(ahead < LEAD) {
            fill(wpos, CHUNK);
            wpos = (wpos + CHUNK) % RING;
            ahead += CHUNK;
        }
#ifdef DK_AUTOTEST
        if(played >= report) {
            printf("DKU AX: %llu s played\n", (unsigned long long)(played / AUDIO_MIX_RATE));
            report += played < 5ull * AUDIO_MIX_RATE ? AUDIO_MIX_RATE : 10ull * AUDIO_MIX_RATE;
        }
#endif
        OSSleepTicks(OSMillisecondsToTicks(2));
    }
    return NULL;
}

void audio_ax_start(void) {
    if(started) return;
    AXInitParams params = {.renderer = AX_INIT_RENDERER_48KHZ, .pipeline = AX_INIT_PIPELINE_SINGLE};
    if(!AXIsInit()) AXInitWithParams(&params);
    if(!AXIsInit()) {
        printf("DKU AX: init failed; no sound\n");
        return;
    }
    for(int c = 0; c < 2; c++) {
        ring[c] = memalign(64, RING * sizeof(int16_t));
        voice[c] = AXAcquireVoice(31, NULL, NULL);
        if(!ring[c] || !voice[c]) {
            printf("DKU AX: no voice; no sound\n");
            return;
        }
        memset(ring[c], 0, RING * sizeof(int16_t));
        DCFlushRange(ring[c], RING * sizeof(int16_t));
    }
    for(uint32_t at = 0; at < LEAD; at += CHUNK) fill(at, CHUNK);
    AXVoiceBegin(voice[0]);
    AXVoiceBegin(voice[1]);
    for(int c = 0; c < 2; c++) {
        AXSetVoiceType(voice[c], 0);
        AXVoiceVeData ve = {.volume = 0x8000};
        AXSetVoiceVe(voice[c], &ve);
        /* Channel c of the TV (6 channels) and the GamePad (4), main bus. */
        AXVoiceDeviceMixData tv[6], drc[4];
        memset(tv, 0, sizeof(tv));
        memset(drc, 0, sizeof(drc));
        tv[c].bus[0].volume = 0x8000;
        drc[c].bus[0].volume = 0x8000;
        AXSetVoiceDeviceMix(voice[c], AX_DEVICE_TYPE_TV, 0, tv);
        AXSetVoiceDeviceMix(voice[c], AX_DEVICE_TYPE_DRC, 0, drc);
        AXSetVoiceSrcType(voice[c], AX_VOICE_SRC_TYPE_NONE);
        AXSetVoiceSrcRatio(voice[c], 1.0f);
        AXVoiceOffsets off = {
            .dataType = AX_VOICE_FORMAT_LPCM16,
            .loopingEnabled = AX_VOICE_LOOP_ENABLED,
            .loopOffset = 0,
            .endOffset = RING - 1,
            .currentOffset = 0,
            .data = ring[c],
        };
        AXSetVoiceOffsets(voice[c], &off);
        AXSetVoiceState(voice[c], AX_VOICE_STATE_PLAYING);
    }
    AXVoiceEnd(voice[1]);
    AXVoiceEnd(voice[0]);
    run = true;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 * 1024);   /* the mixer and the music resampler run on it */
    int err = pthread_create(&thread, &attr, feeder, NULL);
    pthread_attr_destroy(&attr);
    if(err) {
        printf("DKU AX: no feeder thread; no sound\n");
        run = false;
        return;
    }
    started = true;
    printf("DKU AX: up, %u Hz, %u ms ahead\n", (unsigned)AXGetInputSamplesPerSec(), LEAD * 1000u / AUDIO_MIX_RATE);
}

/* From the ProcUI callbacks: waits briefly for the feeder to act. */
void audio_ax_pause(bool on) {
    if(!started) return;
    want_pause = on;
    for(int i = 0; i < 50 && parked != on; i++) OSSleepTicks(OSMillisecondsToTicks(1));
}

void audio_ax_stop(void) {
    if(!started) return;
    run = false;
    pthread_join(thread, NULL);
    for(int c = 0; c < 2; c++) {
        AXVoiceBegin(voice[c]);
        AXSetVoiceState(voice[c], AX_VOICE_STATE_STOPPED);
        AXVoiceEnd(voice[c]);
        AXFreeVoice(voice[c]);
        free(ring[c]);
    }
    AXQuit();
    started = false;
}
