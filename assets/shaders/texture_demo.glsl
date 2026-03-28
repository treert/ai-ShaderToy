// Texture channel demo - displays iChannel0 with animated distortion
// Usage: ShaderToyDesktop.exe --shader assets/shaders/texture_demo.glsl --channel0 path/to/image.png

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;

    // Animated wave distortion
    float wave = sin(uv.y * 20.0 + iTime * 3.0) * 0.01;
    uv.x += wave;

    // Sample iChannel0 if available, otherwise show a test pattern
    vec4 texColor = texture(iChannel0, uv);

    // If texture is all black (no texture loaded), show a fallback pattern
    if (texColor.r + texColor.g + texColor.b < 0.001) {
        // Checkerboard pattern
        vec2 grid = floor(uv * 10.0);
        float checker = mod(grid.x + grid.y, 2.0);
        texColor = vec4(vec3(checker * 0.3 + 0.1), 1.0);

        // Add color tint based on position
        texColor.rgb += 0.3 * cos(iTime + uv.xyx + vec3(0.0, 2.0, 4.0));
    }

    fragColor = texColor;
}
