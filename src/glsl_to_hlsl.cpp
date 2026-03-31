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

void InitShaderTranslator() {
    glslang::InitializeProcess();
}

void ShutdownShaderTranslator() {
    glslang::FinalizeProcess();
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
    glsl << "layout(std140) uniform ShaderToyUniforms {\n"
         << "    vec4  iResolution;           // xyz=resolution, w=padding\n"
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
         // 提供 ShaderToy 标准变量名——全局数组，在 main() 入口从 UBO 赋值
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

    // Common 共享代码段
    if (!commonSource.empty()) {
        glsl << "// === Common code begin ===\n"
             << commonSource
             << "\n// === Common code end ===\n\n";
    }

    // 用户 shader 代码
    glsl << userSource << "\n";

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
        // 收集所有 UBO 成员名，用于后处理去前缀
        std::vector<std::string> uboMemberNames;
        for (auto& ub : resources.uniform_buffers) {
            compiler.set_decoration(ub.id, spv::DecorationBinding, 0);
            compiler.set_decoration(ub.id, spv::DecorationDescriptorSet, 0);

            // 收集 block 成员名
            auto& type = compiler.get_type(ub.base_type_id);
            for (uint32_t i = 0; i < type.member_types.size(); ++i) {
                uboMemberNames.push_back(compiler.get_member_name(ub.base_type_id, i));
            }
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

        // 后处理：SPIRV-Cross 对无实例名的 uniform block 生成 _ID_ 前缀
        // 如 _24_iTime → iTime。用成员名精确匹配替换。
        for (auto& memberName : uboMemberNames) {
            // 匹配 _数字_memberName 模式
            std::regex prefixRe("_\\d+_" + memberName);
            hlsl = std::regex_replace(hlsl, prefixRe, memberName);
        }

        // 后处理：D3D11 SV_Position.y 是 top-down（top=0.5, bottom=H-0.5）
        // 而 OpenGL gl_FragCoord.y 是 bottom-up（bottom=0.5, top=H-0.5）
        // Image pass 需要翻转（最终输出到屏幕），Buffer pass 不翻转
        // （Buffer pass 不翻转时 texelFetch(ivec2(fragCoord)) 坐标和 RTV 一致）
        if (flipFragCoordY) {
            const std::string wLine = "gl_FragCoord.w = 1.0 / gl_FragCoord.w;";
            size_t wPos = hlsl.find(wLine);
            if (wPos != std::string::npos) {
                size_t insertPos = wPos + wLine.size();
                hlsl.insert(insertPos,
                    "\n    gl_FragCoord.y = iResolution.y - gl_FragCoord.y;"
                );
            }
        }

        // 后处理：重命名与 HLSL 内置函数冲突的变量名
        // SPIRV-Cross 翻译时 GLSL fract() → HLSL frac()，但如果用户代码有
        // 变量名 fract，也会被翻译为 frac，导致和内置函数 frac() 冲突。
        // 检测模式：类型声明后跟内置函数名作为变量（如 "float frac"）
        {
            static const char* hlslBuiltins[] = {
                "frac", "clip", "step", "lerp", "clamp", "sign", "normalize",
                "reflect", "refract", "distance", "length", "dot", "cross",
                "sample", "noise", "abort", "log", "log2", "exp", "exp2",
                "pow", "sqrt", "rsqrt", "round", "trunc", "ceil", "floor",
                nullptr
            };
            for (const char** p = hlslBuiltins; *p; ++p) {
                std::string name = *p;
                // 检测是否存在 "类型 name =" 或 "类型 name;" 模式（变量声明）
                std::regex declRe("(float|int|uint|half|double|bool)\\s+" + name + "\\s*[=;,)]");
                if (std::regex_search(hlsl, declRe)) {
                    // 全词替换为 name_var（但只替换变量引用，不替换函数调用）
                    // 策略：先把变量声明和所有非函数调用的引用重命名
                    // 简单做法：全词替换 name 为 name_var，然后把 name_var( 改回 name(
                    std::string renamed = name + "_var";
                    std::regex wordRe("\\b" + name + "\\b");
                    hlsl = std::regex_replace(hlsl, wordRe, renamed);
                    // 恢复内置函数调用：name_var( → name(
                    std::string fnCall = renamed + "(";
                    std::string fnOrig = name + "(";
                    size_t pos = 0;
                    while ((pos = hlsl.find(fnCall, pos)) != std::string::npos) {
                        hlsl.replace(pos, fnCall.size(), fnOrig);
                        pos += fnOrig.size();
                    }
                }
            }
        }

        // 后处理：安全化 tanh 调用
        // 某些 D3D11 GPU/驱动对极大输入的 tanh 返回 NaN 而非 ±1，
        // 导致迭代式 shader 数值发散（如 zippy_zaps）。
        // 注入 _safe_tanh 重载函数并替换所有 tanh 调用。
        if (hlsl.find("tanh(") != std::string::npos) {
            // 先替换用户代码中的 tanh( → _safe_tanh(
            // 此时 HLSL 中还没有 _safe_tanh 定义，所以不会误替换
            // 注意：跳过 atanh( 等前缀包含字母的情况
            {
                std::string result;
                result.reserve(hlsl.size() + 256);
                size_t pos = 0;
                while (pos < hlsl.size()) {
                    size_t found = hlsl.find("tanh(", pos);
                    if (found == std::string::npos) {
                        result.append(hlsl, pos, hlsl.size() - pos);
                        break;
                    }
                    // 检查前一个字符是否为字母或下划线（如 atanh, _tanh 等）
                    bool partOfWord = (found > 0 && (std::isalnum(static_cast<unsigned char>(hlsl[found - 1])) || hlsl[found - 1] == '_'));
                    result.append(hlsl, pos, found - pos);
                    if (partOfWord) {
                        result += "tanh(";  // 保持原样
                    } else {
                        result += "_safe_tanh(";
                    }
                    pos = found + 5;  // skip "tanh("
                }
                hlsl = std::move(result);
            }

            // 然后在 cbuffer 之前插入 _safe_tanh 定义（内部调用原生 tanh）
            static const char* safeTanhDefs = R"hlsl(
// Safe tanh: clamp input to avoid NaN on some D3D11 GPU drivers
float  _safe_tanh(float  x) { return tanh(clamp(x, -20.0f, 20.0f)); }
float2 _safe_tanh(float2 x) { return tanh(clamp(x, -20.0f, 20.0f)); }
float3 _safe_tanh(float3 x) { return tanh(clamp(x, -20.0f, 20.0f)); }
float4 _safe_tanh(float4 x) { return tanh(clamp(x, -20.0f, 20.0f)); }

)hlsl";
            size_t cbufPos = hlsl.find("cbuffer ");
            if (cbufPos != std::string::npos) {
                hlsl.insert(cbufPos, safeTanhDefs);
            } else {
                hlsl = std::string(safeTanhDefs) + hlsl;
            }
        }

        // 后处理：修复 X3507 "Not all control paths return a value"
        // 为非 void 函数添加兜底 return 语句
        // SPIRV-Cross 生成的函数格式规律：顶格 "type name(params)\n{"，末尾顶格 "}"
        {
            static const std::regex funcDefRe(
                R"(^(float[2-4]?|int[2-4]?|uint[2-4]?|bool|half[2-4]?)\s+(\w+)\s*\([^)]*\)\s*$)");
            std::string result;
            result.reserve(hlsl.size() + 256);
            std::istringstream stream(hlsl);
            std::string line;
            std::string pendingReturnType;  // 非空表示上一个函数需要兜底 return
            int braceDepth = 0;
            bool inNonVoidFunc = false;

            while (std::getline(stream, line)) {
                // 检测函数定义
                std::smatch m;
                if (std::regex_match(line, m, funcDefRe)) {
                    pendingReturnType = m[1].str();
                    inNonVoidFunc = false;
                    braceDepth = 0;
                }

                // 跟踪大括号深度
                for (char c : line) {
                    if (c == '{') {
                        if (!pendingReturnType.empty() && braceDepth == 0) {
                            inNonVoidFunc = true;
                        }
                        braceDepth++;
                    } else if (c == '}') {
                        braceDepth--;
                        if (inNonVoidFunc && braceDepth == 0) {
                            // 函数结束——在 } 前插入兜底 return
                            std::string defaultVal = "0";
                            if (pendingReturnType.find("2") != std::string::npos) defaultVal = "0.0f.xx";
                            else if (pendingReturnType.find("3") != std::string::npos) defaultVal = "0.0f.xxx";
                            else if (pendingReturnType.find("4") != std::string::npos) defaultVal = "0.0f.xxxx";
                            else defaultVal = "0.0f";
                            result += "    return " + defaultVal + ";\n";
                            inNonVoidFunc = false;
                            pendingReturnType.clear();
                        }
                    }
                }

                result += line + "\n";
            }
            hlsl = std::move(result);
        }

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
