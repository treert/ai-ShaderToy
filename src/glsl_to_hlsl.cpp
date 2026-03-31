#include "glsl_to_hlsl.h"
#include "shader_manager.h"  // for ChannelType enum
#include <regex>
#include <sstream>
#include <vector>
#include <algorithm>
#include <iostream>

// ============================================================
// SPIRV-Cross 翻译管线 (glslang + SPIRV-Cross)
// ============================================================
#ifdef USE_SPIRV_CROSS

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_hlsl.hpp>
#include "hlsl_fixup.h"

void InitShaderTranslator() {
    glslang::InitializeProcess();
}

void ShutdownShaderTranslator() {
    glslang::FinalizeProcess();
}

/// GLSL 层预处理：在送给 glslang 之前修改用户代码
/// 目的是在 GLSL 阶段解决问题，减少 SPIRV-Cross HLSL 后处理的负担
static std::string PreprocessGlsl(const std::string& source) {
    std::string s = source;

    // --- 1. 变量名冲突预重命名 ---
    // SPIRV-Cross 翻译时会把 GLSL 函数名映射为 HLSL 等价函数：
    //   fract() → frac(), mix() → lerp() 等
    // 如果用户代码中有**变量名**恰好是 GLSL 函数名（如 "float fract"），
    // 翻译后变量名也会变成 HLSL 函数名（"float frac"），导致冲突。
    // 解决：在 GLSL 层就把这些变量名加 _var 后缀。
    //
    // 映射表：GLSL 变量名 → 翻译后的 HLSL 名字（可能冲突的）
    // 只需检测 GLSL 中的变量声明，不需要关心 GLSL 函数调用
    static const struct { const char* glslName; const char* hlslConflict; } conflictMap[] = {
        {"fract", "frac"},      // fract() → frac()，变量 fract → frac 冲突
        {"mod",   nullptr},     // mod → glsl_mod（旧翻译器），SPIRV-Cross 不重命名，但保留以防万一
        {nullptr, nullptr}
    };
    // 另外，用户变量名可能直接就是 HLSL 内置函数名（不需要经过映射）
    static const char* directConflicts[] = {
        "frac", "clip", "step", "lerp", "clamp", "sign", "normalize",
        "reflect", "refract", "distance", "length", "dot", "cross",
        "sample", "noise", "abort", "log", "log2", "exp", "exp2",
        "pow", "sqrt", "rsqrt", "round", "trunc", "ceil", "floor",
        nullptr
    };

    auto renameVarInGlsl = [&s](const std::string& varName) {
        // 检测是否存在变量声明模式（类型名 + 空格 + 变量名 + [=;,)]）
        std::regex declRe("(float|int|uint|double|bool|vec[234]|ivec[234]|uvec[234]|mat[234])\\s+"
                          + varName + "\\s*[=;,)]");
        if (std::regex_search(s, declRe)) {
            // 全词替换变量名为 varName_var
            std::string renamed = varName + "_var";
            std::regex wordRe("\\b" + varName + "\\b");
            // 先全替换，然后恢复函数调用
            s = std::regex_replace(s, wordRe, renamed);
            // 恢复函数调用：varName_var( → varName(
            std::string fnCall = renamed + "(";
            std::string fnOrig = varName + "(";
            size_t pos = 0;
            while ((pos = s.find(fnCall, pos)) != std::string::npos) {
                s.replace(pos, fnCall.size(), fnOrig);
                pos += fnOrig.size();
            }
        }
    };

    // 检测 GLSL 变量名经翻译后会冲突的情况
    for (auto* p = &conflictMap[0]; p->glslName; ++p) {
        renameVarInGlsl(p->glslName);
    }
    // 检测直接冲突的变量名（名字在 GLSL 和 HLSL 中相同）
    for (const char** p = directConflicts; *p; ++p) {
        renameVarInGlsl(*p);
    }

    // --- 2. tanh 安全化 ---
    // 某些 D3D11 GPU/驱动对极大输入的 tanh 返回 NaN，导致数值发散。
    // 在 GLSL 层替换 tanh → _safe_tanh，注入 GLSL 版 _safe_tanh 定义。
    // 这样 glslang 编译后 SPIR-V 中就是 _safe_tanh，SPIRV-Cross 直接输出，无需 HLSL 后处理。
    if (s.find("tanh(") != std::string::npos) {
        // 替换 tanh( → _safe_tanh(，跳过 atanh( 等前缀
        std::string result;
        result.reserve(s.size() + 256);
        size_t pos = 0;
        while (pos < s.size()) {
            size_t found = s.find("tanh(", pos);
            if (found == std::string::npos) {
                result.append(s, pos, s.size() - pos);
                break;
            }
            bool partOfWord = (found > 0 && (std::isalnum(static_cast<unsigned char>(s[found - 1])) || s[found - 1] == '_'));
            result.append(s, pos, found - pos);
            if (partOfWord) {
                result += "tanh(";
            } else {
                result += "_safe_tanh(";
            }
            pos = found + 5;
        }
        s = std::move(result);

        // 在代码开头注入 GLSL 版 _safe_tanh 定义
        static const char* safeTanhGlsl =
            "// Safe tanh: clamp input to avoid NaN on some D3D11 GPU drivers\n"
            "float  _safe_tanh(float  x) { return tanh(clamp(x, -20.0, 20.0)); }\n"
            "vec2   _safe_tanh(vec2   x) { return tanh(clamp(x, vec2(-20.0), vec2(20.0))); }\n"
            "vec3   _safe_tanh(vec3   x) { return tanh(clamp(x, vec3(-20.0), vec3(20.0))); }\n"
            "vec4   _safe_tanh(vec4   x) { return tanh(clamp(x, vec4(-20.0), vec4(20.0))); }\n\n";
        s = std::string(safeTanhGlsl) + s;
    }

    return s;
}

