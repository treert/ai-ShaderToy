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

void MultiPassRenderer::SetImageTargetFBO(GLuint fbo) {
    imagePass_.fbo = fbo;
}

void MultiPassRenderer::RenderAllPasses(GLuint quadVAO, float time, float timeDelta,
                                        int frame, const float mouse[4], const float date[4],
                                        int viewportW, int viewportH, float clickTime) {
    // 1. 渲染所有 Buffer pass
    for (auto& pass : bufferPasses_) {
        RenderSinglePass(pass, quadVAO, time, timeDelta, frame,
                        mouse, date, viewportW, viewportH, clickTime);
    }

    // 2. 交换 buffer 双缓冲
    SwapBuffers();

    // 3. 渲染 Image pass（输出到屏幕或降分辨率 FBO）
    RenderSinglePass(imagePass_, quadVAO, time, timeDelta, frame,
                    mouse, date, viewportW, viewportH, clickTime);
}

GLuint MultiPassRenderer::GetBufferOutputTexture(int bufferIndex) const {
    if (bufferIndex >= 0 && bufferIndex < static_cast<int>(bufferPasses_.size())) {
        return bufferPasses_[bufferIndex].outputTexturePrev;
    }
    return 0;
}

std::vector<std::string> MultiPassRenderer::GetPassNames() const {
    std::vector<std::string> names;
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

    // 设置 uniform
    float resW, resH;
    if (pass.fbo && pass.width > 0 && pass.height > 0) {
        resW = static_cast<float>(pass.width);
        resH = static_cast<float>(pass.height);
    } else {
        resW = static_cast<float>(viewportW);
        resH = static_cast<float>(viewportH);
    }
    glUniform3f(pass.shader.GetUniformLocation("iResolution"), resW, resH, 1.0f);
    glUniform1f(pass.shader.GetUniformLocation("iTime"), time);
    glUniform1f(pass.shader.GetUniformLocation("iTimeDelta"), timeDelta);
    glUniform1i(pass.shader.GetUniformLocation("iFrame"), frame);
    glUniform4f(pass.shader.GetUniformLocation("iMouse"),
                mouse[0], mouse[1], mouse[2], mouse[3]);
    glUniform4f(pass.shader.GetUniformLocation("iDate"),
                date[0], date[1], date[2], date[3]);
    glUniform1f(pass.shader.GetUniformLocation("iSampleRate"), 44100.0f);
    float frameRate = (timeDelta > 0.0f) ? (1.0f / timeDelta) : 60.0f;
    glUniform1f(pass.shader.GetUniformLocation("iFrameRate"), frameRate);
    float channelTime[4] = {time, time, time, time};
    glUniform1fv(pass.shader.GetUniformLocation("iChannelTime"), 4, channelTime);
    glUniform1f(pass.shader.GetUniformLocation("iClickTime"), clickTime);

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

        char uniformName[16];
        snprintf(uniformName, sizeof(uniformName), "iChannel%d", i);
        glUniform1i(pass.shader.GetUniformLocation(uniformName), i);
    }
    glUniform3fv(pass.shader.GetUniformLocation("iChannelResolution"), 4, &channelRes[0][0]);

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
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
