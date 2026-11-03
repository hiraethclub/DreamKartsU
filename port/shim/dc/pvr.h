/* DKU shim: the parts of KallistiOS's <dc/pvr.h> the game uses outside
   render.c. Names, fields and constant values follow KOS so upstream code
   compiles unchanged; pvr_poly_hdr_t holds decoded state instead of the
   Tile Accelerator's packed words, and the port's renderer (render_gl.c,
   render_gx2.c) reads it through pvr_shim_*(). */
#ifndef DKU_SHIM_DC_PVR_H
#define DKU_SHIM_DC_PVR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void *pvr_ptr_t;
typedef uint32_t pvr_list_t;

#define PVR_LIST_OP_POLY 0
#define PVR_LIST_OP_MOD  1
#define PVR_LIST_TR_POLY 2
#define PVR_LIST_TR_MOD  3
#define PVR_LIST_PT_POLY 4

#define PVR_BINSIZE_0  0
#define PVR_BINSIZE_8  8
#define PVR_BINSIZE_16 16
#define PVR_BINSIZE_32 32

#define PVR_TXRFMT_NONE        0
#define PVR_TXRFMT_VQ_DISABLE  (0 << 30)
#define PVR_TXRFMT_VQ_ENABLE   (1 << 30)
#define PVR_TXRFMT_ARGB1555    (0 << 27)
#define PVR_TXRFMT_RGB565      (1 << 27)
#define PVR_TXRFMT_ARGB4444    (2 << 27)
#define PVR_TXRFMT_YUV422      (3 << 27)
#define PVR_TXRFMT_TWIDDLED    (0 << 26)
#define PVR_TXRFMT_NONTWIDDLED (1 << 26)
#define PVR_TXRFMT_FMT_MASK    (7 << 27)

#define PVR_FILTER_NONE     0
#define PVR_FILTER_NEAREST  0
#define PVR_FILTER_BILINEAR 2

#define PVR_UVCLAMP_NONE 0
#define PVR_UVCLAMP_V    1
#define PVR_UVCLAMP_U    2
#define PVR_UVCLAMP_UV   3

#define PVR_TXRENV_REPLACE       0
#define PVR_TXRENV_MODULATE      1
#define PVR_TXRENV_DECAL         2
#define PVR_TXRENV_MODULATEALPHA 3

/* D-adjust in quarters: 1_00 is the hardware's own pick. */
#define PVR_MIPBIAS_NORMAL 4
#define PVR_MIPBIAS_0_25 1
#define PVR_MIPBIAS_0_50 2
#define PVR_MIPBIAS_0_75 3
#define PVR_MIPBIAS_1_00 4
#define PVR_MIPBIAS_1_25 5
#define PVR_MIPBIAS_1_50 6
#define PVR_MIPBIAS_1_75 7
#define PVR_MIPBIAS_2_00 8

#define PVR_BLEND_ZERO         0
#define PVR_BLEND_ONE          1
#define PVR_BLEND_DESTCOLOR    2
#define PVR_BLEND_INVDESTCOLOR 3
#define PVR_BLEND_SRCALPHA     4
#define PVR_BLEND_INVSRCALPHA  5
#define PVR_BLEND_DESTALPHA    6
#define PVR_BLEND_INVDESTALPHA 7

/* Compared on 1/w: GREATER means nearer. */
#define PVR_DEPTHCMP_NEVER    0
#define PVR_DEPTHCMP_LESS     1
#define PVR_DEPTHCMP_EQUAL    2
#define PVR_DEPTHCMP_LEQUAL   3
#define PVR_DEPTHCMP_GREATER  4
#define PVR_DEPTHCMP_NOTEQUAL 5
#define PVR_DEPTHCMP_GEQUAL   6
#define PVR_DEPTHCMP_ALWAYS   7

/* The hardware bit is "disable": ENABLE is 0. */
#define PVR_DEPTHWRITE_ENABLE  0
#define PVR_DEPTHWRITE_DISABLE 1

#define PVR_FOG_TABLE   0
#define PVR_FOG_VERTEX  1
#define PVR_FOG_DISABLE 2
#define PVR_FOG_TABLE2  3

#define PVR_CULLING_NONE  0
#define PVR_CULLING_SMALL 1
#define PVR_CULLING_CCW   2
#define PVR_CULLING_CW    3

#define PVR_USERCLIP_DISABLE 0
#define PVR_USERCLIP_INSIDE  2
#define PVR_USERCLIP_OUTSIDE 3

#define PVR_SHADE_FLAT    0
#define PVR_SHADE_GOURAUD 1

#define PVR_SPECULAR_DISABLE 0
#define PVR_SPECULAR_ENABLE  1

#define PVR_MODIFIER_OTHER_POLY        0
#define PVR_MODIFIER_INCLUDE_LAST_POLY 1
#define PVR_MODIFIER_EXCLUDE_LAST_POLY 2

#define PVR_CMD_POLYHDR    0x80840000
#define PVR_CMD_VERTEX     0xe0000000
#define PVR_CMD_VERTEX_EOL 0xf0000000
#define PVR_CMD_USERCLIP   0x20000000

/* Registers the game writes with PVR_SET. */
#define PVR_PT_ALPHA_REF 0x011c
#define PVR_OBJECT_CLIP  0x0078
#define PVR_SET(reg, value) pvr_shim_set_reg((reg), (uint32_t)(value))

