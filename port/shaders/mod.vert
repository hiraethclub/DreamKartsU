#version 450
// DKU: modifier volume faces: position only, main.vert's transform.
#define DEPTH_K (1.0 / 128.0)
layout(location = 0) in vec3 a_pos;
void main() {
    float w = 1.0 / max(a_pos.z, 1e-7);
    gl_Position = vec4((a_pos.x / 320.0 - 1.0) * w, (1.0 - a_pos.y / 240.0) * w, DEPTH_K, w);
}
