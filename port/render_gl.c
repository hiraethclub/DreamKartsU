/* DKU: the OpenGL 3.3 core backend for the host build. It draws the frame
   the software TA decoded (ta.h) into a 640x480 framebuffer, then scales
   that to the window.

   - Lists draw in the PVR's order, OP, PT, TR, each in submission order
     (the game sets presort mode, so TR needs no sorting).
   - Depth is the PVR's: 1/w, compared as the header says. The fragment
     shader writes z / (1 + z) into a float depth buffer, which keeps
     the order and the precision far away; cleared to 0 (infinitely far).
   - Textures come from texture memory (pvr_shim_block): de-twiddled, with
     the KOS mip chain, cached by address and re-uploaded when
     pvr_txr_load bumps the block's generation.
   - Fog is the PVR's table (entry k at far / 2^(k/16)) or the offset
     colour's alpha (vertex fog), mixed in after texturing and specular.
   - Modifier volumes (shadows) are not drawn yet (PORT_PLAN.md M2).

   DKU_SHOTS="f1 f2 ..." saves the frame as PNG at those frame numbers to
   DKU_CAPTURE_DIR (default captures/host), named DKU_SHOT_TAG-fNNNNN.png. */
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#include "plat.h"
#include "ta.h"

void plat_window_size(int *w, int *h);
void plat_swap(void);

#define FB_W 640
#define FB_H 480

static GLuint prog, vao, vbo, fbo, fb_color, fb_depth, white_tex;
static GLint u_tex_on, u_env, u_spec, u_fog_mode, u_use_alpha, u_tex_alpha, u_alpha_ref, u_fog_color,
    u_fog_table, u_fog_far, u_tex;
static unsigned long frame_no;
static int shots[64], nshots;
static size_t vbo_cap;

/* ---- shaders ---- */

static const char *vs_src =
    "#version 330 core\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "layout(location=2) in vec4 a_col;\n"   /* bytes b, g, r, a (ARGB word, little-endian) */
    "layout(location=3) in vec4 a_ofs;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_col, v_ofs;\n"
    "noperspective out float v_z;\n"
    "void main() {\n"
    "    float z = max(a_pos.z, 1e-7);\n"
    "    float w = 1.0 / z;\n"
    "    gl_Position = vec4((a_pos.x / 320.0 - 1.0) * w, (1.0 - a_pos.y / 240.0) * w, 0.0, w);\n"
    "    v_uv = a_uv;\n"
    "    v_col = a_col.bgra;\n"
    "    v_ofs = a_ofs.bgra;\n"
    "    v_z = z;\n"
    "}\n";

static const char *fs_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "in vec4 v_col, v_ofs;\n"
    "noperspective in float v_z;\n"
    "out vec4 o_col;\n"
    "uniform sampler2D u_tex;\n"
    "uniform bool u_tex_on, u_spec, u_use_alpha, u_tex_alpha;\n"
    "uniform int u_env, u_fog_mode;\n"   /* PVR_TXRENV_*, PVR_FOG_* */
    "uniform float u_alpha_ref;\n"       /* < 0: no alpha test */
    "uniform vec4 u_fog_color;\n"        /* r, g, b, a */
    "uniform float u_fog_table[129];\n"
    "uniform float u_fog_far;\n"
    "void main() {\n"
    "    vec4 c = v_col;\n"
    "    if(!u_use_alpha) c.a = 1.0;\n"
    "    if(u_tex_on) {\n"
    "        vec4 t = texture(u_tex, v_uv);\n"
    "        if(!u_tex_alpha) t.a = 1.0;\n"
    "        if(u_env == 0) c = t;\n"
    "        else if(u_env == 1) c = vec4(t.rgb * c.rgb, t.a);\n"
    "        else if(u_env == 2) c = vec4(mix(c.rgb, t.rgb, t.a), c.a);\n"
    "        else c = t * c;\n"
    "    }\n"
    "    if(u_spec) c.rgb += v_ofs.rgb;\n"
    "    if(u_fog_mode == 0 || u_fog_mode == 3) {\n"
    "        float k = clamp(16.0 * log2(max(u_fog_far * v_z, 1e-30)), 0.0, 127.999);\n"
    "        int i = int(k);\n"
    "        float f = mix(u_fog_table[i], u_fog_table[i + 1], k - float(i));\n"
    "        c.rgb = mix(c.rgb, u_fog_color.rgb, f * u_fog_color.a);\n"
    "    } else if(u_fog_mode == 1) {\n"
    "        c.rgb = mix(c.rgb, u_fog_color.rgb, v_ofs.a);\n"
    "    }\n"
    "    if(c.a < u_alpha_ref) discard;\n"
    "    o_col = clamp(c, 0.0, 1.0);\n"
    "    gl_FragDepth = v_z / (1.0 + v_z);\n"
    "}\n";