/// 合成完整的标准 GLSL 330 程序，供 glslang 编译
static std::string SynthesizeFullGlsl(
    const std::string& userSource,
    const std::array<ChannelType, 4>& channelTypes,
    const std::string& commonSource,
    bool isCubeMapPass)
{
    std::ostringstream glsl;
    glsl << "#version 330\n\n";

    // ShaderToy 内置 uniform 声明——使用 std140 uniform block 确保 SPIRV-Cross 生成 cbuffer
    // 内存布局必须与 C++ ShaderToyConstants 结构体对齐
    // 无实例名，UBO 成员作为全局变量直接可用（ShaderToy 用户代码直接引用 iTime 等）
    glsl << "layout(std140) uniform ShaderToyUniforms {\n"
         << "    vec4  _iResolution4;          // xyz=resolution, w=padding (内部别名)\n"
         << "    float iTime;\n"
         << "    float iTimeDelta;\n"
         << "    int   iFrame;\n"
         << "    float iFrameRate;\n"
         << "    vec4  iMouse;\n"
         << "    vec4  iDate;\n"
         << "    float iSampleRate;\n"
         << "    float iClickTime;\n"
         << "    vec2  _pad0;\n"
         << "    vec4  iChannelTime_;\n"       // 用别名避免与 float[] 冲突
         << "    vec4  iChannelResolution_[4];\n"
         << "    vec4  _cubeFaceRight;\n"
         << "    vec4  _cubeFaceUp;\n"
         << "    vec4  _cubeFaceDir;\n"
         << "};\n\n"
         // 提供 ShaderToy 标准变量名
         << "vec3 iResolution = _iResolution4.xyz;\n"
         << "float iChannelTime[4];\n"
         << "vec3  iChannelResolution[4];\n\n";

    // 通道采样器声明
    const char* samplerTypeNames[] = {"sampler2D", "sampler2D", "samplerCube", "sampler3D"};
    for (int i = 0; i < 4; ++i) {
        int typeIndex = 0;
        switch (channelTypes[i]) {
        case ChannelType::None:      typeIndex = 0; break;
        case ChannelType::Texture2D: typeIndex = 1; break;
        case ChannelType::CubeMap:   typeIndex = 2; break;
        case ChannelType::Texture3D: typeIndex = 3; break;
        }
        glsl << "uniform " << samplerTypeNames[typeIndex]
             << " iChannel" << i << ";\n";
    }

    glsl << "\nout vec4 _fragColor_out;\n\n";

    // Common 共享代码段（预处理：变量名冲突重命名 + tanh 安全化）
    if (!commonSource.empty()) {
        std::string processedCommon = PreprocessGlsl(commonSource);
        glsl << "// === Common code begin ===\n"
             << processedCommon
             << "\n// === Common code end ===\n\n";
    }

    // 用户 shader 代码（预处理：变量名冲突重命名 + tanh 安全化）
    glsl << PreprocessGlsl(userSource) << "\n";

    // main 入口
    // 先生成 UBO → 全局数组赋值代码
    const char* initArrays = R"glsl(
    // 从 UBO vec4 成员赋值到 ShaderToy 标准数组
    iChannelTime[0] = iChannelTime_.x;
    iChannelTime[1] = iChannelTime_.y;
    iChannelTime[2] = iChannelTime_.z;
    iChannelTime[3] = iChannelTime_.w;
    iChannelResolution[0] = iChannelResolution_[0].xyz;
    iChannelResolution[1] = iChannelResolution_[1].xyz;
    iChannelResolution[2] = iChannelResolution_[2].xyz;
    iChannelResolution[3] = iChannelResolution_[3].xyz;
)glsl";

    if (isCubeMapPass) {
        glsl << "\nvoid main() {\n"
             << initArrays
             << R"glsl(
    _fragColor_out = vec4(0.0);
    vec2 uv = (gl_FragCoord.xy / iResolution.xy) * 2.0 - 1.0;
    vec3 rayDir = normalize(_cubeFaceDir + uv.x * _cubeFaceRight + uv.y * _cubeFaceUp);
    vec3 rayOri = vec3(0.0);
    mainCubemap(_fragColor_out, gl_FragCoord.xy, rayOri, rayDir);
}
)glsl";
    } else {
        glsl << "\nvoid main() {\n"
             << initArrays
             << R"glsl(
    _fragColor_out = vec4(0.0);
    mainImage(_fragColor_out, gl_FragCoord.xy);
}
)glsl";
    }

    return glsl.str();
}

