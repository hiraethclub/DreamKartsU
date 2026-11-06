/* DKU: the Wii U backend (GX2). A port of render_gl.c: it draws the frame
   the software TA decoded (ta.h) into a 640x480 colour buffer, then scales
   that, 4:3 and pillarboxed, to the TV and the GamePad.

   - Lists draw in the PVR's order, OP, PT, TR, each in submission order.
   - Depth is the PVR's 1/w as render_gl.c does it: clip z is the constant
     DEPTH_K (port/shaders/main.vert), so window depth is DEPTH_K / w, with
     halfZ clip control. FLOAT_D24_S8, cleared to 0 (infinitely far) by a
     full-screen quad: Cemu 2.6 ignores GX2ClearDepthStencilEx's depth
     value (it cleared above every depth the game draws, so nothing passed
     GREATER), and a draw behaves the same on any implementation.
   - Textures: de-twiddled from texture memory to RGBA8, reading the PVR's
     16-bit texels little-endian, with the KOS mip chain; each level goes
     through a linear staging surface and GX2CopySurface does the tiling.
     Cached by address, re-uploaded when pvr_txr_load bumps the block's
     generation.
   - Vertex colours (ARGB words, big-endian here) are fetched 8-in-32
     swapped, so the shaders see b, g, r, a as on the little-endian host.
   - Uniform blocks are written little-endian, as the GPU reads them, from a
     ring reset each frame; GX2_SHADER_MODE_UNIFORM_BLOCK is set after every
     context switch (the console needs it; Cemu forgives forgetting it).
   - Modifier volumes are the PVR's cheap shadow in stencil, as in
     render_gl.c (see there).
   - Every frame ends with GX2DrawDone, so the CPU may reuse vertex, uniform
     and staging memory at once. Simple; overlap is an optimisation for
     later if profiling asks for it. */
#include <coreinit/cache.h>
#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/draw.h>
#include <gx2/enum.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <whb/gfx.h>

#include "plat.h"
#include "ta.h"

void plat_shutdown(void);

#define FB_W 640
#define FB_H 480
#define MAX_FRAME_VERTS (3 * 131072)
#define MAX_FRAME_MOD_VERTS (3 * 8192)
#define UNIFORM_RING (2 * 1024 * 1024)

extern const uint8_t dku_gsh_main[], dku_gsh_blit[], dku_gsh_mod[], dku_gsh_dark[];

static WHBGfxShaderGroup g_main, g_blit, g_mod, g_dark;
static uint32_t loc_hdr, loc_frame, loc_tex, loc_fb;
static GX2ContextState *ctx;
static GX2ColorBuffer cb;
static GX2DepthBuffer db;
static GX2Texture fb_tex;
static GX2Sampler fb_sampler;
static ta_vertex_t *vbuf;
static ta_mod_vertex_t *mod_vbuf;
static float *blit_vbuf;
static uint8_t *uring;
static size_t uring_used;
static bool uring_full;

/* ---- helpers ---- */

static void *gpu_alloc(size_t align, size_t size) {
    void *p = memalign(align, size);
    if(!p) {
        fprintf(stderr, "DKU GX2: out of memory (%u bytes)\n", (unsigned)size);
        abort();
    }
    return p;
}

/* Little-endian 32-bit words for a uniform block, from the ring. */
static void *uniforms(const void *data, size_t size) {
    size_t at = (uring_used + GX2_UNIFORM_BLOCK_ALIGNMENT - 1) & ~(size_t)(GX2_UNIFORM_BLOCK_ALIGNMENT - 1);
    if(at + size > UNIFORM_RING) {
        uring_full = true;
        at = 0;   /* overwrite: wrong colours somewhere, not a crash */
    }
    uint32_t *dst = (uint32_t *)(uring + at);
    const uint32_t *src = data;
    for(size_t i = 0; i < size / 4; i++) dst[i] = __builtin_bswap32(src[i]);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU | GX2_INVALIDATE_MODE_UNIFORM_BLOCK, dst, size);
    uring_used = at + size;
    return dst;
}

