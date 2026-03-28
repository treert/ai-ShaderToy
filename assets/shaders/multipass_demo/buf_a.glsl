// Multi-pass demo: Buffer A — feedback blur accumulator
// iChannel0 = self (Buffer A, previous frame feedback)
// Each frame blends current frame position with previous accumulated result

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;

    // Read previous frame from self (feedback)
    vec4 prev = texture(iChannel0, uv);

    // Generate a moving pattern
    vec2 center = vec2(0.5 + 0.3 * sin(iTime * 0.7), 0.5 + 0.3 * cos(iTime * 0.5));
    float dist = length(uv - center);
    float ring = smoothstep(0.02, 0.0, abs(dist - 0.15));

    vec3 newColor = ring * (0.5 + 0.5 * cos(iTime + uv.xyx * 6.0 + vec3(0, 2, 4)));

    // Blend: 95% previous frame + 5% new (creates trailing/blur effect)
    vec3 result = mix(prev.rgb * 0.98, newColor, 0.15);

    fragColor = vec4(result, 1.0);
}
