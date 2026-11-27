#version 450

layout(push_constant) uniform UiPushConstants {
    vec2 viewport;
    uvec2 glyphRows;
    vec4 rect;
    vec4 color;
} pc;

layout(location = 0) out vec4 v_color;

const vec2 k_corners[6] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main() {
    precise vec2 origin = pc.rect.xy;
    if ((pc.glyphRows.x | pc.glyphRows.y) != 0u) {
        uint col = uint(gl_InstanceIndex) % 5u;
        uint row = uint(gl_InstanceIndex) / 5u;
        uint bit = 5u * (row % 4u) + (4u - col);
        if ((pc.glyphRows[row / 4u] & (1u << bit)) == 0u) {
            gl_Position = vec4(-2.0, -2.0, 0.0, 1.0);
            v_color = pc.color;
            return;
        }
        // Each lit cell keeps the same rasterized edges as the original quad.
        origin += vec2(col, row) * pc.rect.zw;
    }
    vec2 pixel = origin + (k_corners[gl_VertexIndex] * pc.rect.zw);
    vec2 ndc = vec2(
        (pixel.x / max(pc.viewport.x, 1.0)) * 2.0 - 1.0,
        (pixel.y / max(pc.viewport.y, 1.0)) * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = pc.color;
}
