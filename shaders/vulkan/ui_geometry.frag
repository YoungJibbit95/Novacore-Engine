#version 450
layout(set=0,binding=0) uniform sampler2D image;
layout(location=0) in vec2 uv;
layout(location=1) in vec4 tint;
layout(location=0) out vec4 outputColor;
layout(push_constant) uniform View { vec2 size; int srgb; } view;
void main() {
    vec4 color = tint * texture(image, uv);
    // RmlUi supplies sRGB colors premultiplied in sRGB space. SRGB attachments
    // encode on write, so convert straight RGB to linear before blending.
    if (view.srgb != 0 && color.a > 0.0) {
        vec3 straight = clamp(color.rgb / color.a, 0.0, 1.0);
        vec3 linear = mix(pow((straight + 0.055) / 1.055, vec3(2.4)),
            straight / 12.92, lessThanEqual(straight, vec3(0.04045)));
        color.rgb = linear * color.a;
    }
    outputColor = color;
}
