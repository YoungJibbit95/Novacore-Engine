layout(set = 0, binding = 1) uniform sampler2DShadow directional_shadow;

float directionalVisibility(vec3 world_position, vec3 world_normal) {
    if (shadow.parameters.w < 0.5) {
        return 1.0;
    }
    float normalized_distance = length(world_position - shadow.camera_position_distance.xyz)
        / shadow.camera_position_distance.w;
    if (normalized_distance >= 1.0) {
        return 1.0;
    }
    vec4 projected = shadow.light_view_projection * vec4(world_position, 1.0);
    vec3 position = projected.xyz / projected.w;
    if (any(greaterThan(abs(position.xy), vec2(1.0))) || position.z < 0.0 || position.z > 1.0) {
        return 1.0;
    }
    vec2 uv = position.xy * 0.5 + 0.5;
    float slope = 1.0 - max(dot(normalize(world_normal), shadow.toward_sun.xyz), 0.0);
    float reference = position.z - shadow.parameters.x - shadow.parameters.y * slope;
    float visible = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            visible += texture(directional_shadow,
                vec3(uv + vec2(x, y) * shadow.parameters.z, reference));
        }
    }
    float edge_fade = smoothstep(0.8, 1.0, normalized_distance);
    return mix(visible / 9.0, 1.0, edge_fade);
}
