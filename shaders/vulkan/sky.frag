#version 450

layout(push_constant) uniform SkyPushConstants {
    vec4 zenith_color;
    vec4 horizon_color;
    vec4 ground_color;
    vec4 parameters;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() {
    float horizon = clamp(pc.parameters.x, 0.05, 0.95);
    float power = clamp(pc.parameters.y, 0.20, 5.0);
    float exposure = clamp(pc.parameters.z, 0.0, 4.0);
    float vertical = clamp(v_uv.y, 0.0, 1.0);

    vec3 color;
    if (vertical >= horizon) {
        float t = pow(clamp((vertical - horizon) / max(1.0 - horizon, 0.001), 0.0, 1.0), power);
        color = mix(pc.horizon_color.rgb, pc.zenith_color.rgb, t);
    } else {
        float t = pow(clamp(1.0 - (vertical / max(horizon, 0.001)), 0.0, 1.0), power);
        color = mix(pc.horizon_color.rgb, pc.ground_color.rgb, t);
    }

    out_color = vec4(clamp(color * exposure, vec3(0.0), vec3(1.0)), 1.0);
}
