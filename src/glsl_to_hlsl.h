#pragma once

#include <string>
#include <array>

// 前向声明 ChannelType，避免引入 shader_manager.h 的 OpenGL 依赖
enum class ChannelType;

// ============================================================
// SPIRV-Cross 管线（glslang + SPIRV-Cross）
// ============================================================

/// 全局初始化 glslang（进程启动时调用一次）
void InitShaderTranslator();

/// 全局清理 glslang（进程退出时调用一次）
void ShutdownShaderTranslator();

/// 将 ShaderToy GLSL 翻译为完整的 HLSL Pixel Shader 源码。
/// USE_SPIRV_CROSS 时走 glslang + SPIRV-Cross 管线，否则走旧正则翻译器。
///
/// @param glslSource       用户 shader 源码（ShaderToy 格式，非标准 GLSL）
/// @param channelTypes     四通道类型（Texture2D/CubeMap/Texture3D）
/// @param commonSource     Common 共享 GLSL 代码（空字符串表示无）
/// @param isCubeMapPass    是否为 CubeMap pass（mainCubemap 入口）
/// @param flipFragCoordY   是否翻转 gl_FragCoord.y（Image pass = true，Buffer pass = false）
/// @param outErrors        翻译错误信息输出（nullptr 表示不关心）
/// @return 完整 HLSL Pixel Shader 源码（空字符串表示翻译失败）
std::string TranslateGlslToFullHlsl(
    const std::string& glslSource,
    const std::array<ChannelType, 4>& channelTypes,
    const std::string& commonSource = "",
    bool isCubeMapPass = false,
    bool flipFragCoordY = true,
    std::string* outErrors = nullptr);

// ============================================================
// 旧接口（保留向后兼容，USE_SPIRV_CROSS=OFF 时使用）
// ============================================================

/// 将 ShaderToy GLSL 源码翻译为 HLSL 代码段（正则替换）。
std::string TranslateGlslToHlsl(const std::string& glslSource);

/// 将翻译后的 HLSL 代码段包装为完整的 HLSL Pixel Shader 源码。
std::string WrapShaderToyHlsl(const std::string& translatedHlsl,
                              const std::array<ChannelType, 4>& channelTypes,
                              const std::string& commonSource = "",
                              bool isCubeMapPass = false);

// ============================================================
// 编译验证（两种管线共用）
// ============================================================

/// 编译 HLSL 源码进行验证（D3DCompile ps_5_0，仅 Windows）。
bool CompileHlslForValidation(const std::string& hlslSource,
                              const std::string& sourceName,
                              std::string& outErrors);