typedef struct {
    uint32_t flags;
    float x, y, z;
    float u, v;
    uint32_t argb;
    uint32_t oargb;
} pvr_vertex_t;

typedef struct {
    int list_type;
    struct {
        int alpha;
        int shading;
        int fog_type;
        int culling;
        int color_clamp;
        int clip_mode;
        int modifier_mode;
        int specular;
        int alpha2;
        int fog_type2;
        int color_clamp2;
    } gen;
    struct {
        int src, dst;
        int src_enable, dst_enable;
        int src2, dst2;
        int src_enable2, dst_enable2;
    } blend;
    struct {
        int color;
        int uv;
        int modifier;
    } fmt;
    struct {
        int comparison;
        int write;
    } depth;
    struct {
        int enable;
        int filter;
        int mipmap;
        int mipmap_bias;
        int uv_flip;
        int uv_clamp;
        int alpha;
        int env;
        int width;
        int height;
        int format;
        pvr_ptr_t base;
    } txr, txr2;
} pvr_poly_cxt_t;

/* A compiled header: the context it came from, tagged so pvr_prim can
   tell it from a modifier header. Opaque to the game. */
#define PVR_SHIM_POLY_TAG 0x504f4c59u   /* "POLY" */
#define PVR_SHIM_MOD_TAG  0x4d4f4456u   /* "MODV" */
typedef struct {
    uint32_t tag;
    pvr_poly_cxt_t cxt;
} pvr_poly_hdr_t;

typedef struct {
    uint32_t tag;
    int list;
    int mode;
    int culling;
} pvr_mod_hdr_t;

typedef struct {
    int opb_sizes[5];
    int vertex_buf_size;
    int dma_enabled;
    int fsaa_enabled;
    int autosort_disabled;
    int opb_overflow_count;
    int vbuf_doublebuf_disabled;
} pvr_init_params_t;

typedef struct {
    uint64_t frame_last_time;
    uint64_t reg_last_time;
    uint64_t rnd_last_time;
    uint64_t buf_last_time;
    size_t frame_count;
    size_t vbl_count;
    size_t vtx_buffer_used;
    size_t vtx_buffer_used_max;
    float frame_rate;
    uint32_t enabled_list_mask;
} pvr_stats_t;

int pvr_init(const pvr_init_params_t *params);
int pvr_init_defaults(void);
int pvr_shutdown(void);

pvr_ptr_t pvr_mem_malloc(size_t size);
void pvr_mem_free(pvr_ptr_t p);
size_t pvr_mem_available(void);
void pvr_txr_load(const void *src, pvr_ptr_t dst, size_t count);

void pvr_poly_cxt_col(pvr_poly_cxt_t *dst, pvr_list_t list);
void pvr_poly_cxt_txr(pvr_poly_cxt_t *dst, pvr_list_t list, int textureformat, int tw, int th,
                      pvr_ptr_t textureaddr, int filtering);
void pvr_poly_compile(pvr_poly_hdr_t *dst, const pvr_poly_cxt_t *src);
void pvr_mod_compile(pvr_mod_hdr_t *dst, pvr_list_t list, uint32_t mode, uint32_t cull);

void pvr_set_bg_color(float r, float g, float b);
void pvr_set_shadow_scale(bool enable, float scale_value);
void pvr_set_presort_mode(bool presort);

void pvr_fog_table_color(float a, float r, float g, float b);
void pvr_fog_table_linear(float start, float end);
void pvr_fog_table_custom(float table[]);
void pvr_fog_far_depth(float d);

int pvr_get_stats(pvr_stats_t *stat);

/* Direct rendering: render.c writes each 32-byte vertex (or user clip, or
   half a modifier triangle) into pvr_dr_target() and hands it over with
   pvr_dr_commit(); headers go through pvr_prim. */
void pvr_list_begin(pvr_list_t list);
int pvr_list_finish(void);
int pvr_prim(const void *data, size_t size);
void *pvr_dr_target(void);
void pvr_dr_commit(void *addr);
/* Only DK_HW capture builds (the TA sink probe) touch it. */
extern uint32_t pvr_dr_addr;

/* Paces the frame loop: the platform presents the finished frame, pumps
   its events and waits for the next display refresh here. */
int pvr_wait_ready(void);
int pvr_scene_begin(void);
int pvr_scene_finish(void);

/* ---- Port side: state for the renderer, not called by upstream. ---- */

void pvr_shim_set_reg(uint32_t reg, uint32_t value);

typedef struct {
    float bg[3];
    float fog_color[4];      /* a, r, g, b */
    float fog_table[129];    /* PVR order: entry 0 is the far end */
    float fog_far_depth;     /* the far depth pvr_fog_far_depth set */
    float shadow_scale;
    bool shadow_enabled;
    bool presort;
    uint32_t pt_alpha_ref;   /* 0..255 */
} pvr_shim_state_t;

const pvr_shim_state_t *pvr_shim_state(void);

/* The allocation holding p (texture memory), so the renderer can re-upload
   a texture whose bytes pvr_txr_load changed. generation counts loads. */
typedef struct {
    void *base;
    size_t size;
    uint32_t generation;
} pvr_shim_block_t;

const pvr_shim_block_t *pvr_shim_block(pvr_ptr_t p);

/* Frame-time accounting the renderer reports for pvr_get_stats. */
void pvr_shim_report(uint64_t render_us, size_t vtx_bytes);

#endif
