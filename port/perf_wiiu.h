/* DKU: frame timing for the console test log (perf_wiiu.c). All in us. */
#ifndef DKU_PERF_WIIU_H
#define DKU_PERF_WIIU_H

#include <stdint.h>

typedef struct {
    uint32_t frame;      /* one present to the next */
    uint32_t game;       /* game update and building the frame: CPU */
    uint32_t gpuwait;    /* waiting for the GPU to free this frame's buffers */
    uint32_t draw;       /* writing GPU commands */
    uint32_t present;    /* blit commands, swap, flush */
    uint32_t flipwait;   /* waiting for the display (idle time when on target) */
    uint32_t upload_us;  /* the textures converted this frame */
    uint32_t uploads;
    uint32_t hdrs, batches, tris;
    uint32_t gpu_late;   /* 1: the last frame's GPU work had not ended by now */
} perf_frame_t;

void perf_frame(const perf_frame_t *p);

/* Sound health, counted where it happens. */
extern volatile uint32_t dku_ax_underruns;     /* audio_ax.c */
extern volatile uint32_t dku_music_dry;        /* cdda_shim.c: decoder fell behind */

#endif
