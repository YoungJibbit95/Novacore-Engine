#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;

layout(push_constant) uniform MeshPushConstants {
    mat4 world_view_projection;
    vec4 color;
    vec4 light_direction_ambient;
    vec4 fill_direction_intensity;
    vec4 material_response;
} pc;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_light_direction_ambient;
layout(location = 4) out vec4 v_fill_direction_intensity;
layout(location = 5) out vec4 v_material_response;

void main() {
    v_color = pc.color;
    v_normal = normalize(in_normal);
    v_uv = in_texcoord;
    v_light_direction_ambient = pc.light_direction_ambient;
    v_fill_direction_intensity = pc.fill_direction_intensity;
    v_material_response = pc.material_response;
    gl_Position = pc.world_view_projection * vec4(in_position, 1.0);
}
