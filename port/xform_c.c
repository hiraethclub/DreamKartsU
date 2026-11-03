/* DKU: C versions of upstream src/xform.S, the SH-4 assembly hot paths
   render.c calls. Each follows the contract in xform.S's comments and the
   C reference beside it in render.c's -DDK_XF_CHECK build, which can run
   against these (tools/test_host.sh XFCHECK=1). Output goes to the
   software TA through pvr_dr_target()/pvr_dr_commit(). */
#include "render.h"
#include <sh4zam/shz_sh4zam.h>

/* Must match render.c. */
typedef struct { float near_w, xmax, y0, y1; uint8_t *codes; const uint32_t *col; } XfParams;
typedef struct { const pvr_vertex_t *pv; const uint8_t *codes; uint32_t clip_mask, sent, back; } MeshSubmit;
typedef struct { const float (*pos)[4]; const uint32_t *col; uint32_t sent, back; } StripSubmit;

static inline void send(const pvr_vertex_t *v, uint32_t flags) {
    pvr_vertex_t *d = pvr_dr_target();
    *d = *v;
    d->flags = flags;
    pvr_dr_commit(d);
}

/* Transform (x, y, z) with XMTRX, its outcode and projected vertex. */
static inline unsigned stage_one(float x, float y, float z, float u, float v, uint32_t argb,
                                 const XfParams *p, pvr_vertex_t *out) {
    shz_vec4_t r = shz_xmtrx_transform_vec4(shz_vec4_init(x, y, z, 1.0f));
    unsigned code = r.w < p->near_w ? 16u : 0u;
    if(r.x < 0) code |= 1;
    if(r.x > p->xmax * r.w) code |= 2;
    if(r.y < p->y0 * r.w) code |= 4;
    if(r.y > p->y1 * r.w) code |= 8;
    float inv = shz_invf_fsrra(r.w);
    out->flags = PVR_CMD_VERTEX;
    out->x = r.x * inv;
    out->y = r.y * inv;
    out->z = inv;
    out->u = u;
    out->v = v;
    out->argb = argb;
    out->oargb = 0;
    return code;
}

uint32_t dk_xf_stage(const RV *v, int n, pvr_vertex_t *out, const XfParams *p) {
    unsigned and_code = 31, or_code = 0;
    for(int i = 0; i < n; i++) {
        unsigned c = stage_one(v[i].x, v[i].y, v[i].z, v[i].u, v[i].v, v[i].c, p, &out[i]);
        p->codes[i] = (uint8_t)c;
        and_code &= c;
        or_code |= c;
    }
    return and_code | or_code << 8;
}

uint32_t dk_xf_stage_kv(const KV *v, int n, pvr_vertex_t *out, const XfParams *p) {
    unsigned and_code = 31, or_code = 0;
    for(int i = 0; i < n; i++) {
        unsigned c = stage_one(v[i].x, v[i].y, v[i].z, 0, 0, p->col[v[i].group], p, &out[i]);
        p->codes[i] = (uint8_t)c;
        and_code &= c;
        or_code |= c;
    }
    return and_code | or_code << 8;
}

void dk_sq_run(const pvr_vertex_t *src, int n) {
    for(int i = 0; i < n; i++) send(&src[i], i == n - 1 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX);
}

void dk_xf_emit(const RV *v, int n) {
    /* Only the projection is used: the strip needs no clip tests. */
    XfParams p = {0, 640.0f, 0, 480.0f, NULL, NULL};
    for(int i = 0; i < n; i++) {
        pvr_vertex_t pv;
        stage_one(v[i].x, v[i].y, v[i].z, v[i].u, v[i].v, v[i].c, &p, &pv);
        send(&pv, i == n - 1 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX);
    }
}

int dk_sq_tris(MeshSubmit *ms, const uint16_t *idx, int ntri, int cull) {
    for(int t = 0; t < ntri; t++) {
        unsigned a = idx[t * 3], b = idx[t * 3 + 1], c = idx[t * 3 + 2];
        unsigned ca = ms->codes[a], cb = ms->codes[b], cc = ms->codes[c];
        if(ca & cb & cc) continue;
        if((ca | cb | cc) & ms->clip_mask) return t;
        const pvr_vertex_t *pa = &ms->pv[a], *pb = &ms->pv[b], *pc = &ms->pv[c];
        if(cull) {
            float area = (pb->x - pa->x) * (pc->y - pa->y) - (pb->y - pa->y) * (pc->x - pa->x);
            if(area <= 0) {
                ms->back++;
                continue;
            }
        }
        send(pa, PVR_CMD_VERTEX);
        send(pb, PVR_CMD_VERTEX);
        send(pc, PVR_CMD_VERTEX_EOL);
        ms->sent++;
    }
    return ntri;
}

uint32_t dk_xf_pos(const float (*pos)[4], int n, float (*out)[4], const XfParams *p) {
    unsigned and_code = 31, or_code = 0;
    for(int i = 0; i < n; i++) {
        pvr_vertex_t pv;
        unsigned c = stage_one(pos[i][0], pos[i][1], pos[i][2], 0, 0, 0, p, &pv);
        and_code &= c;
        or_code |= c;
        out[i][0] = pv.x;
        out[i][1] = pv.y;
        out[i][2] = pv.z;
    }
    return and_code | or_code << 8;
}

static inline bool strip_front(const float *a, const float *b, const float *c) {
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]) > 0;
}

static inline void strip_vertex(const StripSubmit *ss, const uint16_t *e, uint32_t flags) {
    const float *p = ss->pos[e[0] / 16];
    pvr_vertex_t v = {flags, p[0], p[1], p[2], 0, 0, ss->col[e[1] / 4], 0};
    send(&v, flags);
}

/* Strip format (render.h KMesh): n, then n pairs (position index * 16,
   group * 4); 0 ends. Triangle k is k, k+1, k+2, its first two swapped
   when k is odd. A run of front faces k0..k1 goes as the sub-strip of
   vertices k0..k1 + 2. */
void dk_sq_strips(StripSubmit *ss, const uint16_t *strips) {
    const uint16_t *s = strips;
    for(int n; (n = *s++) != 0; s += n * 2) {
        int m = n - 2;
        bool front[R_MESH_STRIP_TRIS];
        for(int k = 0; k < m; k++) {
            const float *pa = ss->pos[s[(k + (k & 1)) * 2] / 16];
            const float *pb = ss->pos[s[(k + 1 - (k & 1)) * 2] / 16];
            const float *pc = ss->pos[s[(k + 2) * 2] / 16];
            front[k] = strip_front(pa, pb, pc);
            if(front[k]) ss->sent++;
            else ss->back++;
        }
        for(int k = 0; k < m;) {
            if(!front[k]) {
                k++;
                continue;
            }
            int k1 = k;
            while(k1 + 1 < m && front[k1 + 1]) k1++;
            /* An odd first triangle would flip the run's winding; the PVR
               never culls by winding here (PVR_CULLING_SMALL), and the TA
               keeps the strip's own order, so send it as it stands. */
            for(int i = k; i <= k1 + 2; i++)
                strip_vertex(ss, &s[i * 2], i == k1 + 2 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX);
            k = k1 + 1;
        }
    }
}
