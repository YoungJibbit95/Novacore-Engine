#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec2 texcoord;
layout(location=2) in vec4 color;
layout(push_constant) uniform View { vec2 size; int srgb; } view;
layout(location=0) out vec2 uv;
layout(location=1) out vec4 tint;
void main() {
    gl_Position = vec4(2.0 * position / view.size - 1.0, 0.0, 1.0);
    uv = texcoord;
    tint = color;
}