static uint32_t block_location(const GX2PixelShader *ps, const char *name) {
    for(uint32_t i = 0; i < ps->uniformBlockCount; i++)
        if(!strcmp(ps->uniformBlocks[i].name, name)) return ps->uniformBlocks[i].offset;
    fprintf(stderr, "DKU GX2: no uniform block %s\n", name);
    return 0;
}

static uint32_t sampler_location(const GX2PixelShader *ps, const char *name) {
    for(uint32_t i = 0; i < ps->samplerVarCount; i++)
        if(!strcmp(ps->samplerVars[i].name, name)) return ps->samplerVars[i].location;
    fprintf(stderr, "DKU GX2: no sampler %s\n", name);
    return 0;
}

static void load_group(WHBGfxShaderGroup *g, const uint8_t *gsh, const char *what) {
    if(!WHBGfxLoadGFDShaderGroup(g, 0, gsh)) {
        fprintf(stderr, "DKU GX2: cannot load shader %s\n", what);
        abort();
    }
}

/* An attribute a shader may have optimised away (the darken pass's uv). */
static void attrib(WHBGfxShaderGroup *g, const char *name, uint32_t offset, GX2AttribFormat fmt, bool swap32) {
    if(!WHBGfxInitShaderAttribute(g, name, 0, offset, fmt)) return;
    if(swap32) g->attributes[g->numAttributes - 1].endianSwap = GX2_ENDIAN_SWAP_8_IN_32;
}

static void use_group(const WHBGfxShaderGroup *g) {
    GX2SetFetchShader(&g->fetchShader);
    GX2SetVertexShader(g->vertexShader);
    GX2SetPixelShader(g->pixelShader);
}

/* ---- textures ---- */

typedef struct {
    const void *base;
    int fmt, w, h;
    bool mip;
    uint32_t gen;
    GX2Texture *tex;
} tex_entry_t;

#define TEX_SLOTS 2048
static tex_entry_t texes[TEX_SLOTS];

/* Staging surfaces waiting for GX2DrawDone before they can be freed. */
static void *staging[1024];
static int nstaging;

static uint32_t spread(uint32_t v) {
    uint32_t r = 0;
    for(int b = 0; v >> b; b++) r |= ((v >> b) & 1) << (2 * b);
    return r;
}

/* One PVR texel (little-endian 16 bits) as RGBA8, bytes r, g, b, a. */
static uint32_t texel(const uint8_t *p, int fmt) {
    uint32_t v = p[0] | (uint32_t)p[1] << 8, r, g, b, a;
    switch(fmt & PVR_TXRFMT_FMT_MASK) {
        case PVR_TXRFMT_RGB565:
            r = (v >> 11) & 31;
            g = (v >> 5) & 63;
            b = v & 31;
            return (r << 3 | r >> 2) << 24 | (g << 2 | g >> 4) << 16 | (b << 3 | b >> 2) << 8 | 255;
        case PVR_TXRFMT_ARGB4444:
            a = v >> 12;
            r = (v >> 8) & 15;
            g = (v >> 4) & 15;
            b = v & 15;
            return r * 17 << 24 | g * 17 << 16 | b * 17 << 8 | a * 17;
        default:   /* ARGB1555; YUV/VQ unused */
            a = v >> 15;
            r = (v >> 10) & 31;
            g = (v >> 5) & 31;
            b = v & 31;
            return (r << 3 | r >> 2) << 24 | (g << 3 | g >> 2) << 16 | (b << 3 | b >> 2) << 8 | (a ? 255 : 0);
    }
}

/* A w x h level from PVR order (twiddled: y in the low Morton bit; a
   rectangle is square blocks along its long side) into texture level lv. */
