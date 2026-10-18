#version 450

layout(push_constant) uniform SkyPushConstants {
    vec4 zenith_color;
    vec4 horizon_color;
    vec4 ground_color;
    vec4 parameters;
    vec4 sun_direction_size;
    vec4 ray_forward_haze;
    vec4 ray_right_cloud;
    vec4 ray_down;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() {
    float horizon = clamp(pc.parameters.x, 0.05, 0.95);
    float power = clamp(pc.parameters.y, 0.20, 5.0);
    float exposure = clamp(pc.parameters.z, 0.0, 4.0);
    vec2 ndc = v_uv * 2.0 - vec2(1.0);
    // Identical camera basis and FOV/aspect as the world view projection.
    vec3 ray = normalize(pc.ray_forward_haze.xyz + ndc.x * pc.ray_right_cloud.xyz + ndc.y * pc.ray_down.xyz);
    float vertical = clamp(ray.y * 0.5 + 0.5, 0.0, 1.0);

    vec3 color;
    if (vertical >= horizon) {
        float t = pow(clamp((vertical - horizon) / max(1.0 - horizon, 0.001), 0.0, 1.0), power);
        color = mix(pc.horizon_color.rgb, pc.zenith_color.rgb, t);
    } else {
        float t = pow(clamp(1.0 - (vertical / max(horizon, 0.001)), 0.0, 1.0), power);
        color = mix(pc.horizon_color.rgb, pc.ground_color.rgb, t);
    }

    float haze = clamp(pc.ray_forward_haze.w, 0.0, 1.0);
    float horizonBand = pow(1.0 - abs(ray.y), 5.0);
    color = mix(color, pc.horizon_color.rgb * 1.12, horizonBand * haze);

    vec3 sunDirection = normalize(pc.sun_direction_size.xyz);
    float angularRadius = max(pc.sun_direction_size.w, 0.001);
    float sunDot = clamp(dot(ray, sunDirection), -1.0, 1.0);
    float sunDisc = smoothstep(cos(angularRadius * 1.35), cos(angularRadius), sunDot);
    float sunGlow = pow(max(sunDot, 0.0), 96.0) * 0.38;
    color += vec3(1.0, 0.82, 0.58) * (sunDisc + sunGlow) * pc.parameters.w;

    float cloudStrength = clamp(pc.ray_right_cloud.w, 0.0, 1.0);
    float cloudNoise = sin(ray.x * 31.0 + ray.z * 23.0) * sin(ray.x * 13.0 - ray.z * 17.0);
    float cloudMask = smoothstep(0.28, 0.84, cloudNoise * 0.5 + 0.5) * smoothstep(-0.02, 0.35, ray.y);
    color = mix(color, vec3(0.88, 0.92, 0.96), cloudMask * cloudStrength * 0.32);

    color = vec3(1.0) - exp(-max(color, vec3(0.0)) * exposure);
    out_color = vec4(clamp(color, vec3(0.0), vec3(1.0)), 1.0);
}
