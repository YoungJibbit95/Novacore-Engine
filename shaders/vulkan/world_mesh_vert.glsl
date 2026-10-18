
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
#ifdef MATERIAL_TEXTURES
layout(location = 3) in vec2 in_texcoord1;
#include "material_frame.glsl"
layout(location = 7) out vec2 v_uv1;
#endif

layout(push_constant) uniform MeshPushConstants {
    mat4 world_view_projection;
    vec4 color;
    vec4 normal_rotation;
    vec3 inverse_scale;
    uint sun_direction;
    uvec4 lighting_material;
} pc;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_light_direction_ambient;
layout(location = 4) out vec4 v_fill_direction_intensity;
layout(location = 5) out vec4 v_material_response;
#ifdef DIRECTIONAL_SHADOW_RECEIVER
#include "shadow_data.glsl"
#endif
#if defined(DIRECTIONAL_SHADOW_RECEIVER) || defined(MATERIAL_TEXTURES)
layout(location = 6) out vec3 v_world_position;
#endif

vec3 unpackDirection(uint packed) {
    vec2 oct = unpackUnorm2x16(packed) * 2.0 - 1.0;
    vec3 direction = vec3(oct, 1.0 - abs(oct.x) - abs(oct.y));
    float fold = clamp(-direction.z, 0.0, 1.0);
    direction.xy += mix(vec2(fold), vec2(-fold), greaterThanEqual(direction.xy, vec2(0.0)));
    return normalize(direction);
}

vec3 rotateNormal(vec4 rotation, vec3 value) {
    return value + 2.0 * cross(rotation.xyz, cross(rotation.xyz, value) + rotation.w * value);
}

void main() {
    v_color = pc.color;
    // Inverse transpose of the render instance T*R*S. Imported/skinned vertex
    // normals already include their node transforms; apply the instance once.
    vec3 scaled_normal = in_normal * pc.inverse_scale;
    // Remove a common magnitude before squaring/rotating, avoiding overflow for
    // small finite instance scales without changing the normal's direction.
    float normal_scale = max(max(abs(scaled_normal.x), abs(scaled_normal.y)), abs(scaled_normal.z));
    scaled_normal = normal_scale > 0.0 ? scaled_normal / normal_scale : vec3(0.0, 1.0, 0.0);
    v_normal = normalize(rotateNormal(pc.normal_rotation, scaled_normal));
    v_uv = in_texcoord;
#ifdef MATERIAL_TEXTURES
    v_uv1 = in_texcoord1;
#endif
    vec2 ambient_fill = unpackUnorm2x16(pc.lighting_material.y);
    v_light_direction_ambient = vec4(unpackDirection(pc.sun_direction), ambient_fill.x);
    v_fill_direction_intensity = vec4(unpackDirection(pc.lighting_material.x), ambient_fill.y);
    v_material_response = vec4(unpackUnorm2x16(pc.lighting_material.z), unpackUnorm2x16(pc.lighting_material.w));
    gl_Position = pc.world_view_projection * vec4(in_position, 1.0);
#ifdef MATERIAL_TEXTURES
    v_world_position = materialWorldPosition(gl_Position);
#elif defined(DIRECTIONAL_SHADOW_RECEIVER)
    v_world_position = shadowWorldPosition(gl_Position);
#endif
}
