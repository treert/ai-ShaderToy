// Multi-pass demo: Image pass — display Buffer A output with post-processing
// iChannel0 = Buffer A output

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;

    // Read from Buffer A
    vec3 col = texture(iChannel0, uv).rgb;

    // Simple vignette
    float vig = 1.0 - 0.4 * length(uv - 0.5);
    col *= vig;

    // Gamma correction
    col = pow(col, vec3(0.9));

    fragColor = vec4(col, 1.0);
}