static const char *blit_vs =
    "#version 330 core\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 p = vec2(gl_VertexID & 1, gl_VertexID >> 1);\n"
    "    v_uv = p;\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";
static const char *blit_fs =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 o;\n"
    "uniform sampler2D u_fb;\n"
    "void main() { o = texture(u_fb, v_uv); }\n";
static GLuint blit_prog;

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "DKU GL: shader: %s\n", log);
        exit(1);
    }
    return s;
}

static GLuint link_prog(const char *vs, const char *fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if(!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "DKU GL: link: %s\n", log);
        exit(1);
    }
    return p;
}

/* ---- textures ---- */

typedef struct {
    const void *base;
    int fmt, w, h;
    bool mip;
    uint32_t gen;
    GLuint tex;
} tex_entry_t;

#define TEX_SLOTS 2048
static tex_entry_t texes[TEX_SLOTS];

static uint32_t spread(uint32_t v) {
    uint32_t r = 0;
    for(int b = 0; v >> b; b++) r |= ((v >> b) & 1) << (2 * b);
    return r;
}

/* Linear texels of a w x h level from PVR order (twiddled: y in the low
   Morton bit; a rectangle is square blocks along its long side). */
static void detwiddle(uint16_t *out, const uint16_t *in, int w, int h, bool twiddled) {
    if(!twiddled) {
        memcpy(out, in, (size_t)w * h * 2);
        return;
    }
    int edge = w < h ? w : h, mask = edge - 1;
    for(int y = 0; y < h; y++)
        for(int x = 0; x < w; x++) {
            uint32_t block = (uint32_t)((x / edge) + (y / edge)) * edge * edge;
            out[y * w + x] = in[block + (spread(x & mask) << 1) + spread(y & mask)];
        }
}

