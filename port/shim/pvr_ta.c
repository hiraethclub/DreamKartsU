/* DKU shim: PVR state, texture memory and the software Tile Accelerator
   (port/ta.h). Context defaults follow KallistiOS's pvr_poly_cxt_col/txr. */
#include <dc/pvr.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ta.h"

/* ---- State ---- */

static pvr_shim_state_t st = {
    .bg = {0, 0, 0},
    .fog_color = {1, 0.5f, 0.5f, 0.5f},
    .fog_far_depth = 1000.0f,
    .shadow_scale = 1.0f,
    .presort = true,
    .pt_alpha_ref = 0xff,
};

const pvr_shim_state_t *pvr_shim_state(void) { return &st; }

void pvr_shim_set_reg(uint32_t reg, uint32_t value) {
    if(reg == PVR_PT_ALPHA_REF) st.pt_alpha_ref = value & 0xff;
}

void pvr_set_bg_color(float r, float g, float b) {
    st.bg[0] = r;
    st.bg[1] = g;
    st.bg[2] = b;
}

void pvr_set_shadow_scale(bool enable, float scale_value) {
    st.shadow_enabled = enable;
    st.shadow_scale = scale_value;
}

void pvr_set_presort_mode(bool presort) { st.presort = presort; }

void pvr_fog_table_color(float a, float r, float g, float b) {
    st.fog_color[0] = a;
    st.fog_color[1] = r;
    st.fog_color[2] = g;
    st.fog_color[3] = b;
}

void pvr_fog_far_depth(float d) { st.fog_far_depth = d; }

void pvr_fog_table_custom(float table[]) {
    memcpy(st.fog_table, table, sizeof(st.fog_table));
}

/* Entry k lies at far / 2^(k/16) (world.c fog_table_octaves): linear in
   distance between start and end. */
void pvr_fog_table_linear(float start, float end) {
    pvr_fog_far_depth(end);
    for(int k = 0; k <= 128; k++) {
        float d = end / exp2f((float)k / 16.0f);
        float f = (d - start) / (end - start);
        st.fog_table[k] = f < 0 ? 0 : f > 1 ? 1 : f;
    }
}

/* ---- Texture memory ---- */

#define VRAM_BYTES (8u << 20)
#define MAX_BLOCKS 1024

static pvr_shim_block_t blocks[MAX_BLOCKS];
static int nblocks;
static size_t vram_used;

pvr_ptr_t pvr_mem_malloc(size_t size) {
    if(nblocks >= MAX_BLOCKS) return NULL;
    void *p = calloc(1, size ? size : 1);
    if(!p) return NULL;
    blocks[nblocks++] = (pvr_shim_block_t){p, size, 1};
    vram_used += size;
    return p;
}

void pvr_mem_free(pvr_ptr_t p) {
    for(int i = 0; i < nblocks; i++)
        if(blocks[i].base == p) {
            vram_used -= blocks[i].size;
            free(p);
            blocks[i] = blocks[--nblocks];
            return;
        }
}

size_t pvr_mem_available(void) { return vram_used < VRAM_BYTES ? VRAM_BYTES - vram_used : 0; }

const pvr_shim_block_t *pvr_shim_block(pvr_ptr_t p) {
    for(int i = 0; i < nblocks; i++)
        if((uint8_t *)p >= (uint8_t *)blocks[i].base &&
           (uint8_t *)p < (uint8_t *)blocks[i].base + blocks[i].size)
            return &blocks[i];
    return NULL;
}

void pvr_txr_load(const void *src, pvr_ptr_t dst, size_t count) {
    memcpy(dst, src, count);
    pvr_shim_block_t *b = (pvr_shim_block_t *)pvr_shim_block(dst);
    if(b) b->generation++;
}

/* ---- Polygon contexts ---- */

