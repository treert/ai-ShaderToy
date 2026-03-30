#pragma once

#include <string>

/// 将 ShaderToy GLSL 源码翻译为 HLSL 代码段。
/// 只翻译用户 shader 代码部分（不含 uniform 声明和 main 包装，
/// 那些由 D3D11ShaderManager 的 HLSL 模板处理）。
///
/// 当前实现基于正则/字符串替换，覆盖 ShaderToy GLSL 330 子集。
/// 后续可替换为 glslang + SPIRV-Cross 管线，接口不变。
///
/// @param glslSource ShaderToy 用户 shader 源码
/// @return 翻译后的 HLSL 代码段
std::string TranslateGlslToHlsl(const std::string& glslSource);
