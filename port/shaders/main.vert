#version 450
// DKU: the TA vertex as render_gl.c's vs_src: screen x, y and 1/w back to
// clip space; clip z is the constant DEPTH_K so window depth is DEPTH_K / w
// (halfZ clip control, render_gx2.c).
#define DEPTH_K (1.0 / 128.0)
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_col;   // ARGB word, fetched 8-in-32 swapped: b, g, r, a
layout(location = 3) in vec4 a_ofs;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_col;
layout(location = 2) out vec4 v_ofs;
void main() {
    float z = max(a_pos.z, 1e-7);
    float w = 1.0 / z;
    gl_Position = vec4((a_pos.x / 320.0 - 1.0) * w, (1.0 - a_pos.y / 240.0) * w, DEPTH_K, w);
    v_uv = a_uv;
    v_col = a_col.bgra;
    v_ofs = a_ofs.bgra;
}