void pvr_poly_cxt_col(pvr_poly_cxt_t *dst, pvr_list_t list) {
    memset(dst, 0, sizeof(*dst));
    dst->list_type = (int)list;
    dst->fmt.color = 0;
    dst->gen.shading = PVR_SHADE_GOURAUD;
    dst->gen.fog_type = PVR_FOG_DISABLE;
    dst->gen.culling = PVR_CULLING_CCW;
    dst->gen.clip_mode = PVR_USERCLIP_DISABLE;
    dst->depth.comparison = PVR_DEPTHCMP_GREATER;
    if(list == PVR_LIST_OP_POLY) {
        dst->gen.alpha = 0;
        dst->blend.src = PVR_BLEND_ONE;
        dst->blend.dst = PVR_BLEND_ZERO;
        dst->depth.write = PVR_DEPTHWRITE_ENABLE;
    }
    else if(list == PVR_LIST_PT_POLY) {
        dst->gen.alpha = 1;
        dst->blend.src = PVR_BLEND_SRCALPHA;
        dst->blend.dst = PVR_BLEND_INVSRCALPHA;
        dst->depth.write = PVR_DEPTHWRITE_ENABLE;
    }
    else {
        dst->gen.alpha = 1;
        dst->blend.src = PVR_BLEND_SRCALPHA;
        dst->blend.dst = PVR_BLEND_INVSRCALPHA;
        dst->depth.write = PVR_DEPTHWRITE_DISABLE;
    }
    dst->txr.enable = 0;
}

void pvr_poly_cxt_txr(pvr_poly_cxt_t *dst, pvr_list_t list, int textureformat, int tw, int th,
                      pvr_ptr_t textureaddr, int filtering) {
    pvr_poly_cxt_col(dst, list);
    dst->txr.enable = 1;
    dst->txr.filter = filtering;
    dst->txr.mipmap = 0;
    dst->txr.mipmap_bias = PVR_MIPBIAS_NORMAL;
    dst->txr.uv_clamp = PVR_UVCLAMP_NONE;
    dst->txr.alpha = list == PVR_LIST_OP_POLY ? 0 : 1;
    dst->txr.env = PVR_TXRENV_MODULATE;
    dst->txr.width = tw;
    dst->txr.height = th;
    dst->txr.format = textureformat;
    dst->txr.base = textureaddr;
}

void pvr_poly_compile(pvr_poly_hdr_t *dst, const pvr_poly_cxt_t *src) {
    dst->tag = PVR_SHIM_POLY_TAG;
    dst->cxt = *src;
}

void pvr_mod_compile(pvr_mod_hdr_t *dst, pvr_list_t list, uint32_t mode, uint32_t cull) {
    dst->tag = PVR_SHIM_MOD_TAG;
    dst->list = (int)list;
    dst->mode = (int)mode;
    dst->culling = (int)cull;
}

/* ---- The frame being built ---- */

#define MAX_HDRS     8192
#define MAX_VERTS    (3 * 131072)
#define MAX_BATCHES  16384
#define MAX_MOD_VERTS (3 * 8192)
#define MAX_VOLUMES  1024
#define MAX_STRIP_VERTS 1024

static pvr_poly_hdr_t f_hdrs[MAX_HDRS];
static ta_vertex_t f_verts[MAX_VERTS];
static ta_batch_t f_batches[TA_LISTS][MAX_BATCHES];
static ta_mod_vertex_t f_mod_verts[MAX_MOD_VERTS];
static ta_volume_t f_volumes[MAX_VOLUMES];
static ta_frame_t frame;

static int cur_list = -1;        /* PVR list id */
static int cur_hdr = -1;         /* index into f_hdrs */
static bool hdr_pending;         /* a new header since the last batch */
static int clip_y0 = 0, clip_y1 = 480;
static ta_vertex_t strip_buf[MAX_STRIP_VERTS];
static int strip_n;
static uint32_t dr_slot[16] __attribute__((aligned(32)));
static int mod_half;             /* 0: next word starts a modifier triangle */
static ta_mod_vertex_t mod_tri[3];
static bool mod_volume_open, mod_close_next;
uint32_t pvr_dr_addr;

