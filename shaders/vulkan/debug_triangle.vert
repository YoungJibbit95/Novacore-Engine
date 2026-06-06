#version 450

layout(location = 0) out vec3 v_color;

vec2 positions[3] = vec2[](
    vec2(0.0, -0.58),
    vec2(0.58, 0.46),
    vec2(-0.58, 0.46)
);

vec3 colors[3] = vec3[](
    vec3(0.20, 0.86, 1.00),
    vec3(1.00, 0.35, 0.20),
    vec3(0.72, 1.00, 0.36)
);

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    v_color = colors[gl_VertexIndex];
}
