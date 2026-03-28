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
    /// @param mouse  iMouse 四分量：xy=当前位置, zw=按下瞬间位置(松开后为负)
    /// @param date   iDate 四分量：年/月/日/当天已过秒数
    void RenderFrame(ShaderManager& shader, float time, float timeDelta,
                     int frame, const float mouse[4], const float date[4]);

private:
    /// 创建全屏四边形的 VAO/VBO
    void CreateFullscreenQuad();

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    int viewportWidth_ = 800;
    int viewportHeight_ = 600;
};
