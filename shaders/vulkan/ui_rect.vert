#version 450

layout(push_constant) uniform UiPushConstants {
    vec4 viewport;
    vec4 rect;
    vec4 color;
} pc;

layout(location = 0) out vec4 v_color;

const vec2 k_corners[6] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main() {
    vec2 pixel = pc.rect.xy + (k_corners[gl_VertexIndex] * pc.rect.zw);
    vec2 ndc = vec2(
        (pixel.x / max(pc.viewport.x, 1.0)) * 2.0 - 1.0,
        1.0 - (pixel.y / max(pc.viewport.y, 1.0)) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = pc.color;
}
