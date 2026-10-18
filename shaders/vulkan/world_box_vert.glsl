
layout(push_constant) uniform PushConstants {
    mat4 view_projection;
    vec4 center;
    vec4 half_extents;
    vec4 color;
    vec4 light_direction_ambient;
} pc;

layout(location = 0) out vec4 v_color;
#ifdef DIRECTIONAL_SHADOW_RECEIVER
layout(location = 1) out vec3 v_world_position;
layout(location = 2) out vec3 v_world_normal;
layout(location = 3) out vec2 v_ambient_diffuse;
#endif

#include "box_geometry.glsl"

void main() {
    vec3 world_position = pc.center.xyz + (k_corners[gl_VertexIndex] * pc.half_extents.xyz);
    vec3 normal = k_face_normals[gl_VertexIndex / 6];
    vec3 light_dir = normalize(pc.light_direction_ambient.xyz);
    float ambient = clamp(pc.light_direction_ambient.w, 0.02, 0.95);
    float diffuse = clamp(dot(normal, light_dir), 0.0, 1.0);
    float shade = clamp(ambient + diffuse * (1.0 - ambient), 0.0, 1.0);
    gl_Position = pc.view_projection * vec4(world_position, 1.0);
    #ifdef DIRECTIONAL_SHADOW_RECEIVER
    v_color = pc.color;
    v_world_position = world_position;
    v_world_normal = normal;
    v_ambient_diffuse = vec2(ambient, diffuse * (1.0 - ambient));
#else
    v_color = vec4(pc.color.rgb * shade, pc.color.a);
#endif
}
