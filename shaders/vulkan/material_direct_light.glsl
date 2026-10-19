// Opaque isotropic glTF metallic/roughness direct-light approximation.
// Formula references: glTF 2.0 Appendix B and Khronos glTF-Sample-Renderer
// source/Renderer/shaders/brdf.glsl (GGX distribution, correlated Smith,
// Schlick Fresnel, Lambert diffuse). No environment or multiple scattering.
// Audited reference revisions: glTF 521a91d491f02bb7db7d445b35f591d09d2a64ce;
// Sample Renderer 74aaf40853dd92c554101e4f9f17ba1a13d12367. See renderer docs.

vec3 materialSafeDirection(vec3 direction) {
    float scale = max(max(abs(direction.x), abs(direction.y)), abs(direction.z));
    if (!(scale > 0.0) || any(isnan(direction)) || any(isinf(direction))) {
        return vec3(0.0);
    }
    return normalize(direction / scale);
}

// Includes the incident cosine; caller supplies linear incoming radiance.
vec3 materialDirectLight(vec3 normal, vec3 view, vec3 light, vec3 albedo,
                         float metallic, float perceptual_roughness) {
    float nv = clamp(dot(normal, view), 0.0, 1.0);
    float nl = clamp(dot(normal, light), 0.0, 1.0);
    if (!(nv > 0.0) || !(nl > 0.0)) {
        return vec3(0.0);
    }
    vec3 half_direction = materialSafeDirection(view + light);
    if (!(dot(half_direction, half_direction) > 0.0)) {
        return vec3(0.0);
    }
    float nh = clamp(dot(normal, half_direction), 0.0, 1.0);
    float vh = clamp(dot(view, half_direction), 0.0, 1.0);
    float roughness = clamp(perceptual_roughness, 0.04, 1.0);
    float alpha = roughness * roughness;
    float alpha_squared = alpha * alpha;
    float nh_squared = nh * nh;
    // This equivalent form avoids subtracting nearly equal values at nh=1.
    float distribution_denominator = (1.0 - nh_squared) + nh_squared * alpha_squared;
    const float pi = 3.14159265358979323846;
    float distribution = alpha_squared /
        (pi * distribution_denominator * distribution_denominator);
    float smith_v = nl * sqrt(nv * nv * (1.0 - alpha_squared) + alpha_squared);
    float smith_l = nv * sqrt(nl * nl * (1.0 - alpha_squared) + alpha_squared);
    float smith_sum = smith_v + smith_l;
    if (!(smith_sum > 0.0)) {
        return vec3(0.0);
    }
    // Fold nl into visibility before multiplication, avoiding a grazing-angle
    // intermediate overflow from V=0.5/smith_sum followed by nl*V.
    float visibility_cosine = (0.5 * nl) / smith_sum;
    float fresnel_weight = pow(1.0 - vh, 5.0);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = f0 + (vec3(1.0) - f0) * fresnel_weight;
    float dielectric_fresnel = 0.04 + 0.96 * fresnel_weight;
    vec3 diffuse = (1.0 - metallic) * (1.0 - dielectric_fresnel) * albedo / pi;
    return diffuse * nl + fresnel * distribution * visibility_cosine;
}
