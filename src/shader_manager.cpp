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

ShaderManager::ShaderManager(ShaderManager&& other) noexcept
    : program_(other.program_), lastError_(std::move(other.lastError_)),
      commonSource_(std::move(other.commonSource_)),
      isCubeMapPass_(other.isCubeMapPass_),
      channelTypes_(other.channelTypes_),
      uniforms_(other.uniforms_) {
    other.program_ = 0;
    other.uniforms_ = {};
}

ShaderManager& ShaderManager::operator=(ShaderManager&& other) noexcept {
    if (this != &other) {
        Cleanup();
        program_ = other.program_;
        lastError_ = std::move(other.lastError_);
        commonSource_ = std::move(other.commonSource_);
        isCubeMapPass_ = other.isCubeMapPass_;
        channelTypes_ = other.channelTypes_;
        uniforms_ = other.uniforms_;
        other.program_ = 0;
        other.uniforms_ = {};
    }
    return *this;
}

void ShaderManager::SetChannelTypes(const std::array<ChannelType, 4>& types) {
    channelTypes_ = types;
}

void ShaderManager::SetCommonSource(const std::string& common) {
    commonSource_ = common;
}

void ShaderManager::SetCubeMapPassMode(bool isCubeMap) {
    isCubeMapPass_ = isCubeMap;
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

    // 缓存所有 uniform location
    CacheUniformLocations();
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
    std::string wrapped = "#version 330 core\n\n"
        "// ShaderToy 内置 uniform 变量\n"
        "uniform vec3      iResolution;           // 视口分辨率 (pixels)\n"
        "uniform float     iTime;                 // 播放时间 (seconds)\n"
        "uniform float     iTimeDelta;            // 帧间隔时间 (seconds)\n"
        "uniform int       iFrame;               // 帧计数\n"
        "uniform float     iFrameRate;            // 每秒渲染帧数\n"
        "uniform vec4      iMouse;               // 鼠标位置: xy=当前位置, zw=点击位置\n"
        "uniform vec4      iDate;                // 年/月/日/秒\n"
        "uniform float     iSampleRate;          // 音频采样率\n"
        "uniform float     iChannelTime[4];      // 各通道播放时间 (seconds)\n"
        "uniform vec3      iChannelResolution[4]; // 各通道分辨率\n\n";

    // 根据通道类型动态生成 iChannel 采样器声明
    const char* samplerTypeNames[] = {"sampler2D", "sampler2D", "samplerCube", "sampler3D"};
    for (int i = 0; i < 4; ++i) {
        int typeIndex = 0;
        switch (channelTypes_[i]) {
        case ChannelType::None:      typeIndex = 0; break; // 未使用也声明为 sampler2D（不会出错）
        case ChannelType::Texture2D: typeIndex = 1; break;
        case ChannelType::CubeMap:   typeIndex = 2; break;
        case ChannelType::Texture3D: typeIndex = 3; break;
        }
        wrapped += "uniform ";
        wrapped += samplerTypeNames[typeIndex];
        wrapped += " iChannel";
        wrapped += std::to_string(i);
        wrapped += ";\n";
    }

    wrapped += R"glsl(
// 自定义扩展 uniform（非 ShaderToy 标准）
uniform float     iClickTime;           // 最近一次点击的时间 (seconds)

)glsl";

    // CubeMap pass 模式：额外声明 uniform 传递面方向信息
    if (isCubeMapPass_) {
        wrapped += R"glsl(
// CubeMap pass uniforms
uniform vec3  _cubeFaceRight;   // 当前面的 X 轴方向
uniform vec3  _cubeFaceUp;      // 当前面的 Y 轴方向
uniform vec3  _cubeFaceDir;     // 当前面的 Z 轴方向（面法线，指向面中心）

)glsl";
    }

    wrapped += "out vec4 _fragColor_out;\n\n";

    // 注入 Common 共享代码段（多 Pass 时各 pass 共享的函数/结构体/常量）
    if (!commonSource_.empty()) {
        wrapped += "// === Common code begin ===\n";
        wrapped += commonSource_;
        wrapped += "\n// === Common code end ===\n\n";
    }

    // 插入用户的 ShaderToy 源码
    wrapped += source;

    // 添加 main 函数，调用 ShaderToy 的 mainImage 或 mainCubemap
    if (isCubeMapPass_) {
        wrapped += R"glsl(

void main() {
    _fragColor_out = vec4(0.0);
    // 将像素坐标映射到 [-1, 1]，然后通过面方向矩阵计算 rayDir
    vec2 uv = (gl_FragCoord.xy / iResolution.xy) * 2.0 - 1.0;
    vec3 rayDir = normalize(_cubeFaceDir + uv.x * _cubeFaceRight + uv.y * _cubeFaceUp);
    vec3 rayOri = vec3(0.0);
    mainCubemap(_fragColor_out, gl_FragCoord.xy, rayOri, rayDir);
}
)glsl";
    } else {
        wrapped += R"glsl(

void main() {
    _fragColor_out = vec4(0.0);
    mainImage(_fragColor_out, gl_FragCoord.xy);
}
)glsl";
    }

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
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::string infoLog(logLen > 0 ? logLen : 1, '\0');
        glGetShaderInfoLog(shader, static_cast<GLsizei>(infoLog.size()), nullptr, &infoLog[0]);
        lastError_ = "Shader compilation error:\n" + infoLog;
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
        GLint logLen = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &logLen);
        std::string infoLog(logLen > 0 ? logLen : 1, '\0');
        glGetProgramInfoLog(program_, static_cast<GLsizei>(infoLog.size()), nullptr, &infoLog[0]);
        lastError_ = "Shader link error:\n" + infoLog;
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
    uniforms_ = {};
}

void ShaderManager::CacheUniformLocations() {
    if (!program_) return;

    uniforms_.iResolution = glGetUniformLocation(program_, "iResolution");
    uniforms_.iTime = glGetUniformLocation(program_, "iTime");
    uniforms_.iTimeDelta = glGetUniformLocation(program_, "iTimeDelta");
    uniforms_.iFrame = glGetUniformLocation(program_, "iFrame");
    uniforms_.iMouse = glGetUniformLocation(program_, "iMouse");
    uniforms_.iDate = glGetUniformLocation(program_, "iDate");
    uniforms_.iSampleRate = glGetUniformLocation(program_, "iSampleRate");
    uniforms_.iFrameRate = glGetUniformLocation(program_, "iFrameRate");
    uniforms_.iChannelTime = glGetUniformLocation(program_, "iChannelTime");
    uniforms_.iChannelResolution = glGetUniformLocation(program_, "iChannelResolution");
    uniforms_.iClickTime = glGetUniformLocation(program_, "iClickTime");

    uniforms_.iChannel[0] = glGetUniformLocation(program_, "iChannel0");
    uniforms_.iChannel[1] = glGetUniformLocation(program_, "iChannel1");
    uniforms_.iChannel[2] = glGetUniformLocation(program_, "iChannel2");
    uniforms_.iChannel[3] = glGetUniformLocation(program_, "iChannel3");

    uniforms_.cubeFaceRight = glGetUniformLocation(program_, "_cubeFaceRight");
    uniforms_.cubeFaceUp = glGetUniformLocation(program_, "_cubeFaceUp");
    uniforms_.cubeFaceDir = glGetUniformLocation(program_, "_cubeFaceDir");
}
