#include "blit_renderer.h"
#include <iostream>

BlitRenderer::BlitRenderer() = default;

BlitRenderer::~BlitRenderer() {
    Cleanup();
}

bool BlitRenderer::Init() {
    if (initialized_) return true;

    // 创建 blit 着色器
    const char* blitVS =
        "#version 330 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "out vec2 vUV;\n"
        "void main(){\n"
        "  vUV = aPos * 0.5 + 0.5;\n"
        "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n";
    const char* blitFS =
        "#version 330 core\n"
        "in vec2 vUV;\n"
        "out vec4 fragColor;\n"
        "uniform sampler2D uTex;\n"
        "void main(){\n"
        "  fragColor = texture(uTex, vUV);\n"
        "}\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &blitVS, nullptr);
    glCompileShader(vs);
    GLint success;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
    if (!success) {
        std::cerr << "BlitRenderer: vertex shader compile failed." << std::endl;
        glDeleteShader(vs);
        return false;
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &blitFS, nullptr);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
    if (!success) {
        std::cerr << "BlitRenderer: fragment shader compile failed." << std::endl;
        glDeleteShader(vs);
        glDeleteShader(fs);
        return false;
    }

    blitProgram_ = glCreateProgram();
    glAttachShader(blitProgram_, vs);
    glAttachShader(blitProgram_, fs);
    glLinkProgram(blitProgram_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGetProgramiv(blitProgram_, GL_LINK_STATUS, &success);
    if (!success) {
        std::cerr << "BlitRenderer: program link failed." << std::endl;
        glDeleteProgram(blitProgram_);
        blitProgram_ = 0;
        return false;
    }

    // 全屏四边形
    float blitQuad[] = {-1,-1, 1,-1, -1,1, 1,-1, 1,1, -1,1};
    glGenVertexArrays(1, &blitVAO_);
    glGenBuffers(1, &blitVBO_);
    glBindVertexArray(blitVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, blitVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(blitQuad), blitQuad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // 缓存 uniform location
    uTexLocation_ = glGetUniformLocation(blitProgram_, "uTex");

    initialized_ = true;
    return true;
}

bool BlitRenderer::CreateRenderFBO(int displayWidth, int displayHeight, float renderScale) {
    renderWidth_ = static_cast<int>(displayWidth * renderScale);
    renderHeight_ = static_cast<int>(displayHeight * renderScale);
    if (renderWidth_ < 1) renderWidth_ = 1;
    if (renderHeight_ < 1) renderHeight_ = 1;

    if (renderFBO_) {
        glDeleteFramebuffers(1, &renderFBO_);
        glDeleteTextures(1, &renderTex_);
        renderFBO_ = 0;
        renderTex_ = 0;
    }

    glGenFramebuffers(1, &renderFBO_);
    glGenTextures(1, &renderTex_);

    glBindTexture(GL_TEXTURE_2D, renderTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, renderWidth_, renderHeight_, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, renderFBO_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTex_, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "BlitRenderer: FBO incomplete!" << std::endl;
        glDeleteFramebuffers(1, &renderFBO_);
        glDeleteTextures(1, &renderTex_);
        renderFBO_ = 0;
        renderTex_ = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void BlitRenderer::BlitToScreen(int viewportWidth, int viewportHeight) {
    if (!initialized_ || !renderFBO_) return;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, viewportWidth, viewportHeight);
    glUseProgram(blitProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderTex_);
    glUniform1i(uTexLocation_, 0);
    glBindVertexArray(blitVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

// ---- Pause Snapshot Implementation ----

bool BlitRenderer::CaptureSnapshot(int width, int height) {
    // Auto-initialize blit shader if not yet initialized (needed for snapshot-only usage)
    if (!initialized_) {
        if (!Init()) return false;
    }

    // Recreate snapshot texture if size changed
    if (snapshotTex_ == 0 || snapshotWidth_ != width || snapshotHeight_ != height) {
        if (snapshotFBO_) {
            glDeleteFramebuffers(1, &snapshotFBO_);
            glDeleteTextures(1, &snapshotTex_);
            snapshotFBO_ = 0;
            snapshotTex_ = 0;
        }

        glGenFramebuffers(1, &snapshotFBO_);
        glGenTextures(1, &snapshotTex_);

        glBindTexture(GL_TEXTURE_2D, snapshotTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, snapshotFBO_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, snapshotTex_, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "BlitRenderer: Snapshot FBO incomplete!" << std::endl;
            glDeleteFramebuffers(1, &snapshotFBO_);
            glDeleteTextures(1, &snapshotTex_);
            snapshotFBO_ = 0;
            snapshotTex_ = 0;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        snapshotWidth_ = width;
        snapshotHeight_ = height;
    }

    // Copy default framebuffer (back buffer) to snapshot FBO
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, snapshotFBO_);
    glBlitFramebuffer(0, 0, width, height,
                      0, 0, width, height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    snapshotValid_ = true;
    return true;
}

void BlitRenderer::BlitSnapshotToScreen(int viewportWidth, int viewportHeight) {
    if (!snapshotValid_ || !snapshotTex_) return;
    // Auto-initialize blit shader if needed
    if (!initialized_) {
        if (!Init()) return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, viewportWidth, viewportHeight);
    glUseProgram(blitProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, snapshotTex_);
    glUniform1i(uTexLocation_, 0);
    glBindVertexArray(blitVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void BlitRenderer::Cleanup() {
    if (renderFBO_) {
        glDeleteFramebuffers(1, &renderFBO_);
        glDeleteTextures(1, &renderTex_);
        renderFBO_ = 0;
        renderTex_ = 0;
    }
    if (snapshotFBO_) {
        glDeleteFramebuffers(1, &snapshotFBO_);
        glDeleteTextures(1, &snapshotTex_);
        snapshotFBO_ = 0;
        snapshotTex_ = 0;
    }
    snapshotValid_ = false;
    if (blitProgram_) {
        glDeleteProgram(blitProgram_);
        blitProgram_ = 0;
    }
    if (blitVAO_) {
        glDeleteVertexArrays(1, &blitVAO_);
        glDeleteBuffers(1, &blitVBO_);
        blitVAO_ = 0;
        blitVBO_ = 0;
    }
    initialized_ = false;
}
