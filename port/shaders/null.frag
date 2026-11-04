#version 450
// DKU: colour writes are masked off or blended with the constant colour.
layout(location = 0) out vec4 o_col;
void main() {
    o_col = vec4(0.0);
}
