/* DKU: a backend that draws nothing, for headless runs of the game logic
   (tools/test_host.sh NULL=1, and the big-endian run under qemu). It still
   checks each frame's batches so TA decoding bugs show up as failures. */
#include <stdio.h>

#include "plat.h"
#include "ta.h"

static unsigned long frames, bad;

void backend_init(void) { plat_init(); }

void backend_shutdown(void) {
    printf("DKU null backend: %lu frames, %lu bad batches\n", frames, bad);
}

void backend_draw(const ta_frame_t *f, const pvr_shim_state_t *st) {
    (void)st;
    frames++;
    for(int l = 0; l < TA_LISTS; l++)
        for(int i = 0; i < f->nbatches[l]; i++) {
            const ta_batch_t *b = &f->batches[l][i];
            if(b->hdr < 0 || b->hdr >= f->nhdrs || b->count % 3 || b->first + b->count > f->nverts) bad++;
        }
}

void backend_present(void) { plat_frame_done(); }
