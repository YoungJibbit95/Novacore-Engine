#version 450
layout(location = 0) in vec3 in_position;
layout(push_constant) uniform ShadowPush { mat4 light_world; } pc;
void main() {
    gl_Position = pc.light_world * vec4(in_position, 1.0);
}
