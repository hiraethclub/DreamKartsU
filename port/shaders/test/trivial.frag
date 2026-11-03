#version 450
layout(location = 0) in vec4 v_col;
layout(location = 0) out vec4 o_col;
layout(binding = 1, std140) uniform PSBlock { vec4 u_tint; };
void main() {
    o_col = v_col * u_tint;
}
