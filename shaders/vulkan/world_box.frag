#version 450
#extension GL_GOOGLE_include_directive : require
#include "surface_inspection.glsl"

layout(location = 0) in vec4 v_color;
layout(location = 2) in vec3 v_world_normal;
layout(location = 4) flat in uint v_inspection;
layout(location = 5) flat in vec3 v_base_color;
layout(location = 0) out vec4 out_color;

void main() {
    if (v_inspection != 0u) {
        out_color = vec4(inspectSurface(v_inspection, v_world_normal, v_base_color), v_color.a);
        return;
    }
    out_color = v_color;
}
