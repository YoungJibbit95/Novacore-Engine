layout(set = 0, binding = 0, std140) uniform DirectionalShadowData {
    mat4 light_view_projection;
    vec4 camera_right;
    vec4 camera_down;
    vec4 camera_forward;
    vec4 camera_position_distance;
    vec4 parameters;
    vec4 toward_sun;
} shadow;

vec3 shadowWorldPosition(vec4 camera_clip) {
    return shadow.camera_position_distance.xyz + shadow.camera_right.xyz * camera_clip.x
        + shadow.camera_down.xyz * camera_clip.y + shadow.camera_forward.xyz * camera_clip.w;
}
