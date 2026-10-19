
layout(location = 0) in vec4 v_color;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec4 v_light_direction_ambient;
layout(location = 4) in vec4 v_fill_direction_intensity;
layout(location = 5) in vec4 v_material_response;
layout(location = 0) out vec4 out_color;
#ifdef DIRECTIONAL_SHADOW_RECEIVER
#include "shadow_data.glsl"
#include "shadow_sampling.glsl"
#endif
#if defined(DIRECTIONAL_SHADOW_RECEIVER) || defined(MATERIAL_TEXTURES)
layout(location = 6) in vec3 v_world_position;
#endif
#ifdef MATERIAL_TEXTURES
#include "material_frame.glsl"
#include "material_textures.glsl"
#include "material_direct_light.glsl"
layout(location = 7) in vec2 v_uv1;
#endif

void main() {
    // Vertex normals, key direction and fill direction all use world space.
    vec3 normal = normalize(v_normal);
    vec3 lightDir = normalize(v_light_direction_ambient.xyz);
    vec3 fillDir = normalize(v_fill_direction_intensity.xyz);
    float ambient = clamp(v_light_direction_ambient.w, 0.02, 0.95);
    float fill = clamp(v_fill_direction_intensity.w, 0.0, 1.0);
#ifndef MATERIAL_TEXTURES
    float rimStrength = clamp(v_material_response.x, 0.0, 1.0);
    float specStrength = clamp(v_material_response.y, 0.0, 1.0);
#endif
    float metallic = clamp(v_material_response.z, 0.0, 1.0);
    float roughness = clamp(v_material_response.w, 0.04, 1.0);
#ifdef MATERIAL_TEXTURES
    vec2 uv_base = materialUv(material.uv_sets.x, v_uv, v_uv1);
    vec2 uv_mr = materialUv(material.uv_sets.y, v_uv, v_uv1);
    vec2 uv_normal = materialUv(material.uv_sets.z, v_uv, v_uv1);
    vec2 uv_emissive = materialUv(material.uv_sets.w, v_uv, v_uv1);
    normal = materialNormal(v_world_position, normal, uv_normal);
    if (material.enabled_maps.y > 0.5) {
        vec4 mr = texture(metallic_roughness_map, uv_mr);
        metallic = clamp(metallic * mr.b, 0.0, 1.0);
        roughness = clamp(roughness * mr.g, 0.04, 1.0);
    }
#endif

    vec2 tiled = v_uv * mix(10.0, 34.0, roughness);
    float weave = sin(tiled.x * 6.28318) * sin(tiled.y * 6.28318);
    float grain = fract(sin(dot(floor(tiled * 2.0), vec2(12.9898, 78.233))) * 43758.5453);
    float textureVariation = mix(0.94, 1.06, grain) + weave * (0.018 + roughness * 0.018);
    vec3 albedo = clamp(v_color.rgb * textureVariation, vec3(0.0), vec3(1.0));
#ifdef MATERIAL_TEXTURES
    if (material.enabled_maps.x > 0.5) {
        albedo = clamp(v_color.rgb * texture(base_color_map, uv_base).rgb, vec3(0.0), vec3(1.0));
    }
#endif

#ifdef MATERIAL_TEXTURES
    vec3 view = materialSafeDirection(material_frame.eye.xyz - v_world_position);
    float keyVisibility = 1.0;
#ifdef DIRECTIONAL_SHADOW_RECEIVER
    keyVisibility = directionalVisibility(v_world_position, normal);
#endif
    vec3 lit = materialDirectLight(normal, view, lightDir, albedo, metallic, roughness)
        * (1.0 - ambient) * keyVisibility;
    lit += materialDirectLight(normal, view, fillDir, albedo, metallic, roughness) * fill;
    // Existing unitless ambient control remains a diffuse approximation.
    // There is no fabricated metallic environment response or artistic rim.
    lit += albedo * (1.0 - metallic) * ambient;
#else
    float keyDiffuse = max(dot(normal, lightDir), 0.0);
#ifdef DIRECTIONAL_SHADOW_RECEIVER
    float keyVisibility = directionalVisibility(v_world_position, normal);
    keyDiffuse *= keyVisibility;
#endif
    float fillDiffuse = max(dot(normal, fillDir) * 0.5 + 0.5, 0.0) * fill;
    vec3 halfVector = normalize(lightDir + vec3(0.0, 0.22, 0.98));
    float specPower = mix(96.0, 9.0, roughness);
    float specular = pow(max(dot(normal, halfVector), 0.0), specPower) * specStrength;
#ifdef DIRECTIONAL_SHADOW_RECEIVER
    specular *= keyVisibility;
#endif
    vec3 fresnelColor = mix(vec3(0.04), albedo, metallic);
    float rim = pow(1.0 - clamp(abs(normal.z), 0.0, 1.0), 2.4) * rimStrength;
    vec3 diffuseColor = albedo * (1.0 - metallic * 0.72);
    vec3 lit = diffuseColor * (ambient + keyDiffuse * (1.0 - ambient) + fillDiffuse);
    lit += fresnelColor * (specular + rim * 0.28);
#endif
#ifdef MATERIAL_TEXTURES
    vec3 emissive = material.emissive_normal_scale.rgb;
    if (material.enabled_maps.w > 0.5) {
        emissive *= texture(emissive_map, uv_emissive).rgb;
    }
    lit += emissive;
#endif
    lit = lit / (lit + vec3(1.0));
#ifdef MATERIAL_TEXTURES
    if (material_frame.parameters.x > 0.5) {
        lit = materialEncodeSrgb(clamp(lit, vec3(0.0), vec3(1.0)));
    }
#endif
    out_color = vec4(clamp(lit, vec3(0.0), vec3(1.0)), v_color.a);
}
