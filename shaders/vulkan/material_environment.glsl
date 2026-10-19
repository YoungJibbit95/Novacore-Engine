// Khronos split-sum GGX specular IBL. Scene-global sky radiance only; no
// diffuse irradiance, local-object reflection, transmission, or solar disc.
layout(set = 2, binding = 0) uniform samplerCube sky_environment;
layout(set = 2, binding = 1) uniform sampler2D sky_brdf;

vec3 materialSkyEnvironment(vec3 normal, vec3 view, vec3 albedo, float metallic, float roughness) {
    float nv = clamp(dot(normal, view), 0.0, 1.0);
    // A coincident eye produces zero V; back-facing/invalid pairs have no
    // reflected contribution. Never sample a cube with a zero direction.
    if (!(nv > 0.0)) {
        return vec3(0.0);
    }
    vec3 reflection = reflect(-view, normal);
    float maximumMip = material_frame.parameters.z;
    vec3 radiance = textureLod(sky_environment, reflection, roughness * maximumMip).rgb;
    vec2 ab = texture(sky_brdf, vec2(nv, roughness)).rg;
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    return radiance * (f0 * ab.x + ab.y);
}
