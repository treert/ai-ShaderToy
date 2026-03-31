#include "shadertoy_normalizer.h"
#include "shader_manager.h"  // for ChannelType enum
#include <regex>
#include <sstream>

// ============================================================
// PreprocessGlsl — GLSL 层预处理
// ============================================================

std::string ShaderToyNormalizer::Preprocess(const std::string& source) {
    std::string s = source;

    // --- 1. 变量名冲突预重命名 ---
    // SPIRV-Cross 翻译时会把 GLSL 函数名映射为 HLSL 等价函数：
    //   fract() → frac(), mix() → lerp() 等
    // 如果用户代码中有变量名恰好是 GLSL 函数名（如 "float fract"），
    // 翻译后变量名也会变成 HLSL 函数名（"float frac"），导致冲突。
    // 解决：在 GLSL 层就把这些变量名加 _var 后缀。
    static const struct { const char* glslName; const char* hlslConflict; } conflictMap[] = {
        {"fract", "frac"},
        {"mod",   nullptr},
        {nullptr, nullptr}
    };
    static const char* directConflicts[] = {
        "frac", "clip", "step", "lerp", "clamp", "sign", "normalize",
        "reflect", "refract", "distance", "length", "dot", "cross",
        "sample", "noise", "abort", "log", "log2", "exp", "exp2",
        "pow", "sqrt", "rsqrt", "round", "trunc", "ceil", "floor",
        nullptr
    };

    auto renameVarInGlsl = [&s](const std::string& varName) {
        std::regex declRe("(float|int|uint|double|bool|vec[234]|ivec[234]|uvec[234]|mat[234])\\s+"
                          + varName + "\\s*[=;,)]");
        if (std::regex_search(s, declRe)) {
            std::string renamed = varName + "_var";
            std::regex wordRe("\\b" + varName + "\\b");
            s = std::regex_replace(s, wordRe, renamed);
            std::string fnCall = renamed + "(";
            std::string fnOrig = varName + "(";
            size_t pos = 0;
            while ((pos = s.find(fnCall, pos)) != std::string::npos) {
                s.replace(pos, fnCall.size(), fnOrig);
                pos += fnOrig.size();
            }
        }
    };

    for (auto* p = &conflictMap[0]; p->glslName; ++p) {
        renameVarInGlsl(p->glslName);
    }
    for (const char** p = directConflicts; *p; ++p) {
        renameVarInGlsl(*p);
    }

    // --- 2. tanh 安全化 ---
    if (s.find("tanh(") != std::string::npos) {
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
            result += partOfWord ? "tanh(" : "_safe_tanh(";
            pos = found + 5;
        }
        s = std::move(result);

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

// ============================================================
// SynthesizeFullGlsl — 合成完整 GLSL 330 程序
// ============================================================

std::string ShaderToyNormalizer::Normalize(const std::string& userSource, const Options& opts) {
    std::ostringstream glsl;
    glsl << "#version 330\n\n";

    // ShaderToy 内置 uniform 声明——std140 uniform block
    // 内存布局必须与 C++ ShaderToyConstants 结构体对齐
    glsl << "layout(std140) uniform ShaderToyUniforms {\n"
         << "    vec4  _iResolution4;\n"
         << "    float iTime;\n"
         << "    float iTimeDelta;\n"
         << "    int   iFrame;\n"
         << "    float iFrameRate;\n"
         << "    vec4  iMouse;\n"
         << "    vec4  iDate;\n"
         << "    float iSampleRate;\n"
         << "    float iClickTime;\n"
         << "    vec2  _pad0;\n"
         << "    vec4  iChannelTime_;\n"
         << "    vec4  iChannelResolution_[4];\n"
         << "    vec4  _cubeFaceRight;\n"
         << "    vec4  _cubeFaceUp;\n"
         << "    vec4  _cubeFaceDir;\n"
         << "};\n\n"
         << "vec3 iResolution = _iResolution4.xyz;\n"
         << "float iChannelTime[4];\n"
         << "vec3  iChannelResolution[4];\n\n";

    // 通道采样器声明
    const char* samplerTypeNames[] = {"sampler2D", "sampler2D", "samplerCube", "sampler3D"};
    for (int i = 0; i < 4; ++i) {
        int typeIndex = 0;
        switch (opts.channelTypes[i]) {
        case ChannelType::None:      typeIndex = 0; break;
        case ChannelType::Texture2D: typeIndex = 1; break;
        case ChannelType::CubeMap:   typeIndex = 2; break;
        case ChannelType::Texture3D: typeIndex = 3; break;
        }
        glsl << "uniform " << samplerTypeNames[typeIndex]
             << " iChannel" << i << ";\n";
    }

    glsl << "\nout vec4 _fragColor_out;\n\n";

    // Common 共享代码段（预处理）
    if (!opts.commonSource.empty()) {
        glsl << "// === Common code begin ===\n"
             << Preprocess(opts.commonSource)
             << "\n// === Common code end ===\n\n";
    }

    // 用户 shader 代码（预处理）
    glsl << Preprocess(userSource) << "\n";

    // main 入口
    const char* initArrays = R"glsl(
    iChannelTime[0] = iChannelTime_.x;
    iChannelTime[1] = iChannelTime_.y;
    iChannelTime[2] = iChannelTime_.z;
    iChannelTime[3] = iChannelTime_.w;
    iChannelResolution[0] = iChannelResolution_[0].xyz;
    iChannelResolution[1] = iChannelResolution_[1].xyz;
    iChannelResolution[2] = iChannelResolution_[2].xyz;
    iChannelResolution[3] = iChannelResolution_[3].xyz;
)glsl";

    if (opts.isCubeMapPass) {
        glsl << "\nvoid main() {\n" << initArrays << R"glsl(
    _fragColor_out = vec4(0.0);
    vec2 uv = (gl_FragCoord.xy / iResolution.xy) * 2.0 - 1.0;
    vec3 rayDir = normalize(_cubeFaceDir + uv.x * _cubeFaceRight + uv.y * _cubeFaceUp);
    vec3 rayOri = vec3(0.0);
    mainCubemap(_fragColor_out, gl_FragCoord.xy, rayOri, rayDir);
}
)glsl";
    } else {
        glsl << "\nvoid main() {\n" << initArrays << R"glsl(
    _fragColor_out = vec4(0.0);
    mainImage(_fragColor_out, gl_FragCoord.xy);
}
)glsl";
    }

    return glsl.str();
}