/// glslang 编译 GLSL → SPIR-V
static bool CompileGlslToSpirv(const std::string& glslSource,
                                std::vector<uint32_t>& spirvOut,
                                std::string& errorsOut) {
    const char* src = glslSource.c_str();
    const int srcLen = static_cast<int>(glslSource.size());

    glslang::TShader shader(EShLangFragment);
    shader.setStringsWithLengths(&src, &srcLen, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, EShLangFragment, glslang::EShClientOpenGL, 330);
    shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    shader.setAutoMapLocations(true);   // 自动为 uniform 分配 location
    shader.setAutoMapBindings(true);    // 自动为采样器分配 binding

    const TBuiltInResource* resources = GetDefaultResources();
    const int defaultVersion = 330;

    if (!shader.parse(resources, defaultVersion, false, EShMsgDefault)) {
        errorsOut = "glslang parse error:\n";
        errorsOut += shader.getInfoLog();
        return false;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(EShMsgDefault)) {
        errorsOut = "glslang link error:\n";
        errorsOut += program.getInfoLog();
        return false;
    }

    // 自动映射 uniform/sampler 绑定点
    program.mapIO();

    glslang::SpvOptions spvOptions;
    spvOptions.validate = false;
    spvOptions.disableOptimizer = true;

    glslang::GlslangToSpv(*program.getIntermediate(EShLangFragment), spirvOut, &spvOptions);
    return !spirvOut.empty();
}

