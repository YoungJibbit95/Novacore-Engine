#version 450
#extension GL_GOOGLE_include_directive : require
#include "box_geometry.glsl"
layout(push_constant) uniform ShadowBoxPush {
    mat4 light_view_projection;
    vec4 center;
    vec4 half_extents;
} pc;
void main() {
    vec3 world_position = pc.center.xyz + k_corners[gl_VertexIndex] * pc.half_extents.xyz;
    gl_Position = pc.light_view_projection * vec4(world_position, 1.0);
}
