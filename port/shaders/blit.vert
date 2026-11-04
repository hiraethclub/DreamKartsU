#version 450
// DKU: a full-screen quad (scaling the 640x480 frame to the TV and GamePad,
// and the shadow darken pass).
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 0) out vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
