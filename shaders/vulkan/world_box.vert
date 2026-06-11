#version 450

layout(push_constant) uniform PushConstants {
    mat4 view_projection;
    vec4 center;
    vec4 half_extents;
    vec4 color;
} pc;

layout(location = 0) out vec4 v_color;

const vec3 k_corners[36] = vec3[](
    vec3(-1.0, -1.0, -1.0), vec3( 1.0, -1.0, -1.0), vec3( 1.0,  1.0, -1.0),
    vec3(-1.0, -1.0, -1.0), vec3( 1.0,  1.0, -1.0), vec3(-1.0,  1.0, -1.0),

    vec3( 1.0, -1.0,  1.0), vec3(-1.0, -1.0,  1.0), vec3(-1.0,  1.0,  1.0),
    vec3( 1.0, -1.0,  1.0), vec3(-1.0,  1.0,  1.0), vec3( 1.0,  1.0,  1.0),

    vec3(-1.0, -1.0,  1.0), vec3(-1.0, -1.0, -1.0), vec3(-1.0,  1.0, -1.0),
    vec3(-1.0, -1.0,  1.0), vec3(-1.0,  1.0, -1.0), vec3(-1.0,  1.0,  1.0),

    vec3( 1.0, -1.0, -1.0), vec3( 1.0, -1.0,  1.0), vec3( 1.0,  1.0,  1.0),
    vec3( 1.0, -1.0, -1.0), vec3( 1.0,  1.0,  1.0), vec3( 1.0,  1.0, -1.0),

    vec3(-1.0,  1.0, -1.0), vec3( 1.0,  1.0, -1.0), vec3( 1.0,  1.0,  1.0),
    vec3(-1.0,  1.0, -1.0), vec3( 1.0,  1.0,  1.0), vec3(-1.0,  1.0,  1.0),

    vec3(-1.0, -1.0,  1.0), vec3( 1.0, -1.0,  1.0), vec3( 1.0, -1.0, -1.0),
    vec3(-1.0, -1.0,  1.0), vec3( 1.0, -1.0, -1.0), vec3(-1.0, -1.0, -1.0)
);

void main() {
    vec3 world_position = pc.center.xyz + (k_corners[gl_VertexIndex] * pc.half_extents.xyz);
    gl_Position = pc.view_projection * vec4(world_position, 1.0);
    v_color = pc.color;
}
