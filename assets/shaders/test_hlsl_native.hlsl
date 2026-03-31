// test_hlsl_native.hlsl — HLSL 原生模式测试 shader
#include "shadertoy_uniforms.hlsl"

void mainImage(inout float4 fragColor, float2 fragCoord) {
    float2 uv = fragCoord / iResolution.xy;

    // 动态渐变色
    float3 col = 0.5 + 0.5 * cos(iTime + uv.xyx + float3(0, 2, 4));

    // 鼠标交互：鼠标位置画一个圆
    float2 mouse = iMouse.xy / iResolution.xy;
    float d = length(uv - mouse);
    if (d < 0.05) {
        col = float3(1.0, 1.0, 1.0);
    }

    fragColor = float4(col, 1.0);
}