static pvr_stats_t stats;
static uint64_t frame_start_us;
uint64_t timer_us_gettime64(void);

static void frame_reset(void) {
    frame.hdrs = f_hdrs;
    frame.verts = f_verts;
    frame.mod_verts = f_mod_verts;
    frame.volumes = f_volumes;
    frame.nhdrs = frame.nverts = frame.nmod_verts = frame.nvolumes = 0;
    for(int l = 0; l < TA_LISTS; l++) {
        frame.batches[l] = f_batches[l];
        frame.nbatches[l] = 0;
    }
    frame.overflow = false;
    cur_list = -1;
    cur_hdr = -1;
    strip_n = 0;
}

static int ta_list(int pvr_list) {
    switch(pvr_list) {
        case PVR_LIST_OP_POLY: return TA_OP;
        case PVR_LIST_PT_POLY: return TA_PT;
        case PVR_LIST_TR_POLY: return TA_TR;
        default: return -1;
    }
}

/* The batch new triangles join: the open one if nothing changed. */
static ta_batch_t *batch_for(int l) {
    int n = frame.nbatches[l];
    ta_batch_t *b = n ? &frame.batches[l][n - 1] : NULL;
    if(b && !hdr_pending && b->hdr == cur_hdr && b->clip_y0 == clip_y0 && b->clip_y1 == clip_y1 &&
       b->first + b->count == frame.nverts)
        return b;
    if(n >= MAX_BATCHES) {
        frame.overflow = true;
        return NULL;
    }
    b = &frame.batches[l][n];
    frame.nbatches[l] = n + 1;
    *b = (ta_batch_t){cur_hdr, frame.nverts, 0, clip_y0, clip_y1};
    hdr_pending = false;
    return b;
}

/* A finished strip as triangles, each kept in the strip's winding. */
static void flush_strip(void) {
    int l = ta_list(cur_list);
    if(l < 0 || cur_hdr < 0 || strip_n < 3) {
        strip_n = 0;
        return;
    }
    int ntri = strip_n - 2;
    if(frame.nverts + ntri * 3 > MAX_VERTS) {
        frame.overflow = true;
        strip_n = 0;
        return;
    }
    ta_batch_t *b = batch_for(l);
    if(!b) {
        strip_n = 0;
        return;
    }
    for(int k = 0; k < ntri; k++) {
        const ta_vertex_t *a = &strip_buf[k], *c = &strip_buf[k + 2];
        const ta_vertex_t *m = &strip_buf[k + 1];
        if(k & 1) {
            const ta_vertex_t *t = a;
            a = m;
            m = t;
        }
        frame.verts[frame.nverts++] = *a;
        frame.verts[frame.nverts++] = *m;
        frame.verts[frame.nverts++] = *c;
    }
    b->count += ntri * 3;
    strip_n = 0;
}

static void mod_triangle(void) {
    if(!mod_volume_open) {
        if(frame.nvolumes >= MAX_VOLUMES) {
            frame.overflow = true;
            return;
        }
        frame.volumes[frame.nvolumes++] = (ta_volume_t){frame.nmod_verts, 0};
        mod_volume_open = true;
    }
    if(frame.nmod_verts + 3 > MAX_MOD_VERTS) {
        frame.overflow = true;
        return;
    }
    for(int i = 0; i < 3; i++) frame.mod_verts[frame.nmod_verts++] = mod_tri[i];
    frame.volumes[frame.nvolumes - 1].count += 3;
    if(mod_close_next) {
        mod_volume_open = false;
        mod_close_next = false;
    }
}

int pvr_init(const pvr_init_params_t *params) {
    (void)params;
    frame_reset();
    backend_init();
    return 0;
}

int pvr_init_defaults(void) { return pvr_init(NULL); }

int pvr_shutdown(void) {
    backend_shutdown();
    return 0;
}

