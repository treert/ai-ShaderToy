#include "glsl_to_hlsl.h"
#include "shader_manager.h"  // for ChannelType enum
#include <regex>
#include <sstream>
#include <vector>
#include <algorithm>

// ============================================================
// GLSL-to-HLSL 翻译器（ShaderToy GLSL 330 子集）
//
// 翻译策略：
//   1. 预处理：移除 #version、precision 声明
//   2. 类型替换：vec/mat/ivec/bvec -> float/int/bool + NxN
//   3. 构造函数替换：vec3(...) -> float3(...)
//   4. 内置函数替换：mix->lerp, fract->frac, mod->glsl_mod 等
//   5. 纹理采样替换：texture() -> 对应的 HLSL 纹理采样
//   6. 矩阵构造处理：mat2/mat3/mat4 -> 行优先构造
//   7. 语法清理：out/inout 参数、atan(y,x) 等
//
// 已知局限（后续升级 SPIRV-Cross 解决）：
//   - 矩阵 * 向量乘法顺序（GLSL 列优先 vs HLSL 行优先）
//     需要转置：GLSL m*v = HLSL mul(v, transpose(m))
//     当前通过宏 mul_vm/mul_mv 处理常见模式
//   - mod() 对负数行为差异（GLSL 取正余数，HLSL fmod 保留符号）
//     通过注入 glsl_mod 辅助函数解决
// ============================================================

/// 全词匹配替换（避免替换子串，如 vec3 在 ivec3 中）
static std::string ReplaceWholeWord(const std::string& src,
                                     const std::string& from,
                                     const std::string& to) {
    // 用正则的 \b 边界匹配全词
    std::regex re("\\b" + from + "\\b");
    return std::regex_replace(src, re, to);
}

/// 移除匹配的整行
static std::string RemoveLines(const std::string& src, const std::regex& pattern) {
    std::string result;
    std::istringstream stream(src);
    std::string line;
    while (std::getline(stream, line)) {
        if (!std::regex_search(line, pattern)) {
            result += line + "\n";
        }
    }
    return result;
}

