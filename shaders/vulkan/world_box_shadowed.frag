#version 450
#extension GL_GOOGLE_include_directive : require
#include "shadow_data.glsl"
#include "shadow_sampling.glsl"
layout(location = 0) in vec4 v_color;
layout(location = 1) in vec3 v_world_position;
layout(location = 2) in vec3 v_world_normal;
layout(location = 3) in vec2 v_ambient_diffuse;
layout(location = 0) out vec4 out_color;
void main() {
    float shade = clamp(v_ambient_diffuse.x + v_ambient_diffuse.y *
        directionalVisibility(v_world_position, v_world_normal), 0.0, 1.0);
    out_color = vec4(v_color.rgb * shade, v_color.a);
}
