#version 450
// DKU: the TA vertex as render_gl.c's vs_src: screen x, y and 1/w back to
// clip space, or a world vertex through its matrix; clip z is the constant DEPTH_K so window depth is DEPTH_K / w
// (halfZ clip control, render_gx2.c).
#define DEPTH_K (1.0 / 128.0)
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_col;   // ARGB word, fetched 8-in-32 swapped: b, g, r, a
layout(location = 3) in vec4 a_ofs;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_col;
layout(location = 2) out vec4 v_ofs;
// World batches (port/ta.h): u_m's columns take the model-space vertex to
// clip space; u_world.x is 1 for them, 0 for screen vertices.
layout(binding = 2, std140) uniform XformBlock {
    vec4 u_m[4];
    vec4 u_world;
};
void main() {
    if(u_world.x > 0.5) {
        gl_Position = u_m[0] * a_pos.x + u_m[1] * a_pos.y + u_m[2] * a_pos.z + u_m[3];
    } else {
        float w = 1.0 / max(a_pos.z, 1e-7);
        gl_Position = vec4((a_pos.x / 320.0 - 1.0) * w, (1.0 - a_pos.y / 240.0) * w, DEPTH_K, w);
    }
    v_uv = a_uv;
    v_col = a_col.bgra;
    v_ofs = a_ofs.bgra;
}
