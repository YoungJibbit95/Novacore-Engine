#version 450

layout(push_constant) uniform PushConstants {
    mat4 view_projection;
    vec4 center;
    vec4 half_extents;
    vec4 color;
    vec4 light_direction_ambient;
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

const vec3 k_face_normals[6] = vec3[](
    vec3( 0.0,  0.0, -1.0),
    vec3( 0.0,  0.0,  1.0),
    vec3(-1.0,  0.0,  0.0),
    vec3( 1.0,  0.0,  0.0),
    vec3( 0.0,  1.0,  0.0),
    vec3( 0.0, -1.0,  0.0)
);

void main() {
    vec3 world_position = pc.center.xyz + (k_corners[gl_VertexIndex] * pc.half_extents.xyz);
    vec3 normal = k_face_normals[gl_VertexIndex / 6];
    vec3 light_dir = normalize(pc.light_direction_ambient.xyz);
    float ambient = clamp(pc.light_direction_ambient.w, 0.02, 0.95);
    float diffuse = clamp(dot(normal, light_dir), 0.0, 1.0);
    float shade = clamp(ambient + diffuse * (1.0 - ambient), 0.0, 1.0);
    gl_Position = pc.view_projection * vec4(world_position, 1.0);
    v_color = vec4(pc.color.rgb * shade, pc.color.a);
}