static void upload_level(GX2Texture *t, int lv, const uint8_t *in, int w, int h, int fmt, bool twiddled) {
    if(nstaging == (int)(sizeof(staging) / sizeof(staging[0]))) return;
    GX2Surface s;
    memset(&s, 0, sizeof(s));
    s.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    s.width = (uint32_t)w;
    s.height = (uint32_t)h;
    s.depth = 1;
    s.mipLevels = 1;
    s.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    s.aa = GX2_AA_MODE1X;
    s.use = GX2_SURFACE_USE_TEXTURE;
    s.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
    GX2CalcSurfaceSizeAndAlignment(&s);
    uint32_t *px = gpu_alloc(s.alignment, s.imageSize);
    s.image = px;
    int edge = w < h ? w : h, mask = edge - 1;
    for(int y = 0; y < h; y++)
        for(int x = 0; x < w; x++) {
            uint32_t i = twiddled ? (uint32_t)((x / edge) + (y / edge)) * edge * edge + (spread(x & mask) << 1) +
                                        spread(y & mask)
                                  : (uint32_t)(y * w + x);
            px[y * s.pitch + x] = texel(in + 2 * i, fmt);
        }
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, px, s.imageSize);
    GX2CopySurface(&s, 0, 0, &t->surface, (uint32_t)lv, 0);
    staging[nstaging++] = px;
}

static GX2Texture *new_texture(int w, int h, int levels) {
    GX2Texture *t = calloc(1, sizeof(*t));
    t->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    t->surface.width = (uint32_t)w;
    t->surface.height = (uint32_t)h;
    t->surface.depth = 1;
    t->surface.mipLevels = (uint32_t)levels;
    t->surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    t->surface.aa = GX2_AA_MODE1X;
    t->surface.use = GX2_SURFACE_USE_TEXTURE;
    t->surface.tileMode = GX2_TILE_MODE_DEFAULT;
    t->viewNumMips = (uint32_t)levels;
    t->viewNumSlices = 1;
    t->compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    GX2CalcSurfaceSizeAndAlignment(&t->surface);
    GX2InitTextureRegs(t);
    t->surface.image = gpu_alloc(t->surface.alignment, t->surface.imageSize);
    if(levels > 1) t->surface.mipmaps = gpu_alloc(t->surface.alignment, t->surface.mipmapSize);
    return t;
}

static void upload(tex_entry_t *e, const pvr_poly_cxt_t *c, const pvr_shim_block_t *blk) {
    int fmt = c->txr.format;
    bool twiddled = !(fmt & PVR_TXRFMT_NONTWIDDLED);
    const uint8_t *src = c->txr.base;
    const uint8_t *end = (const uint8_t *)blk->base + blk->size;
    int levels = 1;
    if(c->txr.mipmap)
        for(int s = c->txr.width; s > 1; s >>= 1) levels++;
    if(!e->tex) e->tex = new_texture(c->txr.width, c->txr.height, levels);
    GX2Texture *t = e->tex;
    if(c->txr.mipmap) {
        /* KOS mip chain: three padding texels, then 1x1, 2x2, ... */
        size_t off = 6;
        for(int lv = levels - 1, s = 1; lv >= 0; lv--, s <<= 1) {
            if(src + off + (size_t)s * s * 2 > end) break;
            upload_level(t, lv, src + off, s, s, fmt, twiddled);
            off += (size_t)s * s * 2;
        }
    }
    else if(src + (size_t)c->txr.width * c->txr.height * 2 <= end) {
        upload_level(t, 0, src, c->txr.width, c->txr.height, fmt, twiddled);
    }
    GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE, t->surface.image, t->surface.imageSize);
    if(t->surface.mipmaps) GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE, t->surface.mipmaps, t->surface.mipmapSize);
    e->gen = blk->generation;
}

static GX2Texture *texture_for(const pvr_poly_cxt_t *c) {
    const pvr_shim_block_t *blk = pvr_shim_block(c->txr.base);
    if(!blk) return NULL;
    uintptr_t key = (uintptr_t)c->txr.base;
    uint32_t h = (uint32_t)((key >> 5) * 2654435761u) % TEX_SLOTS;
    for(int probe = 0; probe < TEX_SLOTS; probe++, h = (h + 1) % TEX_SLOTS) {
        tex_entry_t *e = &texes[h];
        if(!e->base) {
            *e = (tex_entry_t){c->txr.base, c->txr.format, c->txr.width, c->txr.height, c->txr.mipmap, 0, NULL};
            upload(e, c, blk);
            return e->tex;
        }
        if(e->base == c->txr.base && e->fmt == c->txr.format && e->w == c->txr.width &&
           e->h == c->txr.height && e->mip == (bool)c->txr.mipmap) {
            if(e->gen != blk->generation) upload(e, c, blk);
            return e->tex;
        }
    }
    return NULL;
}

