/* DKU: the Wii U way to draw render.c's 3D geometry (PORT_PLAN.md,
   principle 4). The Dreamcast's PowerVR could not transform vertices, so
   upstream render.c transforms, culls and near-clips every vertex on the
   SH-4 (with SH4ZAM and src/xform.S) and sends screen-space triangles. On
   the Wii U the GPU does that work: render.c's hooks (marked DKU, under
   DKU_NATIVE) send each strip and mesh here instead, with its vertices as
   they are, and the matrix render.c had loaded into XMTRX goes with them
   (port/ta.h, world batches). The GPU then transforms, clips at upstream's
   near plane and culls back faces.

   What render.c still decides stays there: headers, which strips to draw
   at all (its bounding spheres), the viewport and split screen (the TA's
   user clip, a scissor here). */
#include "render.h"
#include "render_native.h"
#include "ta.h"
#include <sh4zam/shz_sh4zam.h>

static void load_matrix(void) {
    shz_mat4x4_t m;
    shz_xmtrx_store_4x4(&m);
    ta_world_matrix(m.elem);
}

void dku_native_strip(const RV *v, const uint32_t *spec, int n) {
    if(n < 3) return;
    load_matrix();
    ta_vertex_t *d = ta_world_vertices(n);
    if(!d) return;
    uint32_t base = ta_world_base();
    for(int i = 0; i < n; i++)
        d[i] = (ta_vertex_t){v[i].x, v[i].y, v[i].z, v[i].u, v[i].v, v[i].c, spec ? spec[i] : 0};
    uint32_t *ix = ta_world_indices((n - 2) * 3, false);
    if(!ix) return;
    /* Each triangle in the strip's winding, as the TA's own strips. */
    for(int k = 0; k < n - 2; k++, ix += 3) {
        uint32_t a = base + (uint32_t)k, b = a + 1, c = a + 2;
        if(k & 1) {
            ix[0] = b;
            ix[1] = a;
        }
        else {
            ix[0] = a;
            ix[1] = b;
        }
        ix[2] = c;
    }
}

void dku_native_mesh(const RV *v, const KV *kv, const uint32_t *col, int nv, const uint16_t *idx, int ni,
                     bool cull) {
    if(nv < 1 || ni < 3) return;
    load_matrix();
    ta_vertex_t *d = ta_world_vertices(nv);
    if(!d) return;
    uint32_t base = ta_world_base();
    if(v)
        for(int i = 0; i < nv; i++) d[i] = (ta_vertex_t){v[i].x, v[i].y, v[i].z, v[i].u, v[i].v, v[i].c, 0};
    else
        for(int i = 0; i < nv; i++) d[i] = (ta_vertex_t){kv[i].x, kv[i].y, kv[i].z, 0, 0, col[kv[i].group], 0};
    ni -= ni % 3;
    uint32_t *ix = ta_world_indices(ni, cull);
    if(!ix) return;
    for(int i = 0; i < ni; i++) ix[i] = base + idx[i];
}
