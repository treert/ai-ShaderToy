// shadertoy_uniforms.hlsl — ShaderToy Desktop HLSL 头文件模板
// 用法：在你的 .hlsl 文件中 #include "shadertoy_uniforms.hlsl"，然后只需实现 mainImage()
//
// 示例：
//   #include "shadertoy_uniforms.hlsl"
//   void mainImage(inout float4 fragColor, float2 fragCoord) {
//       float2 uv = fragCoord / iResolution.xy;
//       fragColor = float4(uv, 0.5 + 0.5 * sin(iTime), 1.0);
//   }
//
// 注意：此文件的 cbuffer 布局必须与 C++ ShaderToyConstants 结构体严格对齐（208 字节）

#ifndef SHADERTOY_UNIFORMS_HLSL
#define SHADERTOY_UNIFORMS_HLSL

// ============================================================
// Constant Buffer（与 C++ ShaderToyConstants 严格对齐）
// ============================================================
cbuffer ShaderToyUniforms : register(b0) {
    float4 _iResolution4;           // c0:   xyz=resolution, w=padding
    float  iTime;                   // c1.x
    float  iTimeDelta;              // c1.y
    int    iFrame;                  // c1.z
    float  iFrameRate;              // c1.w
    float4 iMouse;                  // c2:   xy=current pos, zw=click pos
    float4 iDate;                   // c3:   x=year, y=month, z=day, w=seconds
    float  iSampleRate;             // c4.x
    float  iClickTime;              // c4.y
    float2 _pad0;                   // c4.zw: padding
    float4 _iChannelTime;           // c5:   各通道播放时间
    float4 _iChannelResolution[4];  // c6-c9: 各通道分辨率 (xyz + padding)
    float4 _cubeFaceRight;          // c10:  CubeMap pass 用
    float4 _cubeFaceUp;             // c11:  CubeMap pass 用
    float4 _cubeFaceDir;            // c12:  CubeMap pass 用
};

// ============================================================
// ShaderToy 标准变量别名
// ============================================================
static float3 iResolution;
static float  iChannelTime[4];
static float3 iChannelResolution[4];

// ============================================================
// 纹理与采样器（iChannel0 ~ iChannel3）
// 默认全部为 Texture2D，如需 TextureCube 请自行替换声明
// ============================================================
#ifndef ICHANNEL0_TYPE
#define ICHANNEL0_TYPE Texture2D<float4>
#endif
#ifndef ICHANNEL1_TYPE
#define ICHANNEL1_TYPE Texture2D<float4>
#endif
#ifndef ICHANNEL2_TYPE
#define ICHANNEL2_TYPE Texture2D<float4>
#endif
#ifndef ICHANNEL3_TYPE
#define ICHANNEL3_TYPE Texture2D<float4>
#endif

ICHANNEL0_TYPE iChannel0 : register(t0);
ICHANNEL1_TYPE iChannel1 : register(t1);
ICHANNEL2_TYPE iChannel2 : register(t2);
ICHANNEL3_TYPE iChannel3 : register(t3);

SamplerState _iChannel0_sampler : register(s0);
SamplerState _iChannel1_sampler : register(s1);
SamplerState _iChannel2_sampler : register(s2);
SamplerState _iChannel3_sampler : register(s3);

// ============================================================
// 用户需实现的入口函数（前向声明）
// ============================================================
void mainImage(inout float4 fragColor, float2 fragCoord);

// ============================================================
// PS 入口（自动调用 mainImage，处理坐标转换）
// ============================================================

// 是否翻转 Y 轴（Image pass 需要翻转，Buffer pass 不翻转）
// 用户可在 #include 之前 #define SHADERTOY_FLIP_Y 0 来禁用翻转
#ifndef SHADERTOY_FLIP_Y
#define SHADERTOY_FLIP_Y 1
#endif

float4 main(float4 pos : SV_Position) : SV_Target0 {
    // 从 cbuffer 初始化全局变量
    iResolution = _iResolution4.xyz;
    iChannelTime[0] = _iChannelTime.x;
    iChannelTime[1] = _iChannelTime.y;
    iChannelTime[2] = _iChannelTime.z;
    iChannelTime[3] = _iChannelTime.w;
    iChannelResolution[0] = _iChannelResolution[0].xyz;
    iChannelResolution[1] = _iChannelResolution[1].xyz;
    iChannelResolution[2] = _iChannelResolution[2].xyz;
    iChannelResolution[3] = _iChannelResolution[3].xyz;

    // 坐标转换：SV_Position → ShaderToy fragCoord
    float2 fragCoord = pos.xy;
#if SHADERTOY_FLIP_Y
    fragCoord.y = iResolution.y - fragCoord.y;
#endif

    float4 fragColor = float4(0.0, 0.0, 0.0, 1.0);
    mainImage(fragColor, fragCoord);
    return fragColor;
}

#endif // SHADERTOY_UNIFORMS_HLSL
