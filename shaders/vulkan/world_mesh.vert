#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(push_constant) uniform MeshPushConstants {
    mat4 world_view_projection;
    vec4 color;
} pc;

layout(location = 0) out vec4 v_color;

void main() {
    vec3 light_dir = normalize(vec3(0.35, 0.85, 0.28));
    float diffuse = clamp(dot(normalize(in_normal), light_dir), 0.0, 1.0);
    float shade = 0.28 + diffuse * 0.72;
    v_color = vec4(pc.color.rgb * shade, pc.color.a);
    gl_Position = pc.world_view_projection * vec4(in_position, 1.0);
}
