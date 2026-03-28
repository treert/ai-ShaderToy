#include "multi_pass.h"
#include <iostream>

MultiPassRenderer::MultiPassRenderer() = default;

MultiPassRenderer::~MultiPassRenderer() {
    for (auto& pass : bufferPasses_) {
        DestroyFBO(pass);
    }
    // imagePass_ 没有 FBO（直接渲染到屏幕）
}

bool MultiPassRenderer::Init(int width, int height) {
    width_ = width;
    height_ = height;
    return true;
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

int MultiPassRenderer::AddBufferPass(const std::string& name, const std::string& source,
                                     const std::array<int, 4>& inputs) {
    RenderPass pass;
    pass.name = name;
    pass.shaderSource = source;
    pass.inputChannels = inputs;

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
                                     const std::array<int, 4>& inputs) {
    imagePass_.name = "Image";
    imagePass_.shaderSource = source;
    imagePass_.inputChannels = inputs;
    imagePass_.fbo = 0;  // 渲染到屏幕

    if (!imagePass_.shader.LoadFromSource(source)) {
        lastError_ = "Failed to compile Image pass: " + imagePass_.shader.GetLastError();
        std::cerr << lastError_ << std::endl;
        return false;
    }

    return true;
}

void MultiPassRenderer::RenderAllPasses(GLuint quadVAO, float time, float timeDelta,
                                        int frame, const float mouse[4], const float date[4],
                                        int viewportW, int viewportH) {
    // 1. 渲染所有 Buffer pass
    for (auto& pass : bufferPasses_) {
        RenderSinglePass(pass, quadVAO, time, timeDelta, frame,
                        mouse, date, viewportW, viewportH);
    }

    // 2. 交换 buffer 双缓冲
    SwapBuffers();

    // 3. 渲染 Image pass（输出到屏幕）
    RenderSinglePass(imagePass_, quadVAO, time, timeDelta, frame,
                    mouse, date, viewportW, viewportH);
}

GLuint MultiPassRenderer::GetBufferOutputTexture(int bufferIndex) const {
    if (bufferIndex >= 0 && bufferIndex < static_cast<int>(bufferPasses_.size())) {
        return bufferPasses_[bufferIndex].outputTexturePrev;
    }
    return 0;
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
                                          int viewportW, int viewportH) {
    // 绑定 FBO（Image pass 时 fbo=0 即渲染到屏幕）
    glBindFramebuffer(GL_FRAMEBUFFER, pass.fbo);
    if (pass.fbo) {
        glViewport(0, 0, pass.width, pass.height);
    } else {
        glViewport(0, 0, viewportW, viewportH);
    }
    glClear(GL_COLOR_BUFFER_BIT);

    pass.shader.Use();

    // 设置 uniform
    float resW = pass.fbo ? static_cast<float>(pass.width) : static_cast<float>(viewportW);
    float resH = pass.fbo ? static_cast<float>(pass.height) : static_cast<float>(viewportH);
    glUniform3f(pass.shader.GetUniformLocation("iResolution"), resW, resH, 1.0f);
    glUniform1f(pass.shader.GetUniformLocation("iTime"), time);
    glUniform1f(pass.shader.GetUniformLocation("iTimeDelta"), timeDelta);
    glUniform1i(pass.shader.GetUniformLocation("iFrame"), frame);
    glUniform4f(pass.shader.GetUniformLocation("iMouse"),
                mouse[0], mouse[1], mouse[2], mouse[3]);
    glUniform4f(pass.shader.GetUniformLocation("iDate"),
                date[0], date[1], date[2], date[3]);

    // 绑定输入纹理到 iChannel0~3
    for (int i = 0; i < 4; ++i) {
        int input = pass.inputChannels[i];
        glActiveTexture(GL_TEXTURE0 + i);

        if (input >= 0 && input < static_cast<int>(bufferPasses_.size())) {
            // 输入来自 buffer pass —— 使用上一帧的输出
            glBindTexture(GL_TEXTURE_2D, bufferPasses_[input].outputTexturePrev);
        } else {
            // 无输入或外部纹理（由外部绑定）
            if (input < 100) {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            // input >= 100 的情况由外部 TextureManager 绑定，此处不覆盖
        }

        char uniformName[16];
        snprintf(uniformName, sizeof(uniformName), "iChannel%d", i);
        glUniform1i(pass.shader.GetUniformLocation(uniformName), i);
    }

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
