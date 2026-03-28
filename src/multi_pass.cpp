#include "multi_pass.h"
#include <iostream>
#include <algorithm>

MultiPassRenderer::MultiPassRenderer() = default;

MultiPassRenderer::~MultiPassRenderer() {
    Clear();
}

bool MultiPassRenderer::Init(int width, int height) {
    width_ = width;
    height_ = height;
    return true;
}

void MultiPassRenderer::Clear() {
    for (auto& pass : bufferPasses_) {
        DestroyFBO(pass);
    }
    bufferPasses_.clear();

    if (hasCubeMapPass_) {
        DestroyFBO(cubeMapPass_);
        hasCubeMapPass_ = false;
    }
    cubeMapPass_ = RenderPass{};

    // Image pass 的 FBO 可能是外部的（降分辨率 FBO），不由我们销毁
    imagePass_ = RenderPass{};
    commonSource_.clear();
    lastError_.clear();
    externalTextures_ = {};
}

void MultiPassRenderer::Resize(int width, int height) {
    width_ = width;
    height_ = height;

    // 重新创建所有 buffer FBO
    for (auto& pass : bufferPasses_) {
        DestroyFBO(pass);
        CreateFBO(pass, width, height);
    }
    // CubeMap pass 不随窗口大小变化（固定尺寸），不需要 Resize
}

void MultiPassRenderer::SetCommonSource(const std::string& common) {
    commonSource_ = common;
}

void MultiPassRenderer::SetExternalTexture(int channel, GLuint textureId, int width, int height,
                                            ChannelType type) {
    if (channel >= 0 && channel < 4) {
        externalTextures_[channel].textureId = textureId;
        externalTextures_[channel].width = width;
        externalTextures_[channel].height = height;
        externalTextures_[channel].type = type;
    }
}

int MultiPassRenderer::AddBufferPass(const std::string& name, const std::string& source,
                                     const std::array<int, 4>& inputs,
                                     const std::array<ChannelType, 4>& channelTypes) {
    RenderPass pass;
    pass.name = name;
    pass.shaderSource = source;
    pass.inputChannels = inputs;
    pass.channelTypes = channelTypes;

    // 设置通道类型和 Common 代码后编译
    pass.shader.SetChannelTypes(channelTypes);
    pass.shader.SetCommonSource(commonSource_);
    if (!pass.shader.LoadFromSource(source)) {
        lastError_ = "Failed to compile " + name + ": " + pass.shader.GetLastError();
        std::cerr << lastError_ << std::endl;
        return -1;
    }

    if (!CreateFBO(pass, width_, height_)) {
        lastError_ = "Failed to create FBO for " + name;
        return -1;
    }

    int index = static_cast<int>(bufferPasses_.size());
    bufferPasses_.push_back(std::move(pass));
    std::cout << "Buffer pass added: " << name << " (index " << index << ")" << std::endl;
    return index;
}

bool MultiPassRenderer::SetImagePass(const std::string& source,
                                     const std::array<int, 4>& inputs,
                                     const std::array<ChannelType, 4>& channelTypes) {
    imagePass_.name = "Image";
    imagePass_.shaderSource = source;
    imagePass_.inputChannels = inputs;
    imagePass_.channelTypes = channelTypes;
    // imagePass_.fbo 默认为 0（渲染到屏幕），可通过 SetImageTargetFBO 修改

    imagePass_.shader.SetChannelTypes(channelTypes);
    imagePass_.shader.SetCommonSource(commonSource_);
    if (!imagePass_.shader.LoadFromSource(source)) {
        lastError_ = "Failed to compile Image pass: " + imagePass_.shader.GetLastError();
        std::cerr << lastError_ << std::endl;
        return false;
    }

    return true;
}

