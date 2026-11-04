#version 450
// DKU: render_gl.c's fs_src for GX2. Flags are floats (0 or 1) and the
// fog table is 129 floats packed four to a vec4.
#define DEPTH_K (1.0 / 128.0)
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_col;
layout(location = 2) in vec4 v_ofs;
layout(location = 0) out vec4 o_col;
layout(binding = 0) uniform sampler2D u_tex;
// Per header: tex_on, env (PVR_TXRENV_*), spec, fog_mode (PVR_FOG_*);
// use_alpha, tex_alpha, alpha_ref (< 0: no alpha test), unused.
layout(binding = 0, std140) uniform HdrBlock {
    vec4 u_hdr0;
    vec4 u_hdr1;
};
// Per frame: fog colour (r, g, b, a), fog far depth in x, the table.
layout(binding = 1, std140) uniform FrameBlock {
    vec4 u_fog_color;
    vec4 u_fog_far;
    vec4 u_fog_table[33];
};
float fog_at(int i) {
    vec4 q = u_fog_table[i >> 2];
    int c = i & 3;
    return c == 0 ? q.x : c == 1 ? q.y : c == 2 ? q.z : q.w;
}
void main() {
    float v_z = gl_FragCoord.z / DEPTH_K;   // 1/w
    vec4 c = v_col;
    if(u_hdr1.x < 0.5) c.a = 1.0;
    if(u_hdr0.x > 0.5) {
        vec4 t = texture(u_tex, v_uv);
        if(u_hdr1.y < 0.5) t.a = 1.0;
        int env = int(u_hdr0.y + 0.5);
        if(env == 0) c = t;
        else if(env == 1) c = vec4(t.rgb * c.rgb, t.a);
        else if(env == 2) c = vec4(mix(c.rgb, t.rgb, t.a), c.a);
        else c = t * c;
    }
    if(u_hdr0.z > 0.5) c.rgb += v_ofs.rgb;
    int fog = int(u_hdr0.w + 0.5);
    if(fog == 0 || fog == 3) {
        float k = clamp(16.0 * log2(max(u_fog_far.x * v_z, 1e-30)), 0.0, 127.999);
        int i = int(k);
        float f = mix(fog_at(i), fog_at(i + 1), k - float(i));
        c.rgb = mix(c.rgb, u_fog_color.rgb, f * u_fog_color.a);
    } else if(fog == 1) {
        c.rgb = mix(c.rgb, u_fog_color.rgb, v_ofs.a);
    }
    if(c.a < u_hdr1.z) discard;
    o_col = clamp(c, 0.0, 1.0);
}