/* ---- state ---- */

static GX2BlendMode gx2_blend(int f, bool src) {
    switch(f) {
        case PVR_BLEND_ZERO: return GX2_BLEND_MODE_ZERO;
        case PVR_BLEND_ONE: return GX2_BLEND_MODE_ONE;
        case PVR_BLEND_DESTCOLOR: return src ? GX2_BLEND_MODE_DST_COLOR : GX2_BLEND_MODE_SRC_COLOR;   /* "other" */
        case PVR_BLEND_INVDESTCOLOR: return src ? GX2_BLEND_MODE_INV_DST_COLOR : GX2_BLEND_MODE_INV_SRC_COLOR;
        case PVR_BLEND_SRCALPHA: return GX2_BLEND_MODE_SRC_ALPHA;
        case PVR_BLEND_INVSRCALPHA: return GX2_BLEND_MODE_INV_SRC_ALPHA;
        case PVR_BLEND_DESTALPHA: return GX2_BLEND_MODE_DST_ALPHA;
        default: return GX2_BLEND_MODE_INV_DST_ALPHA;
    }
}

static void blend(bool on, GX2BlendMode src, GX2BlendMode dst) {
    GX2SetBlendControl(GX2_RENDER_TARGET_0, src, dst, GX2_BLEND_COMBINE_MODE_ADD, TRUE, src, dst,
                       GX2_BLEND_COMBINE_MODE_ADD);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, on ? 0xff : 0, FALSE, TRUE);
}

/* Depth and stencil with the same stencil settings for both faces. */
static void depth_stencil(bool test, bool write, GX2CompareFunction cmp, GX2CompareFunction sfunc,
                          GX2StencilFunction zpass, uint8_t mask, uint8_t wmask, uint8_t ref) {
    GX2SetDepthStencilControl(test, write, cmp, TRUE, TRUE, sfunc, zpass, GX2_STENCIL_FUNCTION_KEEP,
                              GX2_STENCIL_FUNCTION_KEEP, sfunc, zpass, GX2_STENCIL_FUNCTION_KEEP,
                              GX2_STENCIL_FUNCTION_KEEP);
    GX2SetStencilMask(mask, wmask, ref, mask, wmask, ref);
}

