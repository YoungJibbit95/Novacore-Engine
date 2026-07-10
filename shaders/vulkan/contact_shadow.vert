#version 450

layout(push_constant) uniform ShadowPushConstants {
    mat4 view_projection;
    vec4 caster_receiver;
    vec4 radius_height;
    vec4 sun_opacity;
} pc;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out float v_opacity;
layout(location = 2) out float v_softness;

const vec2 k_corners[6] = vec2[](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0)
);

void main() {
    vec2 sun_xz = pc.sun_opacity.xy;
    float sun_height = max(pc.sun_opacity.z, 0.12);
    vec2 projected_offset = -sun_xz * (max(pc.radius_height.z, 0.0) / sun_height);
    vec2 corner = k_corners[gl_VertexIndex];
    vec3 center = vec3(
        pc.caster_receiver.x + projected_offset.x,
        pc.caster_receiver.w,
        pc.caster_receiver.z + projected_offset.y);
    vec3 world = center + vec3(corner.x * pc.radius_height.x, 0.0, corner.y * pc.radius_height.y);
    v_uv = corner;
    v_opacity = clamp(pc.sun_opacity.w, 0.0, 1.0);
    v_softness = clamp(pc.radius_height.w, 0.05, 1.0);
    gl_Position = pc.view_projection * vec4(world, 1.0);
}
