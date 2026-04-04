#include "stoy_hlsl_generator.h"
#include "stoy_parser.h"

#include <sstream>

// ============================================================
// cbuffer ShaderToyUniforms : register(b0)
// 必须与 C++ ShaderToyConstants 结构体严格对齐（208 字节）
// ============================================================

std::string StoyHlslGenerator::GenerateCBufferB0() {
    return R"(cbuffer ShaderToyUniforms : register(b0) {
    float4 _iResolution4;       // xyz=resolution, w=padding
    float  _iTime;
    float  _iTimeDelta;
    int    _iFrame;
    float  _iFrameRate;
    float4 _iMouse;
    float4 _iDate;
    float  _iSampleRate;
    float  _iClickTime;
    float2 _pad0;
    float4 _iChannelTime;
    float4 _iChannelResolution[4];
    float4 _cubeFaceRight;
    float4 _cubeFaceUp;
    float4 _cubeFaceDir;
};
)";
}

// ============================================================
// inner_vars 别名
// ============================================================

std::string StoyHlslGenerator::GenerateInnerVarsAliases(const std::vector<std::string>& innerVars) {
    if (innerVars.empty()) return "";

    std::ostringstream ss;
    ss << "// --- inner_vars aliases ---\n";
    for (const auto& var : innerVars) {
        if (var == "iResolution")
            ss << "static float3 iResolution = _iResolution4.xyz;\n";
        else if (var == "iTime")
            ss << "static float iTime = _iTime;\n";
        else if (var == "iTimeDelta")
            ss << "static float iTimeDelta = _iTimeDelta;\n";
        else if (var == "iFrame")
            ss << "static int iFrame = _iFrame;\n";
        else if (var == "iFrameRate")
            ss << "static float iFrameRate = _iFrameRate;\n";
        else if (var == "iMouse")
            ss << "static float4 iMouse = _iMouse;\n";
        else if (var == "iDate")
            ss << "static float4 iDate = _iDate;\n";
        else if (var == "iSampleRate")
            ss << "static float iSampleRate = _iSampleRate;\n";
    }
    ss << "\n";
    return ss.str();
}

// ============================================================
// cbuffer TextureParams : register(b1)
// ============================================================

std::string StoyHlslGenerator::GenerateTextureParamsCBuffer(const StoyFileData& data) {
    // 只有在有纹理或 pass > 1 时才需要
    bool hasTexelSize = !data.textures.empty() || data.passes.size() > 1;
    if (!hasTexelSize) return "";

    std::ostringstream ss;
    ss << "cbuffer TextureParams : register(b1) {\n";
    // 外部纹理 TexelSize（按声明顺序）
    for (const auto& tex : data.textures) {
        ss << "    float4 " << tex.name << "_TexelSize;\n";
    }
    // Pass 输出纹理 TexelSize（按声明顺序）
    for (const auto& pass : data.passes) {
        ss << "    float4 " << pass.name << "_TexelSize;\n";
    }
    ss << "};\n\n";
    return ss.str();
}

// ============================================================
// 纹理/采样器声明 + register 分配
// ============================================================

std::string StoyHlslGenerator::GenerateTextureDeclarations(
    const StoyFileData& data, std::vector<StoyTextureBinding>& bindings)
{
    std::ostringstream ss;
    int slot = 0;

    // 外部纹理（按 texture 声明顺序）
    if (!data.textures.empty()) {
        ss << "// --- external textures ---\n";
        for (const auto& tex : data.textures) {
            ss << "Texture2D<float4> " << tex.name << " : register(t" << slot << ");\n";
            ss << "SamplerState " << tex.name << "_Sampler : register(s" << slot << ");\n";

            StoyTextureBinding binding;
            binding.name = tex.name;
            binding.registerSlot = slot;
            binding.isPassOutput = false;
            bindings.push_back(binding);
            slot++;
        }
        ss << "\n";
    }

    // Pass 输出纹理（按 pass 声明顺序，所有 pass 都注入）
    if (data.passes.size() > 0) {
        ss << "// --- pass output textures ---\n";
        for (int i = 0; i < (int)data.passes.size(); i++) {
            const auto& pass = data.passes[i];
            ss << "Texture2D<float4> " << pass.name << " : register(t" << slot << ");\n";
            ss << "SamplerState " << pass.name << "_Sampler : register(s" << slot << ");\n";

            StoyTextureBinding binding;
            binding.name = pass.name;
            binding.registerSlot = slot;
            binding.isPassOutput = true;
            binding.passIndex = i;
            bindings.push_back(binding);
            slot++;
        }
        ss << "\n";
    }

    return ss.str();
}

// ============================================================
// main 入口包装
// ============================================================

std::string StoyHlslGenerator::GenerateMainWrapper(bool isLastPass) {
    std::ostringstream ss;
    ss << "// --- entry point ---\n";
    ss << "struct PS_INPUT {\n";
    ss << "    float4 pos : SV_Position;\n";
    ss << "};\n\n";
    ss << "float4 main(PS_INPUT input) : SV_Target0 {\n";
    ss << "    float2 fragCoord = float2(input.pos.x, ";
    if (isLastPass) {
        // Image pass（最后一个 pass）需要翻转 Y，因为 D3D11 坐标系 Y 向下
        ss << "_iResolution4.y - input.pos.y";
    } else {
        // Buffer pass 不翻转
        ss << "input.pos.y";
    }
    ss << ");\n";
    ss << "    float4 fragColor = float4(0, 0, 0, 1);\n";
    ss << "    mainImage(fragColor, fragCoord);\n";
    ss << "    return fragColor;\n";
    ss << "}\n";
    return ss.str();
}

// ============================================================
// 主入口：Generate
// ============================================================

StoyHlslResult StoyHlslGenerator::Generate(const StoyFileData& data) {
    StoyHlslResult result;

    // 生成公共部分（所有 pass 共享）
    std::string cbufferB0 = GenerateCBufferB0();
    std::string innerAliases = GenerateInnerVarsAliases(data.innerVars);
    std::string cbufferB1 = GenerateTextureParamsCBuffer(data);

    // 纹理声明 + 绑定信息
    std::vector<StoyTextureBinding> bindings;
    std::string texDecls = GenerateTextureDeclarations(data, bindings);
    result.textureBindings = bindings;
    result.totalTextureSlots = bindings.empty() ? 0 : (bindings.back().registerSlot + 1);

    // 为每个 pass 生成完整 HLSL
    for (int i = 0; i < (int)data.passes.size(); i++) {
        const auto& pass = data.passes[i];
        bool isLast = (i == (int)data.passes.size() - 1);

        std::ostringstream ss;
        ss << "// Auto-generated HLSL from .stoy format\n";
        ss << "// Pass: " << pass.name;
        if (isLast) ss << " (Image output)";
        ss << "\n\n";

        ss << cbufferB0;
        ss << innerAliases;
        ss << cbufferB1;
        ss << texDecls;

        // Common 代码
        if (!data.commonCode.empty()) {
            ss << "// --- common code ---\n";
            ss << data.commonCode << "\n\n";
        }

        // 用户 pass 代码
        ss << "// --- pass code: " << pass.name << " ---\n";
        ss << pass.code << "\n\n";

        // main 入口包装
        ss << GenerateMainWrapper(isLast);

        StoyPassHlsl passHlsl;
        passHlsl.passName = pass.name;
        passHlsl.hlslSource = ss.str();
        result.passHlsls.push_back(passHlsl);
    }

    return result;
}