static void apply(const pvr_poly_cxt_t *c, int list, const pvr_shim_state_t *st) {
    GX2Texture *t = c->txr.enable != 0 && c->txr.base ? texture_for(c) : NULL;
    float hdr[8] = {
        t ? 1.0f : 0.0f,
        (float)c->txr.env,
        c->gen.specular == PVR_SPECULAR_ENABLE ? 1.0f : 0.0f,
        (float)c->gen.fog_type,
        c->gen.alpha != 0 ? 1.0f : 0.0f,
        c->txr.alpha != 0 ? 1.0f : 0.0f,   /* the shim keeps 1 = texture alpha on */
        list == TA_PT ? (st->pt_alpha_ref + 0.5f) / 255.0f : -1.0f,
        0.0f,
    };
    GX2SetPixelUniformBlock(loc_hdr, sizeof(hdr), uniforms(hdr, sizeof(hdr)));
    if(t) {
        bool bilinear = c->txr.filter != PVR_FILTER_NEAREST;
        bool cu = c->txr.uv_clamp & PVR_UVCLAMP_U, cv = c->txr.uv_clamp & PVR_UVCLAMP_V;
        bool fu = c->txr.uv_flip & 2, fv = c->txr.uv_flip & 1;
        GX2Sampler s;
        GX2InitSampler(&s, GX2_TEX_CLAMP_MODE_WRAP, bilinear ? GX2_TEX_XY_FILTER_MODE_LINEAR : GX2_TEX_XY_FILTER_MODE_POINT);
        GX2InitSamplerClamping(&s, cu ? GX2_TEX_CLAMP_MODE_CLAMP : fu ? GX2_TEX_CLAMP_MODE_MIRROR : GX2_TEX_CLAMP_MODE_WRAP,
                               cv ? GX2_TEX_CLAMP_MODE_CLAMP : fv ? GX2_TEX_CLAMP_MODE_MIRROR : GX2_TEX_CLAMP_MODE_WRAP,
                               GX2_TEX_CLAMP_MODE_WRAP);
        GX2InitSamplerZMFilter(&s, GX2_TEX_Z_FILTER_MODE_NONE,
                               c->txr.mipmap ? GX2_TEX_MIP_FILTER_MODE_POINT : GX2_TEX_MIP_FILTER_MODE_NONE);
        /* D-adjust in quarters (PVR_MIPBIAS_*): 4 is the hardware's own pick. */
        int bias = c->txr.mipmap_bias ? c->txr.mipmap_bias : PVR_MIPBIAS_NORMAL;
        GX2InitSamplerLOD(&s, 0.0f, c->txr.mipmap ? 16.0f : 0.0f, log2f(bias / 4.0f));
        GX2SetPixelTexture(t, loc_tex);
        GX2SetPixelSampler(&s, loc_tex);
    }
    if(list == TA_TR) blend(true, gx2_blend(c->blend.src, true), gx2_blend(c->blend.dst, false));
    else blend(false, GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ZERO);
    GX2CompareFunction cmp = (GX2CompareFunction)(c->depth.comparison & 7);
    bool zwrite = c->depth.write == PVR_DEPTHWRITE_ENABLE;
    /* Bit 7: whether the surface left in this pixel takes the shadow. */
    if(list != TA_TR)
        depth_stencil(true, zwrite, cmp, GX2_COMPARE_FUNC_ALWAYS, GX2_STENCIL_FUNCTION_REPLACE, 0xff, 0x80,
                      c->fmt.modifier ? 0x80 : 0);
    else
        depth_stencil(true, zwrite, cmp, GX2_COMPARE_FUNC_ALWAYS, GX2_STENCIL_FUNCTION_KEEP, 0xff, 0, 0);
    /* Screen-space winding; y points down on the PVR and up in clip space. */
    if(c->gen.culling == PVR_CULLING_CW || c->gen.culling == PVR_CULLING_CCW)
        GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, c->gen.culling == PVR_CULLING_CW, c->gen.culling == PVR_CULLING_CCW);
    else
        GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
}

/* ---- frame ---- */