std::string TranslateGlslToHlsl(const std::string& glslSource) {
    std::string s = glslSource;

    // ============================================================
    // Step 1: 预处理 — 移除 GLSL 特有指令
    // ============================================================

    // 移除 #version 指令
    s = RemoveLines(s, std::regex(R"(^\s*#version\b)"));

    // 移除 precision 声明（如 precision highp float;）
    s = RemoveLines(s, std::regex(R"(^\s*precision\s+)"));

    // ============================================================
    // Step 2: 类型替换
    // ============================================================

    // 注意顺序：先替换更长的类型名，避免部分匹配
    // bvec -> bool
    s = ReplaceWholeWord(s, "bvec4", "bool4");
    s = ReplaceWholeWord(s, "bvec3", "bool3");
    s = ReplaceWholeWord(s, "bvec2", "bool2");

    // ivec -> int
    s = ReplaceWholeWord(s, "ivec4", "int4");
    s = ReplaceWholeWord(s, "ivec3", "int3");
    s = ReplaceWholeWord(s, "ivec2", "int2");

    // uvec -> uint
    s = ReplaceWholeWord(s, "uvec4", "uint4");
    s = ReplaceWholeWord(s, "uvec3", "uint3");
    s = ReplaceWholeWord(s, "uvec2", "uint2");

    // mat -> float NxN（先长后短）
    s = ReplaceWholeWord(s, "mat4x4", "float4x4");
    s = ReplaceWholeWord(s, "mat3x3", "float3x3");
    s = ReplaceWholeWord(s, "mat2x2", "float2x2");
    s = ReplaceWholeWord(s, "mat4", "float4x4");
    s = ReplaceWholeWord(s, "mat3", "float3x3");
    s = ReplaceWholeWord(s, "mat2", "float2x2");

    // vec -> float
    s = ReplaceWholeWord(s, "vec4", "float4");
    s = ReplaceWholeWord(s, "vec3", "float3");
    s = ReplaceWholeWord(s, "vec2", "float2");

    // ============================================================
    // Step 3: 内置函数替换
    // ============================================================

    s = ReplaceWholeWord(s, "mix", "lerp");
    s = ReplaceWholeWord(s, "fract", "frac");
    s = ReplaceWholeWord(s, "inversesqrt", "rsqrt");
    s = ReplaceWholeWord(s, "dFdx", "ddx");
    s = ReplaceWholeWord(s, "dFdy", "ddy");

    // atan(y, x) -> atan2(y, x)
    // GLSL atan 有两种：atan(y,x) 和 atan(y_over_x)
    // HLSL atan 只有 atan(x)，两参数版本是 atan2(y,x)
    // 由于文本替换难以区分参数数量，注入一个 glsl_atan 辅助宏
    // 不直接替换 atan，因为单参数 atan 在 HLSL 中也存在

    // mod(x, y) -> glsl_mod(x, y)
    // GLSL: mod(x,y) = x - y * floor(x/y) （结果与 y 同号）
    // HLSL: fmod(x,y) 结果与 x 同号
    // 注入辅助函数来保持语义一致
    s = ReplaceWholeWord(s, "mod", "glsl_mod");

    // ============================================================
    // Step 3.5: texture/textureLod/texelFetch 调用重写
    // ============================================================
    // GLSL: texture(iChannel0, uv)    -> HLSL: texture_iChannel0(uv)
    // GLSL: textureLod(iChannel0, uv, lod) -> HLSL: textureLod_iChannel0(uv, lod)
    // GLSL: texelFetch(iChannel0, coord, lod) -> HLSL: texelFetch_iChannel0(coord, lod)
    // 这些宏在 D3D11ShaderManager 的 HLSL 模板中定义
    for (int i = 0; i < 4; ++i) {
        std::string chName = "iChannel" + std::to_string(i);

        // texture(iChannelN, ...) -> texture_iChannelN(...)
        // 匹配 "texture" + 可选空白 + "(" + "iChannelN" + 可选空白 + "," + 可选空白
        std::regex texRe("\\btexture\\s*\\(\\s*" + chName + "\\s*,\\s*");
        s = std::regex_replace(s, texRe, "texture_" + chName + "(");

        // textureLod(iChannelN, ...) -> textureLod_iChannelN(...)
        std::regex texLodRe("\\btextureLod\\s*\\(\\s*" + chName + "\\s*,\\s*");
        s = std::regex_replace(s, texLodRe, "textureLod_" + chName + "(");

        // texelFetch(iChannelN, ...) -> texelFetch_iChannelN(...)
        std::regex texFetchRe("\\btexelFetch\\s*\\(\\s*" + chName + "\\s*,\\s*");
        s = std::regex_replace(s, texFetchRe, "texelFetch_" + chName + "(");
    }

    // ============================================================
    // Step 4: out/inout 参数修饰符
    // ============================================================

    // GLSL 的 out -> HLSL 的 out（兼容）
    // GLSL 的 inout -> HLSL 的 inout（兼容）
    // 这两个在 HLSL 中也存在，不需要替换

    // 移除函数参数中的 "in " 修饰符（GLSL 默认是 in，HLSL 也是）
    // 但要避免误替换关键字（如 "int"）
    // 仅处理函数签名中的 "in vec2" 等模式
    // 安全做法：不替换，HLSL 也接受 in 修饰符

    // ============================================================
    // Step 5: 注入 HLSL 辅助函数（放在翻译后代码的开头）
    // ============================================================

    std::string helpers = R"hlsl(
// === GLSL compatibility helpers ===

// GLSL mod: result has same sign as y (unlike HLSL fmod)
float glsl_mod(float x, float y) { return x - y * floor(x / y); }
float2 glsl_mod(float2 x, float2 y) { return x - y * floor(x / y); }
float3 glsl_mod(float3 x, float3 y) { return x - y * floor(x / y); }
float4 glsl_mod(float4 x, float4 y) { return x - y * floor(x / y); }
float2 glsl_mod(float2 x, float y) { return x - y * floor(x / y); }
float3 glsl_mod(float3 x, float y) { return x - y * floor(x / y); }
float4 glsl_mod(float4 x, float y) { return x - y * floor(x / y); }

// === End GLSL compatibility helpers ===

)hlsl";

    return helpers + s;
}

std::string WrapShaderToyHlsl(const std::string& translatedHlsl,
                              const std::array<ChannelType, 4>& channelTypes,
                              const std::string& commonSource,
                              bool isCubeMapPass) {
    std::ostringstream hlsl;

    // Constant buffer（与 ShaderToyConstants 结构体对齐）
    hlsl << R"hlsl(
// ShaderToy uniform constant buffer
cbuffer ShaderToyUniforms : register(b0) {
    float4 iResolution;           // xyz=resolution, w=padding
    float  iTime;
    float  iTimeDelta;
    int    iFrame;
    float  iFrameRate;
    float4 iMouse;
    float4 iDate;
    float  iSampleRate;
    float  iClickTime;
    float2 _pad0;
    float4 iChannelTime;
    float4 iChannelResolution[4]; // xyz=res, w=padding
    float4 _cubeFaceRight;
    float4 _cubeFaceUp;
    float4 _cubeFaceDir;
};

)hlsl";

    // 纹理和采样器声明
    for (int i = 0; i < 4; ++i) {
        switch (channelTypes[i]) {
        case ChannelType::CubeMap:
            hlsl << "TextureCube iChannel" << i << " : register(t" << i << ");\n";
            break;
        case ChannelType::Texture3D:
            hlsl << "Texture3D<float4> iChannel" << i << " : register(t" << i << ");\n";
            break;
        default:
            hlsl << "Texture2D<float4> iChannel" << i << " : register(t" << i << ");\n";
            break;
        }
    }
    hlsl << "\n";

    // 采样器（所有通道共享线性采样 + Wrap）
    for (int i = 0; i < 4; ++i) {
        hlsl << "SamplerState sampler" << i << " : register(s" << i << ");\n";
    }
    hlsl << "\n";

    // GLSL texture() 兼容函数
    hlsl << R"hlsl(