int pvr_scene_begin(void) {
    frame_reset();
    frame_start_us = timer_us_gettime64();
    return 0;
}

void pvr_list_begin(pvr_list_t list) {
    flush_strip();
    cur_list = (int)list;
    cur_hdr = -1;
    mod_half = 0;
    mod_volume_open = mod_close_next = false;
    clip_y0 = 0;
    clip_y1 = 480;
}

int pvr_list_finish(void) {
    flush_strip();
    return 0;
}

int pvr_prim(const void *data, size_t size) {
    uint32_t tag;
    memcpy(&tag, data, sizeof(tag));
    if(tag == PVR_SHIM_POLY_TAG && size == sizeof(pvr_poly_hdr_t)) {
        flush_strip();
        if(frame.nhdrs >= MAX_HDRS) {
            frame.overflow = true;
            return -1;
        }
        frame.hdrs[frame.nhdrs] = *(const pvr_poly_hdr_t *)data;
        cur_hdr = frame.nhdrs++;
        hdr_pending = true;
        return 0;
    }
    if(tag == PVR_SHIM_MOD_TAG && size == sizeof(pvr_mod_hdr_t)) {
        const pvr_mod_hdr_t *m = data;
        if(m->mode == PVR_MODIFIER_OTHER_POLY) mod_volume_open = false;
        else mod_close_next = true;   /* the next triangle closes the volume */
        return 0;
    }
    fprintf(stderr, "DKU pvr_prim: unknown header (%zu bytes)\n", size);
    return -1;
}

void *pvr_dr_target(void) { return dr_slot; }

void pvr_dr_commit(void *addr) {
    const uint32_t *d = addr;
    const float *f = addr;
    if(cur_list == PVR_LIST_OP_MOD || cur_list == PVR_LIST_TR_MOD) {
        /* Two 32-byte halves per triangle (render.c send_mod_tri). */
        if(mod_half == 0) {
            mod_tri[0] = (ta_mod_vertex_t){f[1], f[2], f[3]};
            mod_tri[1] = (ta_mod_vertex_t){f[4], f[5], f[6]};
            mod_tri[2].x = f[7];
            mod_half = 1;
        }
        else {
            mod_tri[2].y = f[0];
            mod_tri[2].z = f[1];
            mod_half = 0;
            mod_triangle();
        }
        return;
    }
    uint32_t cmd = d[0];
    if((cmd & 0xe0000000u) == PVR_CMD_USERCLIP) {
        flush_strip();
        clip_y0 = (int)d[5] * 32;
        clip_y1 = ((int)d[7] + 1) * 32;
        return;
    }
    if((cmd & 0xe0000000u) == PVR_CMD_VERTEX) {
        const pvr_vertex_t *v = addr;
        if(strip_n < MAX_STRIP_VERTS)
            strip_buf[strip_n++] = (ta_vertex_t){v->x, v->y, v->z, v->u, v->v, v->argb, v->oargb};
        else
            frame.overflow = true;
        if(cmd == PVR_CMD_VERTEX_EOL) flush_strip();
    }
}

int pvr_scene_finish(void) {
    flush_strip();
    backend_draw(&frame, &st);
    size_t vtx = (size_t)frame.nverts * sizeof(ta_vertex_t);
    stats.vtx_buffer_used = vtx;
    if(vtx > stats.vtx_buffer_used_max) stats.vtx_buffer_used_max = vtx;
    stats.frame_count++;
    if(frame.overflow) {
        static int warned;
        if(warned++ < 4) fprintf(stderr, "DKU TA: frame overflowed a buffer, geometry dropped\n");
    }
    return 0;
}

int pvr_wait_ready(void) {
    backend_present();
    return 0;
}

void pvr_shim_report(uint64_t render_us, size_t vtx_bytes) {
    stats.rnd_last_time = render_us;
    (void)vtx_bytes;
}

int pvr_get_stats(pvr_stats_t *stat) {
    *stat = stats;
    return 0;
}
