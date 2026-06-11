#version 450

layout(push_constant) uniform LinePushConstants {
    mat4 view_projection;
    vec4 start;
    vec4 end;
    vec4 color;
} pc;

layout(location = 0) out vec4 v_color;

void main() {
    vec3 position = gl_VertexIndex == 0 ? pc.start.xyz : pc.end.xyz;
    gl_Position = pc.view_projection * vec4(position, 1.0);
    v_color = pc.color;
}