void backend_init(void) {
    plat_init();
    WHBGfxInit();
    ctx = gpu_alloc(GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState));
    GX2SetupContextStateEx(ctx, TRUE);

    load_group(&g_main, dku_gsh_main, "main");
    attrib(&g_main, "a_pos", offsetof(ta_vertex_t, x), GX2_ATTRIB_FORMAT_FLOAT_32_32_32, false);
    attrib(&g_main, "a_uv", offsetof(ta_vertex_t, u), GX2_ATTRIB_FORMAT_FLOAT_32_32, false);
    attrib(&g_main, "a_col", offsetof(ta_vertex_t, argb), GX2_ATTRIB_FORMAT_UNORM_8_8_8_8, true);
    attrib(&g_main, "a_ofs", offsetof(ta_vertex_t, oargb), GX2_ATTRIB_FORMAT_UNORM_8_8_8_8, true);
    WHBGfxInitFetchShader(&g_main);
    loc_hdr = block_location(g_main.pixelShader, "HdrBlock");
    loc_frame = block_location(g_main.pixelShader, "FrameBlock");
    loc_tex = sampler_location(g_main.pixelShader, "u_tex");

    load_group(&g_blit, dku_gsh_blit, "blit");
    attrib(&g_blit, "a_pos", 0, GX2_ATTRIB_FORMAT_FLOAT_32_32, false);
    attrib(&g_blit, "a_uv", 8, GX2_ATTRIB_FORMAT_FLOAT_32_32, false);
    WHBGfxInitFetchShader(&g_blit);
    loc_fb = sampler_location(g_blit.pixelShader, "u_fb");

    load_group(&g_mod, dku_gsh_mod, "mod");
    attrib(&g_mod, "a_pos", 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32, false);
    WHBGfxInitFetchShader(&g_mod);

    load_group(&g_dark, dku_gsh_dark, "dark");
    attrib(&g_dark, "a_pos", 0, GX2_ATTRIB_FORMAT_FLOAT_32_32, false);
    attrib(&g_dark, "a_uv", 8, GX2_ATTRIB_FORMAT_FLOAT_32_32, false);
    WHBGfxInitFetchShader(&g_dark);

    /* The 640x480 frame, also read as a texture by the blit. */
    memset(&cb, 0, sizeof(cb));
    cb.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    cb.surface.width = FB_W;
    cb.surface.height = FB_H;
    cb.surface.depth = 1;
    cb.surface.mipLevels = 1;
    cb.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    cb.surface.aa = GX2_AA_MODE1X;
    cb.surface.use = GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER;
    cb.surface.tileMode = GX2_TILE_MODE_DEFAULT;
    cb.viewNumSlices = 1;
    GX2CalcSurfaceSizeAndAlignment(&cb.surface);
    GX2InitColorBufferRegs(&cb);
    cb.surface.image = gpu_alloc(cb.surface.alignment, cb.surface.imageSize);

    memset(&db, 0, sizeof(db));
    db.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    db.surface.width = FB_W;
    db.surface.height = FB_H;
    db.surface.depth = 1;
    db.surface.mipLevels = 1;
    db.surface.format = GX2_SURFACE_FORMAT_FLOAT_D24_S8;
    db.surface.aa = GX2_AA_MODE1X;
    db.surface.use = GX2_SURFACE_USE_DEPTH_BUFFER;
    db.surface.tileMode = GX2_TILE_MODE_DEFAULT;
    db.viewNumSlices = 1;
    db.depthClear = 0.0f;
    db.stencilClear = 0;
    GX2CalcSurfaceSizeAndAlignment(&db.surface);
    GX2InitDepthBufferRegs(&db);
    db.surface.image = gpu_alloc(db.surface.alignment, db.surface.imageSize);

    memset(&fb_tex, 0, sizeof(fb_tex));
    fb_tex.surface = cb.surface;
    fb_tex.surface.use = GX2_SURFACE_USE_TEXTURE;
    fb_tex.viewNumMips = 1;
    fb_tex.viewNumSlices = 1;
    fb_tex.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    GX2InitTextureRegs(&fb_tex);
    GX2InitSampler(&fb_sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);

    vbuf = gpu_alloc(GX2_VERTEX_BUFFER_ALIGNMENT, MAX_FRAME_VERTS * sizeof(ta_vertex_t));
    mod_vbuf = gpu_alloc(GX2_VERTEX_BUFFER_ALIGNMENT, MAX_FRAME_MOD_VERTS * sizeof(ta_mod_vertex_t));
    uring = gpu_alloc(GX2_UNIFORM_BLOCK_ALIGNMENT, UNIFORM_RING);
    /* Full-screen strip: x, y, u, v. Row 0 of the frame is its top. */
    static const float quad[16] = {-1, -1, 0, 1, 1, -1, 1, 1, -1, 1, 0, 0, 1, 1, 1, 0};
    blit_vbuf = gpu_alloc(GX2_VERTEX_BUFFER_ALIGNMENT, sizeof(quad));
    memcpy(blit_vbuf, quad, sizeof(quad));
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, blit_vbuf, sizeof(quad));
    printf("DKU GX2: up, hdr block %u, frame block %u, tex %u\n", (unsigned)loc_hdr, (unsigned)loc_frame,
           (unsigned)loc_tex);
}

/* Every present ended in GX2DrawDone, and by now ProcUI has taken the
   foreground away, so a GX2DrawDone here could wait forever. */
void backend_shutdown(void) {
    printf("DKU EXIT: graphics\n");
    WHBGfxShutdown();
    plat_shutdown();
}

static void set_frame_state(void) {
    GX2SetContextState(ctx);
    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);
    GX2SetColorBuffer(&cb, GX2_RENDER_TARGET_0);
    GX2SetDepthBuffer(&db);
    GX2SetViewport(0, 0, FB_W, FB_H, 0.0f, 1.0f);
    GX2SetScissor(0, 0, FB_W, FB_H);
    GX2SetRasterizerClipControlEx(TRUE, TRUE, TRUE);
}

