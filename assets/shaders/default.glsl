// Default shader - colorful animated gradient with click ripple effect
// Compatible with ShaderToy format
// Custom uniform: iClickTime (time of last mouse click)

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;

    // 背景：流动的彩色渐变
    vec3 col = 0.5 + 0.5 * cos(iTime + uv.xyx + vec3(0.0, 2.0, 4.0));

    // 点击涟漪效果
    float clickAge = iTime - iClickTime;  // 点击后经过的秒数
    if (clickAge < 2.0 && iMouse.z > 0.0 || (clickAge < 2.0 && clickAge > 0.0)) {
        // 点击位置（像素坐标）
        vec2 clickPos = abs(iMouse.zw);
        vec2 clickUV = clickPos / iResolution.xy;

        // 到点击位置的距离
        float aspectRatio = iResolution.x / iResolution.y;
        vec2 delta = uv - clickUV;
        delta.x *= aspectRatio;
        float dist = length(delta);

        // 涟漪：向外扩散的环
        float rippleRadius = clickAge * 0.4;  // 扩散速度
        float rippleWidth = 0.02 + clickAge * 0.01;  // 环宽度随时间变粗
        float ripple = smoothstep(rippleWidth, 0.0, abs(dist - rippleRadius));

        // 中心光晕
        float glow = exp(-dist * 8.0);

        // 淡出
        float fade = 1.0 - smoothstep(0.0, 2.0, clickAge);
        fade = fade * fade;  // 二次淡出，更自然

        // 涟漪颜色：亮白色 + 轻微色彩
        vec3 rippleColor = vec3(1.0, 0.9, 0.8);
        col += rippleColor * (ripple * 0.8 + glow * 0.5) * fade;
    }

    fragColor = vec4(col, 1.0);
}
