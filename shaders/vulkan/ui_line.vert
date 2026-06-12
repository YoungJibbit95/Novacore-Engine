#version 450

layout(push_constant) uniform UiPushConstants {
    vec4 viewport;
    vec4 line;
    vec4 color;
} pc;

layout(location = 0) out vec4 v_color;

void main() {
    vec2 pixel = gl_VertexIndex == 0 ? pc.line.xy : pc.line.zw;
    vec2 ndc = vec2(
        (pixel.x / max(pc.viewport.x, 1.0)) * 2.0 - 1.0,
        1.0 - (pixel.y / max(pc.viewport.y, 1.0)) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = pc.color;
}