/// SPIRV-Cross 反编译 SPIR-V → HLSL
static std::string CrossCompileToHlsl(const std::vector<uint32_t>& spirv,
                                       bool flipFragCoordY,
                                       std::string& errorsOut) {
    try {
        spirv_cross::CompilerHLSL compiler(spirv);

        // 设置 HLSL 编译选项
        spirv_cross::CompilerHLSL::Options hlslOpts;
        hlslOpts.shader_model = 50;  // SM 5.0
        hlslOpts.point_size_compat = false;
        hlslOpts.point_coord_compat = false;
        compiler.set_hlsl_options(hlslOpts);

        // 通用编译选项
        spirv_cross::CompilerGLSL::Options commonOpts;
        commonOpts.flatten_multidimensional_arrays = true;
        commonOpts.force_zero_initialized_variables = true;  // 解决 HLSL X3508: out 参数未初始化
        compiler.set_common_options(commonOpts);

        // 通过反射获取资源并设置 register 绑定
        auto resources = compiler.get_shader_resources();

        // cbuffer → register(b0)
        // glslang 为无实例名的 uniform block 生成匿名变量名（如 _24），
        // 导致 SPIRV-Cross 生成不确定的 _24_memberName 前缀。
        // 通过 set_name 强制 UBO 变量名为 _stU（ShaderToy Uniform），
        // 使前缀变为确定性的 "_stU_"，后处理只需简单的字符串替换即可。
        // 使用 _stU 而非 _st 以降低与用户代码变量名冲突的风险。
        for (auto& ub : resources.uniform_buffers) {
            compiler.set_decoration(ub.id, spv::DecorationBinding, 0);
            compiler.set_decoration(ub.id, spv::DecorationDescriptorSet, 0);
            compiler.set_name(ub.id, "_stU");
        }

        // 纹理 → register(t0-t3)
        for (auto& img : resources.sampled_images) {
            auto& name = compiler.get_name(img.id);
            // iChannel0-3 → t0-t3
            for (int i = 0; i < 4; ++i) {
                if (name == "iChannel" + std::to_string(i)) {
                    compiler.set_decoration(img.id, spv::DecorationBinding, i);
                    compiler.set_decoration(img.id, spv::DecorationDescriptorSet, 0);
                    break;
                }
            }
        }

        // 分离的纹理和采样器（如果 SPIRV-Cross 拆分了的话）
        for (auto& tex : resources.separate_images) {
            auto& name = compiler.get_name(tex.id);
            for (int i = 0; i < 4; ++i) {
                if (name == "iChannel" + std::to_string(i)) {
                    compiler.set_decoration(tex.id, spv::DecorationBinding, i);
                    compiler.set_decoration(tex.id, spv::DecorationDescriptorSet, 0);
                    break;
                }
            }
        }
        for (auto& samp : resources.separate_samplers) {
            auto& name = compiler.get_name(samp.id);
            for (int i = 0; i < 4; ++i) {
                std::string expectedName = "iChannel" + std::to_string(i);
                // SPIRV-Cross 生成的采样器名可能带 Sampler 后缀
                if (name.find(expectedName) != std::string::npos) {
                    compiler.set_decoration(samp.id, spv::DecorationBinding, i);
                    compiler.set_decoration(samp.id, spv::DecorationDescriptorSet, 0);
                    break;
                }
            }
        }

        std::string hlsl = compiler.compile();

        // 通过 FixupPipeline 按顺序执行所有 HLSL 后处理修复
        static HlslFixupPipeline fixupPipeline = CreateDefaultFixupPipeline();
        HlslFixupContext fixupCtx;
        fixupCtx.flipFragCoordY = flipFragCoordY;
        hlsl = fixupPipeline.run(std::move(hlsl), fixupCtx);

        return hlsl;
    } catch (const spirv_cross::CompilerError& e) {
        errorsOut = "SPIRV-Cross error: ";
        errorsOut += e.what();
        return "";
    }
}

std::string TranslateGlslToFullHlsl(
    const std::string& glslSource,
    const std::array<ChannelType, 4>& channelTypes,
    const std::string& commonSource,
    bool isCubeMapPass,
    bool flipFragCoordY,
    std::string* outErrors)
{
    std::string errors;

    // 1. 合成完整 GLSL 330 程序
    std::string fullGlsl = SynthesizeFullGlsl(glslSource, channelTypes, commonSource, isCubeMapPass);

    // 2. glslang 编译为 SPIR-V
    std::vector<uint32_t> spirv;
    if (!CompileGlslToSpirv(fullGlsl, spirv, errors)) {
        if (outErrors) *outErrors = errors;
        std::cerr << errors << std::endl;
        return "";
    }

    // 3. SPIRV-Cross 反编译为 HLSL
    std::string hlsl = CrossCompileToHlsl(spirv, flipFragCoordY, errors);
    if (hlsl.empty()) {
        if (outErrors) *outErrors = errors;
        std::cerr << errors << std::endl;
        return "";
    }

    if (outErrors) outErrors->clear();
    return hlsl;
}

