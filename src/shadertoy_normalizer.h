#pragma once

#include <string>
#include <array>

// 前向声明 ChannelType，避免引入 shader_manager.h 的 OpenGL 依赖
enum class ChannelType;

// ============================================================
// ShaderToyNormalizer
// 将 ShaderToy 非标准 GLSL 代码标准化为可被 glslang 编译的完整 GLSL 330 程序。
//
// 职责（翻译管线第一层）：
//   1. 预处理用户代码（变量名冲突重命名、tanh 安全化等）
//   2. 合成完整 GLSL 330 程序（uniform block、sampler、Common、main 入口）
// ============================================================

class ShaderToyNormalizer {
public:
    struct Options {
        std::array<ChannelType, 4> channelTypes;
        std::string commonSource;
        bool isCubeMapPass = false;
    };

    /// 将 ShaderToy 用户 GLSL 标准化为完整的 GLSL 330 程序
    /// @param userSource  ShaderToy 格式的 GLSL 代码（mainImage/mainCubemap 函数）
    /// @param opts        通道类型、Common 代码、是否 CubeMap pass
    /// @return 完整的、可被 glslang 编译的 GLSL 330 源码
    static std::string Normalize(const std::string& userSource, const Options& opts);

    /// 仅预处理用户代码（不合成完整程序）
    /// 用于调试或单独需要预处理的场景
    static std::string Preprocess(const std::string& source);
};