bool MultiPassRenderer::SetCubeMapPass(const std::string& source,
                                        const std::array<int, 4>& inputs,
                                        const std::array<ChannelType, 4>& channelTypes,
                                        int cubeSize) {
    cubeMapPass_ = RenderPass{};
    cubeMapPass_.name = "Cube A";
    cubeMapPass_.shaderSource = source;
    cubeMapPass_.inputChannels = inputs;
    cubeMapPass_.channelTypes = channelTypes;
    cubeMapPass_.isCubeMap = true;

    // 编译 shader（CubeMap 模式）
    cubeMapPass_.shader.SetChannelTypes(channelTypes);
    cubeMapPass_.shader.SetCommonSource(commonSource_);
    cubeMapPass_.shader.SetCubeMapPassMode(true);
    if (!cubeMapPass_.shader.LoadFromSource(source)) {
        lastError_ = "Failed to compile Cube A pass: " + cubeMapPass_.shader.GetLastError();
        std::cerr << lastError_ << std::endl;
        return false;
    }

    if (!CreateCubeMapFBO(cubeMapPass_, cubeSize)) {
        lastError_ = "Failed to create CubeMap FBO for Cube A";
        return false;
    }

    hasCubeMapPass_ = true;
    std::cout << "CubeMap pass added: Cube A (" << cubeSize << "x" << cubeSize << " per face)" << std::endl;
    return true;
}