#else // !USE_SPIRV_CROSS

// 无 SPIRV-Cross 时的 stub 实现
void InitShaderTranslator() {}
void ShutdownShaderTranslator() {}

std::string TranslateGlslToFullHlsl(
    const std::string& glslSource,
    const std::array<ChannelType, 4>& channelTypes,
    const std::string& commonSource,
    bool isCubeMapPass,
    bool flipFragCoordY,
    std::string* outErrors)
{
    // 降级到旧翻译器（旧翻译器在 PS 入口中已硬编码 Y 翻转，暂不支持条件翻转）
    std::string translated = TranslateGlslToHlsl(glslSource);
    return WrapShaderToyHlsl(translated, channelTypes, commonSource, isCubeMapPass);
}

#endif // USE_SPIRV_CROSS


// ============================================================
// 旧正则翻译器（始终保留，作为降级方案和 #ifndef USE_SPIRV_CROSS 使用）
// ============================================================

/// 全词匹配替换（避免替换子串，如 vec3 在 ivec3 中）
static std::string ReplaceWholeWord(const std::string& src,
                                     const std::string& from,
                                     const std::string& to) {
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

    s = RemoveLines(s, std::regex(R"(^\s*#version\b)"));
    s = RemoveLines(s, std::regex(R"(^\s*precision\s+)"));

    // 类型替换
    s = ReplaceWholeWord(s, "bvec4", "bool4");
    s = ReplaceWholeWord(s, "bvec3", "bool3");
    s = ReplaceWholeWord(s, "bvec2", "bool2");
    s = ReplaceWholeWord(s, "ivec4", "int4");
    s = ReplaceWholeWord(s, "ivec3", "int3");
    s = ReplaceWholeWord(s, "ivec2", "int2");
    s = ReplaceWholeWord(s, "uvec4", "uint4");
    s = ReplaceWholeWord(s, "uvec3", "uint3");
    s = ReplaceWholeWord(s, "uvec2", "uint2");
    s = ReplaceWholeWord(s, "mat4x4", "float4x4");
    s = ReplaceWholeWord(s, "mat3x3", "float3x3");
    s = ReplaceWholeWord(s, "mat2x2", "float2x2");
    s = ReplaceWholeWord(s, "mat4", "float4x4");
    s = ReplaceWholeWord(s, "mat3", "float3x3");
    s = ReplaceWholeWord(s, "mat2", "float2x2");
    s = ReplaceWholeWord(s, "vec4", "float4");
    s = ReplaceWholeWord(s, "vec3", "float3");
    s = ReplaceWholeWord(s, "vec2", "float2");

    // 内置函数替换
    s = ReplaceWholeWord(s, "mix", "lerp");
    s = ReplaceWholeWord(s, "fract", "frac");
    s = ReplaceWholeWord(s, "inversesqrt", "rsqrt");
    s = ReplaceWholeWord(s, "dFdx", "ddx");
    s = ReplaceWholeWord(s, "dFdy", "ddy");
    s = ReplaceWholeWord(s, "mod", "glsl_mod");

    // 纹理采样重写
    for (int i = 0; i < 4; ++i) {
        std::string chName = "iChannel" + std::to_string(i);
        std::regex texRe("\\btexture\\s*\\(\\s*" + chName + "\\s*,\\s*");
        s = std::regex_replace(s, texRe, "texture_" + chName + "(");
        std::regex texLodRe("\\btextureLod\\s*\\(\\s*" + chName + "\\s*,\\s*");
        s = std::regex_replace(s, texLodRe, "textureLod_" + chName + "(");
        std::regex texFetchRe("\\btexelFetch\\s*\\(\\s*" + chName + "\\s*,\\s*");
        s = std::regex_replace(s, texFetchRe, "texelFetch_" + chName + "(");
    }

    // HLSL 辅助函数
    std::string helpers = R"hlsl(
// === GLSL compatibility helpers ===
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

    hlsl << R"hlsl(
cbuffer ShaderToyUniforms : register(b0) {
    float4 iResolution;
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
    float4 iChannelResolution[4];
    float4 _cubeFaceRight;
    float4 _cubeFaceUp;
    float4 _cubeFaceDir;
};

)hlsl";

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

    for (int i = 0; i < 4; ++i) {
        hlsl << "SamplerState sampler" << i << " : register(s" << i << ");\n";
    }
    hlsl << "\n";

    hlsl << R"hlsl(
