#include "renderer.h"
#include <glad/glad.h>

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
    glViewport(0, 0, width, height);
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
