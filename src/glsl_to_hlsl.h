#pragma once

#include <string>
#include <array>

// 前向声明 ChannelType，避免引入 shader_manager.h 的 OpenGL 依赖
enum class ChannelType;

/// 将 ShaderToy GLSL 源码翻译为 HLSL 代码段。
/// 只翻译用户 shader 代码部分（不含 uniform 声明和 main 包装，
/// 那些由 WrapShaderToyHlsl 处理）。
///
/// 当前实现基于正则/字符串替换，覆盖 ShaderToy GLSL 330 子集。
/// 后续可替换为 glslang + SPIRV-Cross 管线，接口不变。
///
/// @param glslSource ShaderToy 用户 shader 源码
/// @return 翻译后的 HLSL 代码段
std::string TranslateGlslToHlsl(const std::string& glslSource);

/// 将翻译后的 HLSL 代码段包装为完整的 HLSL Pixel Shader 源码。
/// 包含 cbuffer 声明、纹理/采样器声明、辅助函数、Common 段和 main 入口。
/// 此函数是纯文本操作，不依赖 D3D11 设备。
///
/// @param translatedHlsl  经 TranslateGlslToHlsl 翻译后的代码段
/// @param channelTypes    四个通道的类型（Texture2D/CubeMap/Texture3D）
/// @param commonSource    Common 共享 GLSL 代码（空字符串表示无）
/// @param isCubeMapPass   是否为 CubeMap pass（mainCubemap 入口）
/// @return 完整的 HLSL Pixel Shader 源码
std::string WrapShaderToyHlsl(const std::string& translatedHlsl,
                              const std::array<ChannelType, 4>& channelTypes,
                              const std::string& commonSource = "",
                              bool isCubeMapPass = false);

/// 编译 HLSL 源码进行验证（不创建 GPU 资源，仅检查语法和语义错误）。
/// 使用 D3DCompile (ps_5_0) 进行编译。
/// 仅在 Windows 平台可用。
///
/// @param hlslSource  完整的 HLSL Pixel Shader 源码
/// @param sourceName  用于错误信息的源文件名（可选）
/// @param outErrors   编译错误信息输出（为空表示编译成功）
/// @return true 表示编译成功，false 表示有编译错误
bool CompileHlslForValidation(const std::string& hlslSource,
                              const std::string& sourceName,
                              std::string& outErrors);
