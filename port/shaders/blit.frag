#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_col;
layout(binding = 0) uniform sampler2D u_fb;
void main() {
    o_col = texture(u_fb, v_uv);
}
