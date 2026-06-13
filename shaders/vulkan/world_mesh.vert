#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(push_constant) uniform MeshPushConstants {
    mat4 world_view_projection;
    vec4 color;
    vec4 light_direction_ambient;
    vec4 fill_direction_intensity;
    vec4 material_response;
} pc;

layout(location = 0) out vec4 v_color;

void main() {
    vec3 normal = normalize(in_normal);
    vec3 light_dir = normalize(pc.light_direction_ambient.xyz);
    vec3 fill_dir = normalize(pc.fill_direction_intensity.xyz);
    float ambient = clamp(pc.light_direction_ambient.w, 0.02, 0.95);
    float fill = clamp(pc.fill_direction_intensity.w, 0.0, 1.0);
    float rim_strength = clamp(pc.material_response.x, 0.0, 1.0);
    float spec_strength = clamp(pc.material_response.y, 0.0, 1.0);
    float contrast = clamp(pc.material_response.z, 0.5, 1.8);
    float saturation = clamp(pc.material_response.w, 0.0, 2.0);

    float key_diffuse = clamp(dot(normal, light_dir), 0.0, 1.0);
    float fill_diffuse = clamp(dot(normal, fill_dir) * 0.5 + 0.5, 0.0, 1.0) * fill;
    float rim = pow(1.0 - clamp(abs(normal.z), 0.0, 1.0), 2.0) * rim_strength;
    float spec = pow(clamp(dot(normal, normalize(light_dir + vec3(0.0, 0.25, 0.85))), 0.0, 1.0), 24.0) * spec_strength;
    float shade = clamp(ambient + (key_diffuse * (1.0 - ambient)) + fill_diffuse + rim + spec, 0.0, 1.35);

    vec3 lit = pc.color.rgb * shade;
    lit = (lit - vec3(0.5)) * contrast + vec3(0.5);
    float luma = dot(lit, vec3(0.2126, 0.7152, 0.0722));
    lit = mix(vec3(luma), lit, saturation);
    v_color = vec4(clamp(lit, vec3(0.0), vec3(1.0)), pc.color.a);
    gl_Position = pc.world_view_projection * vec4(in_position, 1.0);
}
