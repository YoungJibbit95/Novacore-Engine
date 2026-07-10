#version 450

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec4 v_light_direction_ambient;
layout(location = 4) in vec4 v_fill_direction_intensity;
layout(location = 5) in vec4 v_material_response;
layout(location = 0) out vec4 out_color;

void main() {
    vec3 normal = normalize(v_normal);
    vec3 lightDir = normalize(v_light_direction_ambient.xyz);
    vec3 fillDir = normalize(v_fill_direction_intensity.xyz);
    float ambient = clamp(v_light_direction_ambient.w, 0.02, 0.95);
    float fill = clamp(v_fill_direction_intensity.w, 0.0, 1.0);
    float rimStrength = clamp(v_material_response.x, 0.0, 1.0);
    float specStrength = clamp(v_material_response.y, 0.0, 1.0);
    float metallic = clamp(v_material_response.z, 0.0, 1.0);
    float roughness = clamp(v_material_response.w, 0.04, 1.0);

    vec2 tiled = v_uv * mix(10.0, 34.0, roughness);
    float weave = sin(tiled.x * 6.28318) * sin(tiled.y * 6.28318);
    float grain = fract(sin(dot(floor(tiled * 2.0), vec2(12.9898, 78.233))) * 43758.5453);
    float textureVariation = mix(0.94, 1.06, grain) + weave * (0.018 + roughness * 0.018);
    vec3 albedo = clamp(v_color.rgb * textureVariation, vec3(0.0), vec3(1.0));

    float keyDiffuse = max(dot(normal, lightDir), 0.0);
    float fillDiffuse = max(dot(normal, fillDir) * 0.5 + 0.5, 0.0) * fill;
    vec3 halfVector = normalize(lightDir + vec3(0.0, 0.22, 0.98));
    float specPower = mix(96.0, 9.0, roughness);
    float specular = pow(max(dot(normal, halfVector), 0.0), specPower) * specStrength;
    vec3 fresnelColor = mix(vec3(0.04), albedo, metallic);
    float rim = pow(1.0 - clamp(abs(normal.z), 0.0, 1.0), 2.4) * rimStrength;
    vec3 diffuseColor = albedo * (1.0 - metallic * 0.72);
    vec3 lit = diffuseColor * (ambient + keyDiffuse * (1.0 - ambient) + fillDiffuse);
    lit += fresnelColor * (specular + rim * 0.28);
    lit = lit / (lit + vec3(1.0));
    out_color = vec4(clamp(lit, vec3(0.0), vec3(1.0)), v_color.a);
}
