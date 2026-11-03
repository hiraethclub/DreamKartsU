#version 450
layout(location = 0) in vec4 a_pos;
layout(location = 1) in vec4 a_col;
layout(location = 0) out vec4 v_col;
layout(binding = 0, std140) uniform VSBlock { mat4 u_mvp; };
void main() {
    gl_Position = u_mvp * a_pos;
    v_col = a_col;
}