bool MultiPassRenderer::CreateCubeMapFBO(RenderPass& pass, int cubeSize) {
    pass.width = cubeSize;
    pass.height = cubeSize;

    // 创建两个 cubemap 纹理（双缓冲）
    GLuint textures[2];
    glGenTextures(2, textures);
    for (int t = 0; t < 2; ++t) {
        glBindTexture(GL_TEXTURE_CUBE_MAP, textures[t]);
        for (int face = 0; face < 6; ++face) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA16F,
                         cubeSize, cubeSize, 0, GL_RGBA, GL_FLOAT, nullptr);
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    pass.cubeMapTexture = textures[0];
    pass.cubeMapTexturePrev = textures[1];

    // 为每个面创建 FBO（渲染到 cubeMapTexture 的每个面）
    glGenFramebuffers(6, pass.cubeFBO);
    for (int face = 0; face < 6; ++face) {
        glBindFramebuffer(GL_FRAMEBUFFER, pass.cubeFBO[face]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                               pass.cubeMapTexture, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "CubeMap FBO not complete for face " << face << std::endl;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }

        // 清空初始内容
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    return true;
}

void MultiPassRenderer::SetImageTargetFBO(GLuint fbo) {
    imagePass_.fbo = fbo;
}

void MultiPassRenderer::RenderAllPasses(GLuint quadVAO, float time, float timeDelta,
                                        int frame, const float mouse[4], const float date[4],
                                        int viewportW, int viewportH, float clickTime) {
    RenderBufferPasses(quadVAO, time, timeDelta, frame, mouse, date, viewportW, viewportH, clickTime);
    RenderImagePass(quadVAO, time, timeDelta, frame, mouse, date, viewportW, viewportH, clickTime);
}

void MultiPassRenderer::RenderBufferPasses(GLuint quadVAO, float time, float timeDelta,
                                           int frame, const float mouse[4], const float date[4],
                                           int viewportW, int viewportH, float clickTime) {
    // 0. 渲染 CubeMap pass（在 Buffer passes 之前，因为 Buffer 可能采样 cubemap）
    if (hasCubeMapPass_) {
        RenderCubeMapPass(cubeMapPass_, quadVAO, time, timeDelta, frame,
                          mouse, date, clickTime);
    }

    // 1. 渲染所有 Buffer pass
    for (auto& pass : bufferPasses_) {
        RenderSinglePass(pass, quadVAO, time, timeDelta, frame,
                        mouse, date, viewportW, viewportH, clickTime);
    }

    // 2. 交换 buffer 双缓冲
    SwapBuffers();
}

void MultiPassRenderer::RenderImagePass(GLuint quadVAO, float time, float timeDelta,
                                        int frame, const float mouse[4], const float date[4],
                                        int viewportW, int viewportH, float clickTime) {
    // 渲染 Image pass（输出到屏幕或降分辨率 FBO）
    RenderSinglePass(imagePass_, quadVAO, time, timeDelta, frame,
                    mouse, date, viewportW, viewportH, clickTime);
}

GLuint MultiPassRenderer::GetBufferOutputTexture(int bufferIndex) const {
    if (bufferIndex >= 0 && bufferIndex < static_cast<int>(bufferPasses_.size())) {
        return bufferPasses_[bufferIndex].outputTexturePrev;
    }
    return 0;
}

GLuint MultiPassRenderer::GetCubeMapOutputTexture() const {
    if (hasCubeMapPass_) {
        return cubeMapPass_.cubeMapTexturePrev;
    }
    return 0;
}

std::vector<std::string> MultiPassRenderer::GetPassNames() const {
    std::vector<std::string> names;
    if (hasCubeMapPass_) {
        names.push_back("Cube A");
    }
    for (const auto& pass : bufferPasses_) {
        names.push_back(pass.name);
    }
    names.push_back(imagePass_.name.empty() ? "Image" : imagePass_.name);
    return names;
}

bool MultiPassRenderer::CreateFBO(RenderPass& pass, int width, int height) {
    pass.width = width;
    pass.height = height;

    // 创建两个纹理（双缓冲）
    GLuint textures[2];
    glGenTextures(2, textures);
    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0,
                     GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    pass.outputTexture = textures[0];
    pass.outputTexturePrev = textures[1];

    // 创建 FBO
    glGenFramebuffers(1, &pass.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, pass.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, pass.outputTexture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "FBO not complete for " << pass.name << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    // 清空初始内容
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

void MultiPassRenderer::DestroyFBO(RenderPass& pass) {
    if (pass.isCubeMap) {
        // CubeMap pass: 销毁 6 面 FBO 和 cubemap 纹理
        for (int i = 0; i < 6; ++i) {
            if (pass.cubeFBO[i]) {
                glDeleteFramebuffers(1, &pass.cubeFBO[i]);
                pass.cubeFBO[i] = 0;
            }
        }
        if (pass.cubeMapTexture) {
            glDeleteTextures(1, &pass.cubeMapTexture);
            pass.cubeMapTexture = 0;
        }
        if (pass.cubeMapTexturePrev) {
            glDeleteTextures(1, &pass.cubeMapTexturePrev);
            pass.cubeMapTexturePrev = 0;
        }
    } else {
        if (pass.fbo) {
            glDeleteFramebuffers(1, &pass.fbo);
            pass.fbo = 0;
        }
        if (pass.outputTexture) {
            glDeleteTextures(1, &pass.outputTexture);
            pass.outputTexture = 0;
        }
        if (pass.outputTexturePrev) {
            glDeleteTextures(1, &pass.outputTexturePrev);
            pass.outputTexturePrev = 0;
        }
    }
}

void MultiPassRenderer::RenderSinglePass(RenderPass& pass, GLuint quadVAO,
                                          float time, float timeDelta, int frame,
                                          const float mouse[4], const float date[4],
                                          int viewportW, int viewportH, float clickTime) {
    // 绑定 FBO（Image pass 时 fbo=0 即渲染到屏幕，或降分辨率 FBO）
    glBindFramebuffer(GL_FRAMEBUFFER, pass.fbo);
    if (pass.fbo && pass.width > 0 && pass.height > 0) {
        glViewport(0, 0, pass.width, pass.height);
    } else {
        glViewport(0, 0, viewportW, viewportH);
    }
    glClear(GL_COLOR_BUFFER_BIT);

    pass.shader.Use();

    // 使用缓存的 uniform locations
    const auto& loc = pass.shader.GetCachedLocations();

    // 设置 uniform
    float resW, resH;
    if (pass.fbo && pass.width > 0 && pass.height > 0) {
        resW = static_cast<float>(pass.width);
        resH = static_cast<float>(pass.height);
    } else {
        resW = static_cast<float>(viewportW);
        resH = static_cast<float>(viewportH);
    }
    glUniform3f(loc.iResolution, resW, resH, 1.0f);
    glUniform1f(loc.iTime, time);
    glUniform1f(loc.iTimeDelta, timeDelta);
    glUniform1i(loc.iFrame, frame);
    glUniform4f(loc.iMouse, mouse[0], mouse[1], mouse[2], mouse[3]);
    glUniform4f(loc.iDate, date[0], date[1], date[2], date[3]);
    glUniform1f(loc.iSampleRate, 44100.0f);
    float frameRate = (timeDelta > 0.0f) ? (1.0f / timeDelta) : 60.0f;
    glUniform1f(loc.iFrameRate, frameRate);
    float channelTime[4] = {time, time, time, time};
    glUniform1fv(loc.iChannelTime, 4, channelTime);
    glUniform1f(loc.iClickTime, clickTime);

    // 设置 iChannelResolution 和绑定输入纹理到 iChannel0~3
    float channelRes[4][3] = {};
    for (int i = 0; i < 4; ++i) {
        int input = pass.inputChannels[i];
        glActiveTexture(GL_TEXTURE0 + i);

        if (input >= 0 && input < static_cast<int>(bufferPasses_.size())) {
            // 输入来自 buffer pass —— 使用上一帧的输出
            glBindTexture(GL_TEXTURE_2D, bufferPasses_[input].outputTexturePrev);
            channelRes[i][0] = static_cast<float>(bufferPasses_[input].width);
            channelRes[i][1] = static_cast<float>(bufferPasses_[input].height);
            channelRes[i][2] = 1.0f;
        } else if (input == 200 && hasCubeMapPass_) {
            // 输入来自 CubeMap pass 输出（samplerCube）
            glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapPass_.cubeMapTexturePrev);
            channelRes[i][0] = static_cast<float>(cubeMapPass_.width);
            channelRes[i][1] = static_cast<float>(cubeMapPass_.height);
            channelRes[i][2] = 1.0f;
        } else if (input >= 100 && input <= 103) {
            // 外部纹理
            int extIdx = input - 100;
            const auto& ext = externalTextures_[extIdx];
            if (ext.textureId) {
                GLenum target = (ext.type == ChannelType::CubeMap) ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
                glBindTexture(target, ext.textureId);
                channelRes[i][0] = static_cast<float>(ext.width);
                channelRes[i][1] = static_cast<float>(ext.height);
                channelRes[i][2] = 1.0f;
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        } else {
            // 无输入
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        glUniform1i(loc.iChannel[i], i);
    }
    glUniform3fv(loc.iChannelResolution, 4, &channelRes[0][0]);

    // 绘制全屏四边形
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    // 恢复默认 framebuffer
    if (pass.fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

void MultiPassRenderer::SwapBuffers() {
    for (auto& pass : bufferPasses_) {
        std::swap(pass.outputTexture, pass.outputTexturePrev);
        // 重新绑定 FBO 到新的 outputTexture
        glBindFramebuffer(GL_FRAMEBUFFER, pass.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, pass.outputTexture, 0);
    }
    // CubeMap pass 双缓冲交换
    if (hasCubeMapPass_) {
        std::swap(cubeMapPass_.cubeMapTexture, cubeMapPass_.cubeMapTexturePrev);
        // 重新绑定所有 6 面 FBO 到新的 cubeMapTexture
        for (int face = 0; face < 6; ++face) {
            glBindFramebuffer(GL_FRAMEBUFFER, cubeMapPass_.cubeFBO[face]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                                   cubeMapPass_.cubeMapTexture, 0);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void MultiPassRenderer::RenderCubeMapPass(RenderPass& pass, GLuint quadVAO,
                                           float time, float timeDelta, int frame,
                                           const float mouse[4], const float date[4],
                                           float clickTime) {
    // CubeMap 6 个面的方向矩阵（right, up, dir）
    // OpenGL cubemap face 顺序: +X, -X, +Y, -Y, +Z, -Z
    struct CubeFace {
        float right[3];  // 面的 X 轴方向
        float up[3];     // 面的 Y 轴方向
        float dir[3];    // 面的法线方向（指向面中心）
    };
    static const CubeFace faces[6] = {
        // +X: right = -Z, up = -Y, dir = +X
        {{ 0, 0,-1}, { 0,-1, 0}, { 1, 0, 0}},
        // -X: right = +Z, up = -Y, dir = -X
        {{ 0, 0, 1}, { 0,-1, 0}, {-1, 0, 0}},
        // +Y: right = +X, up = +Z, dir = +Y
        {{ 1, 0, 0}, { 0, 0, 1}, { 0, 1, 0}},
        // -Y: right = +X, up = -Z, dir = -Y
        {{ 1, 0, 0}, { 0, 0,-1}, { 0,-1, 0}},
        // +Z: right = +X, up = -Y, dir = +Z
        {{ 1, 0, 0}, { 0,-1, 0}, { 0, 0, 1}},
        // -Z: right = -X, up = -Y, dir = -Z
        {{-1, 0, 0}, { 0,-1, 0}, { 0, 0,-1}},
    };

    pass.shader.Use();

    // 使用缓存的 uniform locations
    const auto& loc = pass.shader.GetCachedLocations();

    // 设置通用 uniform
    float resW = static_cast<float>(pass.width);
    float resH = static_cast<float>(pass.height);
    glUniform3f(loc.iResolution, resW, resH, 1.0f);
    glUniform1f(loc.iTime, time);
    glUniform1f(loc.iTimeDelta, timeDelta);
    glUniform1i(loc.iFrame, frame);
    glUniform4f(loc.iMouse, mouse[0], mouse[1], mouse[2], mouse[3]);
    glUniform4f(loc.iDate, date[0], date[1], date[2], date[3]);
    glUniform1f(loc.iSampleRate, 44100.0f);
    float frameRate = (timeDelta > 0.0f) ? (1.0f / timeDelta) : 60.0f;
    glUniform1f(loc.iFrameRate, frameRate);
    float channelTime[4] = {time, time, time, time};
    glUniform1fv(loc.iChannelTime, 4, channelTime);
    glUniform1f(loc.iClickTime, clickTime);

    // 绑定输入纹理
    float channelRes[4][3] = {};
    for (int i = 0; i < 4; ++i) {
        int input = pass.inputChannels[i];
        glActiveTexture(GL_TEXTURE0 + i);

        if (input >= 0 && input < static_cast<int>(bufferPasses_.size())) {
            glBindTexture(GL_TEXTURE_2D, bufferPasses_[input].outputTexturePrev);
            channelRes[i][0] = static_cast<float>(bufferPasses_[input].width);
            channelRes[i][1] = static_cast<float>(bufferPasses_[input].height);
            channelRes[i][2] = 1.0f;
        } else if (input == 200 && hasCubeMapPass_) {
            // CubeMap 自引用（上一帧）
            glBindTexture(GL_TEXTURE_CUBE_MAP, pass.cubeMapTexturePrev);
            channelRes[i][0] = static_cast<float>(pass.width);
            channelRes[i][1] = static_cast<float>(pass.height);
            channelRes[i][2] = 1.0f;
        } else if (input >= 100 && input <= 103) {
            int extIdx = input - 100;
            const auto& ext = externalTextures_[extIdx];
            if (ext.textureId) {
                GLenum target = (ext.type == ChannelType::CubeMap) ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
                glBindTexture(target, ext.textureId);
                channelRes[i][0] = static_cast<float>(ext.width);
                channelRes[i][1] = static_cast<float>(ext.height);
                channelRes[i][2] = 1.0f;
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        glUniform1i(loc.iChannel[i], i);
    }
    glUniform3fv(loc.iChannelResolution, 4, &channelRes[0][0]);

    // 渲染 6 个面
    for (int face = 0; face < 6; ++face) {
        glBindFramebuffer(GL_FRAMEBUFFER, pass.cubeFBO[face]);
        glViewport(0, 0, pass.width, pass.height);
        glClear(GL_COLOR_BUFFER_BIT);

        // 设置面方向 uniform
        glUniform3fv(loc.cubeFaceRight, 1, faces[face].right);
        glUniform3fv(loc.cubeFaceUp,    1, faces[face].up);
        glUniform3fv(loc.cubeFaceDir,   1, faces[face].dir);

        // 绘制全屏四边形
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
