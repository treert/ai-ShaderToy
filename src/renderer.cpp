#include "renderer.h"
#include <iostream>

// 全屏四边形的顶点（两个三角形覆盖整个 NDC）
static const float kQuadVertices[] = {
    // 第一个三角形
    -1.0f, -1.0f,
     1.0f, -1.0f,
    -1.0f,  1.0f,
    // 第二个三角形
     1.0f, -1.0f,
     1.0f,  1.0f,
    -1.0f,  1.0f,
};

Renderer::Renderer() = default;

Renderer::~Renderer() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
}

bool Renderer::Init() {
    CreateFullscreenQuad();
    return (vao_ != 0);
}

void Renderer::SetViewport(int width, int height) {
    viewportWidth_ = width;
    viewportHeight_ = height;
    glViewport(0, 0, width, height);
}

void Renderer::RenderFrame(ShaderManager& shader, float time, float timeDelta,
                           int frame, float mouseX, float mouseY,
                           bool mousePressed) {
    glClear(GL_COLOR_BUFFER_BIT);

    shader.Use();

    // 设置 ShaderToy uniform 变量
    glUniform3f(shader.GetUniformLocation("iResolution"),
                static_cast<float>(viewportWidth_),
                static_cast<float>(viewportHeight_), 1.0f);
    glUniform1f(shader.GetUniformLocation("iTime"), time);
    glUniform1f(shader.GetUniformLocation("iTimeDelta"), timeDelta);
    glUniform1i(shader.GetUniformLocation("iFrame"), frame);
    glUniform4f(shader.GetUniformLocation("iMouse"),
                mouseX, mouseY,
                mousePressed ? mouseX : 0.0f,
                mousePressed ? mouseY : 0.0f);

    // 绘制全屏四边形
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void Renderer::CreateFullscreenQuad() {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}
