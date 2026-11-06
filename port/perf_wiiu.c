/* DKU: frame timing lines for the console test log, one a second:

     PERF TRACKS fps=59.9 miss=0 frame=16.6/18.2 game=7.1/9.3 gpuwait=0.0/0.1
          draw=1.0/1.2 present=0.2/0.3 flipwait=8.1/10.0 gpulate=0 up=0/0.0ms
          hdr=300 batch=400 tri=12000 axunder=0 musicdry=0

   (average/worst, in ms; one line in the file). A frame over 20 ms also gets
   its own HITCH line saying where the time went, at most 10 a second. miss
   counts frames over 25 ms: at least one refresh was missed. gpulate counts
   frames whose GPU work was still running when the next frame's commands
   began: with a big gpuwait or miss, the GPU is the limit. */
#include <stdbool.h>
#include <stdio.h>

#include "dk.h"
#include "perf_wiiu.h"

/* Absent in the save check's build. */
extern Game game __attribute__((weak));

static const char *const state_names[] = {"TITLE",  "MENU", "TRACKS", "INTRO",  "COUNTDOWN",
                                          "RACE",   "RESULTS", "MAIN",   "ONLINE", "KARTS", "FLAPPY"};

static const char *state_name(int s) {
    return s >= 0 && s < (int)(sizeof(state_names) / sizeof(state_names[0])) ? state_names[s] : "?";
}

typedef struct {
    uint64_t sum;
    uint32_t max;
} Stat;

static void add(Stat *s, uint32_t v) {
    s->sum += v;
    if(v > s->max) s->max = v;
}

/* "12.3/45.6" in ms. */
static const char *fmt(char *out, const Stat *s, uint32_t n) {
    uint32_t a = n ? (uint32_t)(s->sum / n) : 0;
    sprintf(out, "%u.%u/%u.%u", (unsigned)(a / 1000), (unsigned)(a / 100 % 10), (unsigned)(s->max / 1000),
            (unsigned)(s->max / 100 % 10));
    return out;
}

void perf_frame(const perf_frame_t *p) {
    static Stat frame, gamet, gpuwait, draw, present, flipwait;
    static uint32_t n, miss, late, uploads, upload_us, hitches, state = 0xffffffffu;
    static uint64_t window;
    int s = &game ? game.state : -1;
    if((uint32_t)s != state) {
        state = (uint32_t)s;
        printf("DKU STATE: %s\n", state_name(s));
    }
    add(&frame, p->frame);
    add(&gamet, p->game);
    add(&gpuwait, p->gpuwait);
    add(&draw, p->draw);
    add(&present, p->present);
    add(&flipwait, p->flipwait);
    n++;
    window += p->frame;
    if(p->frame > 25000) miss++;
    late += p->gpu_late;
    uploads += p->uploads;
    upload_us += p->upload_us;
    if(p->frame > 20000 && hitches < 10) {
        hitches++;
        printf("HITCH %s frame=%u game=%u gpuwait=%u draw=%u present=%u flipwait=%u up=%u/%uus gpulate=%u tri=%u\n",
               state_name(s), (unsigned)p->frame, (unsigned)p->game, (unsigned)p->gpuwait, (unsigned)p->draw,
               (unsigned)p->present, (unsigned)p->flipwait, (unsigned)p->uploads, (unsigned)p->upload_us,
               (unsigned)p->gpu_late, (unsigned)p->tris);
    }
    if(window < 1000000) return;
    char a[24], b[24], c[24], d[24], e[24], f[24];
    uint32_t tenths = (uint32_t)((uint64_t)n * 10000000u / window);
    printf("PERF %s fps=%u.%u miss=%u frame=%s game=%s gpuwait=%s draw=%s present=%s flipwait=%s gpulate=%u "
           "up=%u/%ums hdr=%u batch=%u tri=%u axunder=%u musicdry=%u\n",
           state_name(s), (unsigned)(tenths / 10), (unsigned)(tenths % 10), (unsigned)miss, fmt(a, &frame, n),
           fmt(b, &gamet, n), fmt(c, &gpuwait, n), fmt(d, &draw, n), fmt(e, &present, n), fmt(f, &flipwait, n),
           (unsigned)late, (unsigned)uploads, (unsigned)(upload_us / 1000), (unsigned)p->hdrs, (unsigned)p->batches,
           (unsigned)p->tris, (unsigned)dku_ax_underruns, (unsigned)dku_music_dry);
    frame = gamet = gpuwait = draw = present = flipwait = (Stat){0, 0};
    n = miss = late = uploads = upload_us = hitches = 0;
    window = 0;
}