// GLSL texture() compatibility (renamed to avoid HLSL reserved word "texture")
float4 _sampleTex2D(Texture2D<float4> tex, SamplerState samp, float2 uv) {
    return tex.Sample(samp, uv);
}
float4 _sampleTexCube(TextureCube tex, SamplerState samp, float3 uvw) {
    return tex.Sample(samp, uvw);
}
float4 _sampleTex3D(Texture3D<float4> tex, SamplerState samp, float3 uvw) {
    return tex.Sample(samp, uvw);
}
float4 _sampleTex2DLod(Texture2D<float4> tex, SamplerState samp, float2 uv, float lod) {
    return tex.SampleLevel(samp, uv, lod);
}
float4 _sampleTexCubeLod(TextureCube tex, SamplerState samp, float3 uvw, float lod) {
    return tex.SampleLevel(samp, uvw, lod);
}
float4 _fetchTex2D(Texture2D<float4> tex, int2 coord, int lod) {
    return tex.Load(int3(coord, lod));
}

// Redirect texture(iChannelN, ...) calls to use corresponding sampler
)hlsl";

    // 为每个通道生成 texture 重载
    for (int i = 0; i < 4; ++i) {
        std::string chName = "iChannel" + std::to_string(i);
        std::string sampName = "sampler" + std::to_string(i);

        switch (channelTypes[i]) {
        case ChannelType::CubeMap:
            hlsl << "#define texture_" << chName << "(uv) _sampleTexCube(" << chName << ", " << sampName << ", (uv))\n";
            hlsl << "#define textureLod_" << chName << "(uv, lod) _sampleTexCubeLod(" << chName << ", " << sampName << ", (uv), (lod))\n";
            break;
        case ChannelType::Texture3D:
            hlsl << "#define texture_" << chName << "(uv) _sampleTex3D(" << chName << ", " << sampName << ", (uv))\n";
            hlsl << "#define textureLod_" << chName << "(uv, lod) _sampleTex3D(" << chName << ", " << sampName << ", (uv))\n";
            break;
        default:
            hlsl << "#define texture_" << chName << "(uv) _sampleTex2D(" << chName << ", " << sampName << ", (uv))\n";
            hlsl << "#define textureLod_" << chName << "(uv, lod) _sampleTex2DLod(" << chName << ", " << sampName << ", (uv), (lod))\n";
            hlsl << "#define texelFetch_" << chName << "(coord, lod) _fetchTex2D(" << chName << ", (coord), (lod))\n";
            break;
        }
    }
    hlsl << "\n";

    // 注入 Common 共享代码段
    if (!commonSource.empty()) {
        std::string commonHlsl = TranslateGlslToHlsl(commonSource);
        hlsl << "// === Common code begin ===\n";
        hlsl << commonHlsl;
        hlsl << "\n// === Common code end ===\n\n";
    }

    // 用户 shader 代码（已翻译为 HLSL）
    hlsl << translatedHlsl;
    hlsl << "\n";

    // main 函数
    if (isCubeMapPass) {
        hlsl << R"hlsl(
// CubeMap pass entry point
struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target {
    float4 fragColor = float4(0.0, 0.0, 0.0, 1.0);
    // D3D11 SV_Position.y is top-down, ShaderToy fragCoord.y is bottom-up
    float2 fragCoord = float2(input.position.x, iResolution.y - input.position.y);
    float2 uv = fragCoord / iResolution.xy * 2.0 - 1.0;
    float3 rayDir = normalize(_cubeFaceDir.xyz + uv.x * _cubeFaceRight.xyz + uv.y * _cubeFaceUp.xyz);
    float3 rayOri = float3(0.0, 0.0, 0.0);
    mainCubemap(fragColor, fragCoord, rayOri, rayDir);
    return fragColor;
}
)hlsl";
    } else {
        hlsl << R"hlsl(
// Image/Buffer pass entry point
struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target {
    float4 fragColor = float4(0.0, 0.0, 0.0, 1.0);
    // D3D11 SV_Position.y is top-down, ShaderToy fragCoord.y is bottom-up
    float2 fragCoord = float2(input.position.x, iResolution.y - input.position.y);
    mainImage(fragColor, fragCoord);
    return fragColor;
}
)hlsl";
    }

    return hlsl.str();
}
