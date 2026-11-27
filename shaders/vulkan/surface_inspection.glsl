// Low two bits match RenderDebugView. Bit 2 requests sRGB encoding on UNORM attachments.
const uint surfaceWorldNormals = 1u;
const uint surfaceBaseColor = 2u;
uint surfaceMode(uint instance) { return instance & 3u; }

vec3 materialEncodeSrgb(vec3 linear_color) {
    return mix(1.055 * pow(linear_color, vec3(1.0 / 2.4)) - 0.055,
               12.92 * linear_color, lessThanEqual(linear_color, vec3(0.0031308)));
}

vec3 inspectSurface(uint instance, vec3 worldNormal, vec3 baseColor) {
    vec3 linear = surfaceMode(instance) == surfaceWorldNormals ? 0.5 * worldNormal + 0.5 : baseColor;
    linear = clamp(linear, vec3(0.0), vec3(1.0));
    if ((instance & 4u) == 0u) return linear;
    return materialEncodeSrgb(linear);
}