static void upload(tex_entry_t *e, const pvr_poly_cxt_t *c, const pvr_shim_block_t *blk) {
    int fmt = c->txr.format;
    bool twiddled = !(fmt & PVR_TXRFMT_NONTWIDDLED);
    GLenum gfmt = GL_BGRA, gtype;
    switch(fmt & PVR_TXRFMT_FMT_MASK) {
        case PVR_TXRFMT_RGB565:
            gfmt = GL_RGB;
            gtype = GL_UNSIGNED_SHORT_5_6_5;
            break;
        case PVR_TXRFMT_ARGB4444: gtype = GL_UNSIGNED_SHORT_4_4_4_4_REV; break;
        default: gtype = GL_UNSIGNED_SHORT_1_5_5_5_REV; break;   /* ARGB1555; YUV/VQ unused */
    }
    const uint8_t *src = c->txr.base;
    const uint8_t *end = (const uint8_t *)blk->base + blk->size;
    uint16_t *buf = malloc((size_t)c->txr.width * c->txr.height * 2);
    glBindTexture(GL_TEXTURE_2D, e->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    if(c->txr.mipmap) {
        /* KOS mip chain: three padding texels, then 1x1, 2x2, ... */
        int n = 0;
        for(int s = c->txr.width; s > 1; s >>= 1) n++;
        size_t off = 6;
        for(int lv = n, s = 1; lv >= 0; lv--, s <<= 1) {
            if(src + off + (size_t)s * s * 2 > end) break;
            detwiddle(buf, (const uint16_t *)(src + off), s, s, twiddled);
            glTexImage2D(GL_TEXTURE_2D, lv, gfmt == GL_RGB ? GL_RGB : GL_RGBA, s, s, 0, gfmt, gtype, buf);
            off += (size_t)s * s * 2;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, n);
    }
    else {
        size_t need = (size_t)c->txr.width * c->txr.height * 2;
        if(src + need <= end) {
            detwiddle(buf, (const uint16_t *)src, c->txr.width, c->txr.height, twiddled);
            glTexImage2D(GL_TEXTURE_2D, 0, gfmt == GL_RGB ? GL_RGB : GL_RGBA, c->txr.width, c->txr.height, 0,
                         gfmt, gtype, buf);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    }
    free(buf);
    e->gen = blk->generation;
}

static GLuint texture_for(const pvr_poly_cxt_t *c) {
    const pvr_shim_block_t *blk = pvr_shim_block(c->txr.base);
    if(!blk) return white_tex;
    uintptr_t key = (uintptr_t)c->txr.base;
    uint32_t h = (uint32_t)((key >> 5) * 2654435761u) % TEX_SLOTS;
    for(int probe = 0; probe < TEX_SLOTS; probe++, h = (h + 1) % TEX_SLOTS) {
        tex_entry_t *e = &texes[h];
        if(!e->tex) {
            *e = (tex_entry_t){c->txr.base, c->txr.format, c->txr.width, c->txr.height, c->txr.mipmap, 0, 0};
            glGenTextures(1, &e->tex);
            upload(e, c, blk);
            return e->tex;
        }
        if(e->base == c->txr.base && e->fmt == c->txr.format && e->w == c->txr.width &&
           e->h == c->txr.height && e->mip == (bool)c->txr.mipmap) {
            if(e->gen != blk->generation) upload(e, c, blk);
            return e->tex;
        }
    }
    return white_tex;
}

/* ---- state ---- */

static GLenum gl_blend(int f, bool src) {
    switch(f) {
        case PVR_BLEND_ZERO: return GL_ZERO;
        case PVR_BLEND_ONE: return GL_ONE;
        case PVR_BLEND_DESTCOLOR: return src ? GL_DST_COLOR : GL_SRC_COLOR;   /* "other" colour */
        case PVR_BLEND_INVDESTCOLOR: return src ? GL_ONE_MINUS_DST_COLOR : GL_ONE_MINUS_SRC_COLOR;
        case PVR_BLEND_SRCALPHA: return GL_SRC_ALPHA;
        case PVR_BLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
        case PVR_BLEND_DESTALPHA: return GL_DST_ALPHA;
        default: return GL_ONE_MINUS_DST_ALPHA;
    }
}

static const GLenum gl_depth[8] = {GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL,
                                   GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};

static void apply(const pvr_poly_cxt_t *c, int list, const pvr_shim_state_t *st) {
    bool tex = c->txr.enable != 0 && c->txr.base;
    glUniform1i(u_tex_on, tex);
    if(tex) {
        glBindTexture(GL_TEXTURE_2D, texture_for(c));
        bool bilinear = c->txr.filter != PVR_FILTER_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, bilinear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        c->txr.mipmap ? (bilinear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST)
                                      : (bilinear ? GL_LINEAR : GL_NEAREST));
        /* D-adjust in quarters (PVR_MIPBIAS_*): 4 is the hardware's own pick. */
        int bias = c->txr.mipmap_bias ? c->txr.mipmap_bias : PVR_MIPBIAS_NORMAL;
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, log2f(bias / 4.0f));
        bool cu = c->txr.uv_clamp & PVR_UVCLAMP_U, cv = c->txr.uv_clamp & PVR_UVCLAMP_V;
        bool fu = c->txr.uv_flip & 2, fv = c->txr.uv_flip & 1;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, cu ? GL_CLAMP_TO_EDGE : fu ? GL_MIRRORED_REPEAT : GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, cv ? GL_CLAMP_TO_EDGE : fv ? GL_MIRRORED_REPEAT : GL_REPEAT);
        glUniform1i(u_env, c->txr.env);
        glUniform1i(u_tex_alpha, c->txr.alpha != 0);   /* the shim keeps 1 = texture alpha on */
    }
    glUniform1i(u_spec, c->gen.specular == PVR_SPECULAR_ENABLE);
    glUniform1i(u_fog_mode, c->gen.fog_type);
    glUniform1i(u_use_alpha, c->gen.alpha != 0);
    glUniform1f(u_alpha_ref, list == TA_PT ? (st->pt_alpha_ref + 0.5f) / 255.0f : -1.0f);

    if(list == TA_TR) {
        glEnable(GL_BLEND);
        glBlendFunc(gl_blend(c->blend.src, true), gl_blend(c->blend.dst, false));
    }
    else glDisable(GL_BLEND);
    glDepthFunc(gl_depth[c->depth.comparison & 7]);
    glDepthMask(c->depth.write == PVR_DEPTHWRITE_ENABLE);
    /* Screen-space winding; y points down on the PVR and up here. */
    if(c->gen.culling == PVR_CULLING_CW || c->gen.culling == PVR_CULLING_CCW) {
        glEnable(GL_CULL_FACE);
        glCullFace(c->gen.culling == PVR_CULLING_CW ? GL_FRONT : GL_BACK);
    }
    else glDisable(GL_CULL_FACE);
}

