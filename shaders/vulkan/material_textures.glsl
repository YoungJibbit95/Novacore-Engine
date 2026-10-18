layout(set = 1, binding = 1, std140) uniform MaterialTextures {
    vec4 enabled_maps;
    vec4 emissive_normal_scale;
    uvec4 uv_sets;
} material;
layout(set = 1, binding = 2) uniform sampler2D base_color_map;
layout(set = 1, binding = 3) uniform sampler2D metallic_roughness_map;
layout(set = 1, binding = 4) uniform sampler2D normal_map;
layout(set = 1, binding = 5) uniform sampler2D emissive_map;

vec2 materialUv(uint selected, vec2 uv0, vec2 uv1) {
    return selected == 1u ? uv1 : uv0;
}

vec3 materialNormal(vec3 position, vec3 normal, vec2 uv) {
    if (material.enabled_maps.z < 0.5) {
        return normal;
    }
    vec3 dx = dFdx(position);
    vec3 dy = dFdy(position);
    vec2 ux = dFdx(uv);
    vec2 uy = dFdy(uv);
    float uv_scale = max(max(abs(ux.x), abs(ux.y)), max(abs(uy.x), abs(uy.y)));
    if (!(uv_scale > 0.0) || any(isinf(ux)) || any(isinf(uy)) || any(isnan(ux)) || any(isnan(uy))) {
        return normal;
    }
    ux /= uv_scale;
    uy /= uv_scale;
    float determinant = ux.x * uy.y - ux.y * uy.x;
    if (!(abs(determinant) > 1e-8)) {
        return normal;
    }
    vec3 tangent = (dx * uy.y - dy * ux.y) * sign(determinant);
    vec3 bitangent = (dy * ux.x - dx * uy.x) * sign(determinant);
    tangent -= normal * dot(normal, tangent);
    float largest = max(max(abs(tangent.x), abs(tangent.y)), abs(tangent.z));
    if (!(largest > 1e-12) || any(isinf(tangent)) || any(isnan(tangent))
        || any(isinf(bitangent)) || any(isnan(bitangent))) {
        return normal;
    }
    tangent = normalize(tangent / largest);
    float handedness = dot(cross(normal, tangent), bitangent) < 0.0 ? -1.0 : 1.0;
    bitangent = cross(normal, tangent) * handedness;
    vec3 sampled = texture(normal_map, uv).xyz * 2.0 - 1.0;
    sampled.xy *= material.emissive_normal_scale.w;
    float sample_scale = max(max(abs(sampled.x), abs(sampled.y)), abs(sampled.z));
    if (!(sample_scale > 0.0)) {
        return normal;
    }
    sampled /= sample_scale;
    vec3 mapped = tangent * sampled.x + bitangent * sampled.y + normal * sampled.z;
    return dot(mapped, mapped) > 1e-12 ? normalize(mapped) : normal;
}

vec3 materialEncodeSrgb(vec3 linear_color) {
    return mix(1.055 * pow(linear_color, vec3(1.0 / 2.4)) - 0.055,
               12.92 * linear_color, lessThanEqual(linear_color, vec3(0.0031308)));
}
