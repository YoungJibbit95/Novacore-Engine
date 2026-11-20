#version 450

layout(push_constant) uniform UiPushConstants {
    vec2 viewport;
    uvec2 glyphRows;
    vec4 bounds;
    vec4 color;
} pc;

layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 out_color;

void main() {
    if ((pc.glyphRows.x | pc.glyphRows.y) != 0u) {
        // Glyphs use a top-left origin and five high-to-low bits per row.
        vec2 local = (gl_FragCoord.xy - pc.bounds.xy) / pc.bounds.zw;
        uvec2 cell = uvec2(clamp(floor(local * vec2(5, 7)), vec2(0), vec2(4, 6)));
        uint bit = 5u * (cell.y % 4u) + (4u - cell.x);
        if ((pc.glyphRows[cell.y / 4u] & (1u << bit)) == 0u) {
            discard;
        }
    }
    out_color = v_color;
}