/* ---- screenshots ---- */

static void put32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static void png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t hdr[8];
    put32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if(len) fwrite(data, 1, len, f);
    uLong crc = crc32(crc32(0, (const Bytef *)type, 4), data, len);
    put32(hdr, (uint32_t)crc);
    fwrite(hdr, 1, 4, f);
}

static void save_png(const char *path, const uint8_t *rgb, int w, int h) {
    size_t raw_len = (size_t)(w * 3 + 1) * h;
    uint8_t *raw = malloc(raw_len);
    for(int y = 0; y < h; y++) {
        raw[y * (w * 3 + 1)] = 0;
        memcpy(raw + y * (w * 3 + 1) + 1, rgb + (size_t)(h - 1 - y) * w * 3, (size_t)w * 3);
    }
    uLongf zlen = compressBound(raw_len);
    uint8_t *z = malloc(zlen);
    compress2(z, &zlen, raw, raw_len, 6);
    FILE *f = fopen(path, "wb");
    if(f) {
        static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
        uint8_t ihdr[13];
        put32(ihdr, w);
        put32(ihdr + 4, h);
        ihdr[8] = 8;    /* bit depth */
        ihdr[9] = 2;    /* RGB */
        ihdr[10] = ihdr[11] = ihdr[12] = 0;
        fwrite(sig, 1, 8, f);
        png_chunk(f, "IHDR", ihdr, 13);
        png_chunk(f, "IDAT", z, (uint32_t)zlen);
        png_chunk(f, "IEND", NULL, 0);
        fclose(f);
        printf("DKU SHOT %s\n", path);
    }
    free(z);
    free(raw);
}

static void maybe_shot(void) {
    for(int i = 0; i < nshots; i++)
        if(shots[i] == (int)frame_no) {
            const char *dir = getenv("DKU_CAPTURE_DIR"), *tag = getenv("DKU_SHOT_TAG");
            char path[512];
            if(!dir) dir = "captures/host";
            mkdir(dir, 0755);
            snprintf(path, sizeof(path), "%s/%s-f%05lu.png", dir, tag ? tag : "dku", frame_no);
            uint8_t *px = malloc(FB_W * FB_H * 3);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, FB_W, FB_H, GL_RGB, GL_UNSIGNED_BYTE, px);
            save_png(path, px, FB_W, FB_H);
            free(px);
        }
}

