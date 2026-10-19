layout(set = 1, binding = 0, std140) uniform MaterialFrame {
    vec4 right;
    vec4 down;
    vec4 forward;
    vec4 eye;
    vec4 parameters;
} material_frame;

vec3 materialWorldPosition(vec4 camera_clip) {
    return material_frame.eye.xyz + material_frame.right.xyz * camera_clip.x
        + material_frame.down.xyz * camera_clip.y + material_frame.forward.xyz * camera_clip.w;
}
