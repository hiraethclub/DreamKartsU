/* DKU: render.c's hooks into the GPU path (port/render_native.c). Call
   after the strip's or mesh's header has been sent; XMTRX holds the matrix
   its vertices take. */
#ifndef DKU_RENDER_NATIVE_H
#define DKU_RENDER_NATIVE_H

#include "render.h"

void dku_native_strip(const RV *v, const uint32_t *spec, int n);
/* v, or kv coloured col[group] with u = v = 0. */
void dku_native_mesh(const RV *v, const KV *kv, const uint32_t *col, int nv, const uint16_t *idx, int ni,
                     bool cull);

#endif
