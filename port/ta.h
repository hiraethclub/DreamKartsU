/* DKU: the software Tile Accelerator. Upstream render.c talks to the PVR
   through pvr_prim (headers) and pvr_dr_target/pvr_dr_commit (32-byte
   vertex words); port/shim/pvr_ta.c decodes that stream into a frame of
   triangle batches per list, and a backend (render_null.c, render_gl.c,
   render_gx2.c) draws the frame.

   Vertices stay as the PVR gets them: screen x, y in the 640x480 frame and
   z = 1/w. A backend recovers clip space as w = 1/z,
   (x / 320 - 1, 1 - y / 240) * w, which keeps textures perspective
   correct.

   World batches (DKU_NATIVE, PORT_PLAN.md principle 4) are the Wii U way
   instead: port/render_native.c hands over render.c's vertices untransformed,
   in model space, with the matrix that was in XMTRX, and the GPU's vertex
   shader transforms them and the GPU clips them. Their vertices go in the
   same array, and their triangles are indices (ta_frame_t.idx). Each matrix
   is stored ready for the shader: clip = mats[i] * (x, y, z, 1), with
   clip z the constant TA_NEAR_W (upstream's near plane, R_NEAR_W), so with
   halfZ clipping the GPU cuts at w = TA_NEAR_W exactly as render.c did. A
   backend draws world batches with depth range [0, DEPTH_K / TA_NEAR_W],
   which makes their window depth DEPTH_K / w, the same as screen batches. */
#ifndef DKU_TA_H
#define DKU_TA_H

#include <dc/pvr.h>

typedef struct {
    float x, y, z;      /* screen pixels, 1/w */
    float u, v;
    uint32_t argb;
    uint32_t oargb;     /* offset (specular) colour, added after texturing */
} ta_vertex_t;

/* Consecutive triangles (3 vertices each) under one header and one
   scissor. */
typedef struct {
    int hdr;            /* index into ta_frame_t.hdrs */
    int first;          /* first vertex in ta_frame_t.verts (world: first index in idx) */
    int count;          /* vertices (world: indices), a multiple of 3 */
    int clip_y0, clip_y1;   /* user clip, whole pixel rows [y0, y1) */
    int mat;            /* -1: screen vertices; else world, ta_frame_t.mats[mat] */
    bool cull;          /* world only: drop back faces (screen-space clockwise is front) */
} ta_batch_t;

#define TA_NEAR_W 0.25f

/* Column-major 4x4: clip = m * (x, y, z, 1). */
typedef struct {
    float m[16];
} ta_mat_t;

/* A closed modifier volume: triangles of x, y, 1/w. */
typedef struct {
    int first;          /* first vertex in ta_frame_t.mod_verts */
    int count;
} ta_volume_t;

typedef struct {
    float x, y, z;
} ta_mod_vertex_t;

enum { TA_OP, TA_PT, TA_TR, TA_LISTS };

typedef struct {
    pvr_poly_hdr_t *hdrs;
    int nhdrs;
    ta_vertex_t *verts;
    int nverts;
    ta_batch_t *batches[TA_LISTS];
    int nbatches[TA_LISTS];
    ta_mod_vertex_t *mod_verts;
    int nmod_verts;
    ta_volume_t *volumes;
    int nvolumes;
    uint32_t *idx;      /* world batches' triangles: indices into verts,
                           stored little-endian (ta_idx) */
    int nidx;
    ta_mat_t *mats;
    int nmats;
    bool overflow;      /* something was dropped: a buffer filled up */
} ta_frame_t;

/* World batches, for port/render_native.c (call after the header is sent).
   ta_world_matrix takes render.c's XMTRX (view-projection * model, column
   major, giving screen x * w, y * w and w); the vertices and indices that
   follow use it. NULL when a buffer is full (the frame's overflow flag says
   so). Indices are into the whole vertex array: add ta_world_base(), the
   latest ta_world_vertices' first vertex. */
void ta_world_matrix(const float *xmtrx);
/* An index as the frame stores it, and back: little-endian on every
   machine, which is what the Wii U's GPU reads fastest (render_gx2.c). */
static inline uint32_t ta_idx(uint32_t i) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return __builtin_bswap32(i);
#else
    return i;
#endif
}
ta_vertex_t *ta_world_vertices(int n);
uint32_t ta_world_base(void);
uint32_t *ta_world_indices(int n, bool cull);

/* The backend a build links in. */
void backend_init(void);
void backend_shutdown(void);
/* Where the next frame's vertices and indices go (called from
   pvr_scene_begin). A backend that draws them where they are returns its
   own GPU memory, with room for *max_verts and *max_idx, and must not hand
   it out while the GPU may still read it; false keeps the TA's own arrays,
   which the backend then copies. */
bool backend_frame_memory(ta_vertex_t **verts, int *max_verts, uint32_t **idx, int *max_idx);
/* Draw a finished frame (called from pvr_scene_finish). */
void backend_draw(const ta_frame_t *f, const pvr_shim_state_t *st);
/* Show it, pump the platform's events and wait for the next refresh
   (called from pvr_wait_ready). */
void backend_present(void);

#endif
