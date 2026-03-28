#include "shader_manager.h"
#include <fstream>
#include <sstream>
#include <iostream>

// 全屏四边形顶点着色器 —— 直接输出 NDC 坐标
const char* ShaderManager::kVertexShaderSource = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)glsl";

ShaderManager::ShaderManager() = default;

ShaderManager::~ShaderManager() {
    Cleanup();
}

bool ShaderManager::LoadFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        lastError_ = "Failed to open shader file: " + filePath;
        return false;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    return LoadFromSource(ss.str());
}

bool ShaderManager::LoadFromSource(const std::string& shaderToySource) {
    Cleanup();

    // 编译顶点着色器
    GLuint vertShader = CompileShader(GL_VERTEX_SHADER, kVertexShaderSource);
    if (vertShader == 0) return false;

    // 将 ShaderToy 源码包装成完整的 Fragment Shader 并编译
    std::string fragSource = WrapShaderToySource(shaderToySource);
    GLuint fragShader = CompileShader(GL_FRAGMENT_SHADER, fragSource);
    if (fragShader == 0) {
        glDeleteShader(vertShader);
        return false;
    }

    // 链接
    if (!LinkProgram(vertShader, fragShader)) {
        glDeleteShader(vertShader);
        glDeleteShader(fragShader);
        return false;
    }

    // 链接成功后 shader 对象可以删除
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    return true;
}

void ShaderManager::Use() const {
    if (program_) {
        glUseProgram(program_);
    }
}

GLint ShaderManager::GetUniformLocation(const char* name) const {
    return glGetUniformLocation(program_, name);
}

std::string ShaderManager::WrapShaderToySource(const std::string& source) const {
    // 将 ShaderToy 的 mainImage(out vec4, in vec2) 包装成标准 GLSL
    std::string wrapped = R"glsl(
#version 330 core

// ShaderToy 内置 uniform 变量
uniform vec3      iResolution;           // 视口分辨率 (pixels)
uniform float     iTime;                 // 播放时间 (seconds)
uniform float     iTimeDelta;            // 帧间隔时间 (seconds)
uniform int       iFrame;               // 帧计数
uniform vec4      iMouse;               // 鼠标位置: xy=当前位置, zw=点击位置
uniform vec4      iDate;                // 年/月/日/秒
uniform float     iSampleRate;          // 音频采样率

out vec4 _fragColor_out;

)glsl";

    // 插入用户的 ShaderToy 源码
    wrapped += source;

    // 添加 main 函数，调用 ShaderToy 的 mainImage
    wrapped += R"glsl(

void main() {
    _fragColor_out = vec4(0.0);
    mainImage(_fragColor_out, gl_FragCoord.xy);
}
)glsl";

    return wrapped;
}

GLuint ShaderManager::CompileShader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        lastError_ = std::string("Shader compilation error:\n") + infoLog;
        std::cerr << lastError_ << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool ShaderManager::LinkProgram(GLuint vertShader, GLuint fragShader) {
    program_ = glCreateProgram();
    glAttachShader(program_, vertShader);
    glAttachShader(program_, fragShader);
    glLinkProgram(program_);

    GLint success;
    glGetProgramiv(program_, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetProgramInfoLog(program_, sizeof(infoLog), nullptr, infoLog);
        lastError_ = std::string("Shader link error:\n") + infoLog;
        std::cerr << lastError_ << std::endl;
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }
    return true;
}

void ShaderManager::Cleanup() {
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}
