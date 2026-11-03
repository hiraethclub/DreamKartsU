/* DKU: the software Tile Accelerator. Upstream render.c talks to the PVR
   through pvr_prim (headers) and pvr_dr_target/pvr_dr_commit (32-byte
   vertex words); port/shim/pvr_ta.c decodes that stream into a frame of
   triangle batches per list, and a backend (render_null.c, render_gl.c,
   render_gx2.c) draws the frame.

   Vertices stay as the PVR gets them: screen x, y in the 640x480 frame and
   z = 1/w. A backend recovers clip space as w = 1/z,
   (x / 320 - 1, 1 - y / 240) * w, which keeps textures perspective
   correct. */
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
    int first;          /* first vertex in ta_frame_t.verts */
    int count;          /* vertices, a multiple of 3 */
    int clip_y0, clip_y1;   /* user clip, whole pixel rows [y0, y1) */
} ta_batch_t;

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
    bool overflow;      /* something was dropped: a buffer filled up */
} ta_frame_t;

/* The backend a build links in. */
void backend_init(void);
void backend_shutdown(void);
/* Draw a finished frame (called from pvr_scene_finish). */
void backend_draw(const ta_frame_t *f, const pvr_shim_state_t *st);
/* Show it, pump the platform's events and wait for the next refresh
   (called from pvr_wait_ready). */
void backend_present(void);

#endif
