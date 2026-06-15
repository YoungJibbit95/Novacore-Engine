#version 450

layout(location = 0) out vec2 v_uv;

const vec2 k_positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main() {
    vec2 position = k_positions[gl_VertexIndex];
    v_uv = position * 0.5 + vec2(0.5);
    gl_Position = vec4(position, 0.0, 1.0);
}