/* ---- backend API ---- */

void backend_init(void) {
    plat_init();
    prog = link_prog(vs_src, fs_src);
    blit_prog = link_prog(blit_vs, blit_fs);
#define U(n) u_##n = glGetUniformLocation(prog, "u_" #n)
    U(tex_on);
    U(env);
    U(spec);
    U(fog_mode);
    U(use_alpha);
    U(tex_alpha);
    U(alpha_ref);
    U(fog_color);
    U(fog_table);
    U(fog_far);
    U(tex);
#undef U
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ta_vertex_t), (void *)offsetof(ta_vertex_t, x));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ta_vertex_t), (void *)offsetof(ta_vertex_t, u));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ta_vertex_t), (void *)offsetof(ta_vertex_t, argb));
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ta_vertex_t), (void *)offsetof(ta_vertex_t, oargb));

    glGenTextures(1, &fb_color);
    glBindTexture(GL_TEXTURE_2D, fb_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, FB_W, FB_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenRenderbuffers(1, &fb_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, fb_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, FB_W, FB_H);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb_color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fb_depth);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "DKU GL: framebuffer incomplete\n");
        exit(1);
    }

    static const uint32_t white = 0xffffffff;
    glGenTextures(1, &white_tex);
    glBindTexture(GL_TEXTURE_2D, white_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);

    const char *s = getenv("DKU_SHOTS");
    while(s && *s && nshots < 64) {
        char *e;
        long v = strtol(s, &e, 10);
        if(e == s) break;
        shots[nshots++] = (int)v;
        s = e;
    }
    printf("DKU GL: %s, %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
}

void backend_shutdown(void) {}

void backend_draw(const ta_frame_t *f, const pvr_shim_state_t *st) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, FB_W, FB_H);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(st->bg[0], st->bg[1], st->bg[2], 1);
    glClearDepth(0);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);

    glUseProgram(prog);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    size_t bytes = (size_t)f->nverts * sizeof(ta_vertex_t);
    if(bytes > vbo_cap) {
        vbo_cap = bytes * 2;
        glBufferData(GL_ARRAY_BUFFER, vbo_cap, NULL, GL_STREAM_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, f->verts);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(u_tex, 0);
    glUniform4f(u_fog_color, st->fog_color[1], st->fog_color[2], st->fog_color[3], st->fog_color[0]);
    glUniform1fv(u_fog_table, 129, st->fog_table);
    glUniform1f(u_fog_far, st->fog_far_depth);

    static const int order[TA_LISTS] = {TA_OP, TA_PT, TA_TR};
    for(int li = 0; li < TA_LISTS; li++) {
        int l = order[li];
        int last_hdr = -1;
        for(int i = 0; i < f->nbatches[l]; i++) {
            const ta_batch_t *b = &f->batches[l][i];
            if(b->hdr != last_hdr) {
                apply(&f->hdrs[b->hdr].cxt, l, st);
                last_hdr = b->hdr;
            }
            /* PVR rows count down from the top; GL's from the bottom. */
            int y0 = b->clip_y0 < 0 ? 0 : b->clip_y0, y1 = b->clip_y1 > FB_H ? FB_H : b->clip_y1;
            glScissor(0, FB_H - y1, FB_W, y1 > y0 ? y1 - y0 : 0);
            glDrawArrays(GL_TRIANGLES, b->first, b->count);
        }
    }
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
}

void backend_present(void) {
    frame_no++;
    maybe_shot();
    int w, h;
    plat_window_size(&w, &h);
    /* Keep 4:3, letterboxed. */
    int vw = w, vh = w * 3 / 4;
    if(vh > h) {
        vh = h;
        vw = h * 4 / 3;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport((w - vw) / 2, (h - vh) / 2, vw, vh);
    glUseProgram(blit_prog);
    glBindTexture(GL_TEXTURE_2D, fb_color);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    plat_swap();
    plat_frame_done();
}