/* The modifier volumes, after OP and PT (render_gl.c draw_shadows). */
static void draw_shadows(const ta_frame_t *f, const pvr_shim_state_t *st) {
    if(!st->shadow_enabled || !f->nvolumes) return;
    int n = f->nmod_verts < MAX_FRAME_MOD_VERTS ? f->nmod_verts : MAX_FRAME_MOD_VERTS;
    memcpy(mod_vbuf, f->mod_verts, (size_t)n * sizeof(ta_mod_vertex_t));
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, mod_vbuf, (size_t)n * sizeof(ta_mod_vertex_t));
    use_group(&g_mod);
    GX2SetAttribBuffer(0, (uint32_t)n * sizeof(ta_mod_vertex_t), sizeof(ta_mod_vertex_t), mod_vbuf);
    GX2SetScissor(0, 0, FB_W, FB_H);   /* r_modvol cuts volumes to their view */
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
    blend(false, GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ZERO);
    GX2SetTargetChannelMasks((GX2ChannelMask)0, 0, 0, 0, 0, 0, 0, 0);
    for(int i = 0; i < f->nvolumes; i++) {
        const ta_volume_t *v = &f->volumes[i];
        if(v->first + v->count > n) break;
        /* Faces nearer than the surface (larger 1/w) flip bit 0. */
        depth_stencil(true, false, GX2_COMPARE_FUNC_GREATER, GX2_COMPARE_FUNC_ALWAYS, GX2_STENCIL_FUNCTION_INV,
                      0xff, 0x01, 0);
        GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, (uint32_t)v->count, (uint32_t)v->first, 1);
        /* Inside this volume (bit 0): set bit 1; then clear bit 0. */
        depth_stencil(false, false, GX2_COMPARE_FUNC_ALWAYS, GX2_COMPARE_FUNC_EQUAL, GX2_STENCIL_FUNCTION_REPLACE,
                      0x01, 0x02, 0x03);
        GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, (uint32_t)v->count, (uint32_t)v->first, 1);
        depth_stencil(false, false, GX2_COMPARE_FUNC_ALWAYS, GX2_COMPARE_FUNC_ALWAYS, GX2_STENCIL_FUNCTION_ZERO,
                      0xff, 0x01, 0);
        GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, (uint32_t)v->count, (uint32_t)v->first, 1);
    }
    GX2SetTargetChannelMasks(GX2_CHANNEL_MASK_RGBA, 0, 0, 0, 0, 0, 0, 0);
    /* Darken shadowed surfaces that take modifiers. */
    use_group(&g_dark);
    GX2SetAttribBuffer(0, 16 * sizeof(float), 4 * sizeof(float), blit_vbuf);
    depth_stencil(false, false, GX2_COMPARE_FUNC_ALWAYS, GX2_COMPARE_FUNC_EQUAL, GX2_STENCIL_FUNCTION_KEEP, 0x82, 0,
                  0x82);
    GX2SetBlendConstantColor(st->shadow_scale, st->shadow_scale, st->shadow_scale, 1.0f);
    blend(true, GX2_BLEND_MODE_ZERO, GX2_BLEND_MODE_BLEND_FACTOR);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLE_STRIP, 4, 0, 1);
}

