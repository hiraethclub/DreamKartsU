/* DKU: a platform with no window, input or sound, for automated runs.

   Time is virtual: each shown frame is one 60 Hz display period, plus the
   real time since then capped below a period, so the game sees a steady
   60 fps however fast the host runs and timings within a frame still
   advance. DKU_REPRO=1 makes that in-frame time 1 us per call instead of
   real time, so runs repeat exactly (the test scripts set it; real
   in-frame time made mid-frame timer reads, such as the sea's scroll,
   differ by an ulp between runs and drift apart).
   DKU_MAX_FRAMES (environment) quits after that many frames. */
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#include "plat.h"

#define PERIOD_US 16667ull

static uint64_t frames, frame_real0, max_frames, repro_calls;
static bool repro;
static char save_dir[512];

static uint64_t real_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

uint64_t plat_time_us(void) {
    /* main reads the clock before plat_init. */
    if(!frame_real0) frame_real0 = real_us();
    uint64_t in_frame = repro ? ++repro_calls : real_us() - frame_real0;
    if(in_frame > PERIOD_US - 667) in_frame = PERIOD_US - 667;
    return frames * PERIOD_US + in_frame;
}

void plat_init(void) {
    static bool done;
    if(done) return;
    done = true;
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char *mf = getenv("DKU_MAX_FRAMES");
    max_frames = mf ? strtoull(mf, NULL, 10) : 0;
    const char *r = getenv("DKU_REPRO");
    repro = r && *r && *r != '0';
    if(!frame_real0) frame_real0 = real_us();
}

void plat_frame_done(void) {
    frames++;
    repro_calls = 0;
    frame_real0 = real_us();
}

bool plat_quit_requested(void) { return max_frames && frames >= max_frames; }

const char *plat_save_dir(void) {
    if(!save_dir[0]) {
        const char *d = getenv("DKU_SAVE_DIR");
        snprintf(save_dir, sizeof(save_dir), "%s/", d ? d : "build/host/save");
        mkdir(save_dir, 0755);
    }
    return save_dir;
}

bool plat_pad(int i, cont_state_t *out) {
    (void)i;
    (void)out;
    return false;
}

bool plat_pad_has_rumble(int i) {
    (void)i;
    return false;
}

void plat_rumble(int i, int power, int freq) {
    (void)i;
    (void)power;
    (void)freq;
}

bool plat_keyboard(void) { return false; }

int plat_keys(int *buf, int max) {
    (void)buf;
    (void)max;
    return 0;
}
