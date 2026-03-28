#pragma once

#include <glad/glad.h>

/// Renderer 管理全屏四边形 VAO，供 MultiPassRenderer 使用。
class Renderer {
public:
    Renderer();
    ~Renderer();

    /// 初始化 OpenGL 资源（VAO/VBO）
    bool Init();

    /// 设置视口大小
    void SetViewport(int width, int height);

    /// 获取全屏四边形 VAO（供 MultiPassRenderer 等外部使用）
    GLuint GetQuadVAO() const { return vao_; }

private:
    /// 创建全屏四边形的 VAO/VBO
    void CreateFullscreenQuad();

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
};