void backend_draw(const ta_frame_t *f, const pvr_shim_state_t *st) {
    WHBGfxBeginRender();
    set_frame_state();
    GX2ClearColor(&cb, st->bg[0], st->bg[1], st->bg[2], 1.0f);
    GX2ClearDepthStencilEx(&db, 0.0f, 0, GX2_CLEAR_FLAGS_BOTH);
    set_frame_state();   /* the clears change state */
    /* Depth 0 and stencil 0 everywhere (see the top of the file). */
    use_group(&g_dark);
    GX2SetAttribBuffer(0, 16 * sizeof(float), 4 * sizeof(float), blit_vbuf);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
    blend(false, GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ZERO);
    GX2SetTargetChannelMasks((GX2ChannelMask)0, 0, 0, 0, 0, 0, 0, 0);
    depth_stencil(true, true, GX2_COMPARE_FUNC_ALWAYS, GX2_COMPARE_FUNC_ALWAYS, GX2_STENCIL_FUNCTION_REPLACE, 0xff,
                  0xff, 0);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLE_STRIP, 4, 0, 1);
    GX2SetTargetChannelMasks(GX2_CHANNEL_MASK_RGBA, 0, 0, 0, 0, 0, 0, 0);

    int n = f->nverts < MAX_FRAME_VERTS ? f->nverts : MAX_FRAME_VERTS;
    memcpy(vbuf, f->verts, (size_t)n * sizeof(ta_vertex_t));
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, vbuf, (size_t)n * sizeof(ta_vertex_t));

    struct {
        float fog_color[4], fog_far[4], fog_table[33 * 4];
    } fr;
    memset(&fr, 0, sizeof(fr));
    fr.fog_color[0] = st->fog_color[1];
    fr.fog_color[1] = st->fog_color[2];
    fr.fog_color[2] = st->fog_color[3];
    fr.fog_color[3] = st->fog_color[0];
    fr.fog_far[0] = st->fog_far_depth;
    memcpy(fr.fog_table, st->fog_table, sizeof(st->fog_table));
    void *frame_block = uniforms(&fr, sizeof(fr));

    static const int order[TA_LISTS] = {TA_OP, TA_PT, TA_TR};
    for(int li = 0; li < TA_LISTS; li++) {
        int l = order[li];
        int last_hdr = -1;
        if(l == TA_TR) draw_shadows(f, st);
        use_group(&g_main);
        GX2SetPixelUniformBlock(loc_frame, sizeof(fr), frame_block);
        GX2SetAttribBuffer(0, (uint32_t)n * sizeof(ta_vertex_t), sizeof(ta_vertex_t), vbuf);
        for(int i = 0; i < f->nbatches[l]; i++) {
            const ta_batch_t *b = &f->batches[l][i];
            if(b->first + b->count > n) break;
            if(b->hdr != last_hdr) {
                apply(&f->hdrs[b->hdr].cxt, l, st);
                last_hdr = b->hdr;
            }
            /* GX2's scissor counts rows down from the top, as the PVR does. */
            int y0 = b->clip_y0 < 0 ? 0 : b->clip_y0, y1 = b->clip_y1 > FB_H ? FB_H : b->clip_y1;
            if(y1 <= y0) continue;
            GX2SetScissor(0, (uint32_t)y0, FB_W, (uint32_t)(y1 - y0));
            GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, (uint32_t)b->count, (uint32_t)b->first, 1);
        }
    }
}

/* The frame, 4:3, centred on a target of w x h. */
static void blit_to(uint32_t w, uint32_t h) {
    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);
    uint32_t vh = h, vw = h * 4 / 3;
    if(vw > w) {
        vw = w;
        vh = w * 3 / 4;
    }
    GX2SetViewport((float)((w - vw) / 2), (float)((h - vh) / 2), (float)vw, (float)vh, 0.0f, 1.0f);
    GX2SetScissor(0, 0, w, h);
    GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0, FALSE, TRUE);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
    use_group(&g_blit);
    GX2SetAttribBuffer(0, 16 * sizeof(float), 4 * sizeof(float), blit_vbuf);
    GX2SetPixelTexture(&fb_tex, loc_fb);
    GX2SetPixelSampler(&fb_sampler, loc_fb);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLE_STRIP, 4, 0, 1);
}

void backend_present(void) {
    GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE, cb.surface.image,
                  cb.surface.imageSize);
    WHBGfxBeginRenderTV();
    GX2ColorBuffer *tv = WHBGfxGetTVColourBuffer();
    WHBGfxClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    blit_to(tv->surface.width, tv->surface.height);
    WHBGfxFinishRenderTV();
    WHBGfxBeginRenderDRC();
    GX2ColorBuffer *drc = WHBGfxGetDRCColourBuffer();
    WHBGfxClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    blit_to(drc->surface.width, drc->surface.height);
    WHBGfxFinishRenderDRC();
    WHBGfxFinishRender();
    GX2DrawDone();
    for(int i = 0; i < nstaging; i++) free(staging[i]);
    nstaging = 0;
    uring_used = 0;
    if(uring_full) {
        static int warned;
        if(warned++ < 4) fprintf(stderr, "DKU GX2: uniform ring overflowed\n");
        uring_full = false;
    }
    plat_frame_done();
}
