#pragma once

#include <string>
#include <glad/glad.h>

/// ShaderManager 负责加载、编译和管理 ShaderToy 兼容的着色器。
/// 它将 ShaderToy 的 mainImage 函数包装成标准的 OpenGL Fragment Shader。
class ShaderManager {
public:
    ShaderManager();
    ~ShaderManager();

    /// 从文件加载 ShaderToy 格式的着色器源码并编译
    bool LoadFromFile(const std::string& filePath);

    /// 直接从字符串加载 ShaderToy 格式的着色器源码并编译
    bool LoadFromSource(const std::string& shaderToySource);

    /// 激活当前 shader program
    void Use() const;

    /// 获取 OpenGL program ID
    GLuint GetProgram() const { return program_; }

    /// 获取 uniform location（带缓存）
    GLint GetUniformLocation(const char* name) const;

    /// 获取最近的编译/链接错误信息
    const std::string& GetLastError() const { return lastError_; }

private:
    /// 将 ShaderToy 着色器源码包装成完整的 Fragment Shader
    std::string WrapShaderToySource(const std::string& source) const;

    /// 编译着色器
    GLuint CompileShader(GLenum type, const std::string& source);

    /// 链接程序
    bool LinkProgram(GLuint vertShader, GLuint fragShader);

    /// 清理旧的 program
    void Cleanup();

    GLuint program_ = 0;
    std::string lastError_;

    // 全屏四边形的顶点着色器（固定）
    static const char* kVertexShaderSource;
};
