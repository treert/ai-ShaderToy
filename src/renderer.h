#pragma once

#include <glad/glad.h>
#include <SDL.h>
#include "shader_manager.h"

/// Renderer 负责 OpenGL 渲染循环。
/// 管理全屏四边形 VAO、uniform 更新和帧渲染。
class Renderer {
public:
    Renderer();
    ~Renderer();

    /// 初始化 OpenGL 资源（VAO/VBO）
    bool Init();

    /// 设置视口大小
    void SetViewport(int width, int height);

    /// 渲染一帧
    void RenderFrame(ShaderManager& shader, float time, float timeDelta,
                     int frame, float mouseX, float mouseY,
                     bool mousePressed);

private:
    /// 创建全屏四边形的 VAO/VBO
    void CreateFullscreenQuad();

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    int viewportWidth_ = 800;
    int viewportHeight_ = 600;
};