float4 _sampleTex2D(Texture2D<float4> tex, SamplerState samp, float2 uv) { return tex.Sample(samp, uv); }
float4 _sampleTexCube(TextureCube tex, SamplerState samp, float3 uvw) { return tex.Sample(samp, uvw); }
float4 _sampleTex3D(Texture3D<float4> tex, SamplerState samp, float3 uvw) { return tex.Sample(samp, uvw); }
float4 _sampleTex2DLod(Texture2D<float4> tex, SamplerState samp, float2 uv, float lod) { return tex.SampleLevel(samp, uv, lod); }
float4 _sampleTexCubeLod(TextureCube tex, SamplerState samp, float3 uvw, float lod) { return tex.SampleLevel(samp, uvw, lod); }
float4 _fetchTex2D(Texture2D<float4> tex, int2 coord, int lod) { return tex.Load(int3(coord, lod)); }

)hlsl";

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

    if (!commonSource.empty()) {
        std::string commonHlsl = TranslateGlslToHlsl(commonSource);
        hlsl << "// === Common code begin ===\n" << commonHlsl << "\n// === Common code end ===\n\n";
    }

    hlsl << translatedHlsl << "\n";

    if (isCubeMapPass) {
        hlsl << R"hlsl(
struct PSInput { float4 position : SV_Position; float2 texcoord : TEXCOORD0; };
float4 main(PSInput input) : SV_Target {
    float4 fragColor = float4(0.0, 0.0, 0.0, 1.0);
    // D3D11 SV_Position.y: top=0.5, bottom=H-0.5 → 翻转为 ShaderToy bottom=0 top=H
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
struct PSInput { float4 position : SV_Position; float2 texcoord : TEXCOORD0; };
float4 main(PSInput input) : SV_Target {
    float4 fragColor = float4(0.0, 0.0, 0.0, 1.0);
    // D3D11 SV_Position.y: top=0.5, bottom=H-0.5 → 翻转为 ShaderToy bottom=0 top=H
    float2 fragCoord = float2(input.position.x, iResolution.y - input.position.y);
    mainImage(fragColor, fragCoord);
    return fragColor;
}
)hlsl";
    }

    return hlsl.str();
}


// ============================================================
// HLSL 编译验证（两种管线共用）
// ============================================================

#ifdef _WIN32
#include <d3dcompiler.h>

bool CompileHlslForValidation(const std::string& hlslSource,
                              const std::string& sourceName,
                              std::string& outErrors) {
    outErrors.clear();

    UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL0;

    ID3D10Blob* shaderBlob = nullptr;
    ID3D10Blob* errorBlob = nullptr;

    HRESULT hr = D3DCompile(
        hlslSource.c_str(),
        hlslSource.size(),
        sourceName.c_str(),
        nullptr, nullptr,
        "main", "ps_5_0",
        compileFlags, 0,
        &shaderBlob, &errorBlob
    );

    bool success = SUCCEEDED(hr);

    if (errorBlob) {
        outErrors = static_cast<const char*>(errorBlob->GetBufferPointer());
        while (!outErrors.empty() && (outErrors.back() == '\n' || outErrors.back() == '\r' || outErrors.back() == ' ')) {
            outErrors.pop_back();
        }
        errorBlob->Release();
    }
    if (shaderBlob) {
        shaderBlob->Release();
    }

    return success;
}
#else
bool CompileHlslForValidation(const std::string&, const std::string&, std::string& outErrors) {
    outErrors = "HLSL compilation validation is only available on Windows.";
    return false;
}
#endif
