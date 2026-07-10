#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 1) in float v_opacity;
layout(location = 2) in float v_softness;
layout(location = 0) out vec4 out_color;

void main() {
    float radial = length(v_uv);
    float inner = mix(0.20, 0.72, v_softness);
    float coverage = 1.0 - smoothstep(inner, 1.0, radial);
    if (coverage <= 0.002) {
        discard;
    }
    float core = mix(coverage * coverage, coverage, v_softness);
    out_color = vec4(0.008, 0.012, 0.016, core * v_opacity);
}
