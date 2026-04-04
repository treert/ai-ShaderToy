#pragma once

#include <string>
#include <vector>

struct StoyFileData;

// ============================================================
// StoyHlslGenerator
// 从 StoyFileData 为每个 pass 生成完整的 HLSL Pixel Shader 源码。
//
// 生成的 HLSL 结构（每个 pass）：
//   1. cbuffer ShaderToyUniforms : register(b0)  — 固定 208 字节布局
//   2. inner_vars 别名（static float3 iResolution = ...）
//   3. cbuffer TextureParams : register(b1)      — 所有纹理的 _TexelSize
//   4. 外部纹理声明（Texture2D + SamplerState, register t/s）
//   5. Pass 输出纹理声明（Texture2D + SamplerState, register t/s）
//   6. common 代码
//   7. 用户 pass 代码（mainImage 函数）
//   8. main 入口包装（SV_Position → fragCoord, SV_Target0）
// ============================================================

/// 单个 pass 的 HLSL 生成结果
struct StoyPassHlsl {
    std::string passName;       // pass 名称
    std::string hlslSource;     // 完整 HLSL Pixel Shader 源码
};

/// 纹理绑定信息（供 C++ 侧绑定 SRV 使用）
struct StoyTextureBinding {
    std::string name;           // 纹理名 or pass 名
    int registerSlot = -1;      // register(tN) 的 N
    bool isPassOutput = false;  // true = pass 输出纹理, false = 外部纹理
    int passIndex = -1;         // isPassOutput=true 时，对应 passes[] 中的索引
};

/// .stoy HLSL 生成结果
struct StoyHlslResult {
    std::vector<StoyPassHlsl> passHlsls;            // 每个 pass 的 HLSL 源码
    std::vector<StoyTextureBinding> textureBindings; // 所有纹理绑定信息
    int totalTextureSlots = 0;                       // 总共使用的 texture register 槽位数
};

class StoyHlslGenerator {
public:
    /// 从 StoyFileData 生成所有 pass 的 HLSL 源码
    /// @param data 解析后的 .stoy 数据
    /// @return 生成结果
    static StoyHlslResult Generate(const StoyFileData& data);

private:
    /// 生成 cbuffer ShaderToyUniforms : register(b0)
    static std::string GenerateCBufferB0();

    /// 生成 inner_vars 别名
    static std::string GenerateInnerVarsAliases(const std::vector<std::string>& innerVars);

    /// 生成 cbuffer TextureParams : register(b1)
    static std::string GenerateTextureParamsCBuffer(const StoyFileData& data);

    /// 生成纹理/采样器声明
    static std::string GenerateTextureDeclarations(const StoyFileData& data,
                                                    std::vector<StoyTextureBinding>& bindings);

    /// 生成 main 入口包装
    static std::string GenerateMainWrapper(bool isLastPass);
};
