#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(push_constant) uniform MeshPushConstants {
    mat4 world_view_projection;
    vec4 color;
    vec4 light_direction_ambient;
} pc;

layout(location = 0) out vec4 v_color;

void main() {
    vec3 light_dir = normalize(pc.light_direction_ambient.xyz);
    float ambient = clamp(pc.light_direction_ambient.w, 0.02, 0.95);
    float diffuse = clamp(dot(normalize(in_normal), light_dir), 0.0, 1.0);
    float shade = clamp(ambient + diffuse * (1.0 - ambient), 0.0, 1.0);
    v_color = vec4(pc.color.rgb * shade, pc.color.a);
    gl_Position = pc.world_view_projection * vec4(in_position, 1.0);
}
