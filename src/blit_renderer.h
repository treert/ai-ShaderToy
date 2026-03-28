#pragma once

#include <glad/glad.h>

/// BlitRenderer 封装降分辨率渲染的 FBO 管理和 blit（全屏纹理绘制）逻辑。
/// 当 renderScale < 1.0 时启用：shader 先渲染到低分辨率 FBO，再 blit 到屏幕。
class BlitRenderer {
public:
    BlitRenderer();
    ~BlitRenderer();

    BlitRenderer(const BlitRenderer&) = delete;
    BlitRenderer& operator=(const BlitRenderer&) = delete;

    /// 初始化 blit shader 和全屏四边形（只需调用一次）
    bool Init();

    /// 是否已初始化
    bool IsInitialized() const { return initialized_; }

    /// 创建/重建降分辨率 FBO
    /// @param displayWidth  显示分辨率宽度
    /// @param displayHeight 显示分辨率高度
    /// @param renderScale   渲染缩放比例 (0.1~1.0)
    /// @return 是否成功
    bool CreateRenderFBO(int displayWidth, int displayHeight, float renderScale);

    /// 获取降分辨率后的实际渲染尺寸
    int GetRenderWidth() const { return renderWidth_; }
    int GetRenderHeight() const { return renderHeight_; }

    /// 获取渲染 FBO（shader 渲染到这个 FBO）
    GLuint GetRenderFBO() const { return renderFBO_; }

    /// 将降分辨率 FBO 的内容 blit 到指定视口
    /// 调用前应已 glBindFramebuffer(GL_FRAMEBUFFER, 0) 或绑定目标 FBO
    void BlitToScreen(int viewportWidth, int viewportHeight);

    /// 释放所有 OpenGL 资源
    void Cleanup();

private:
    bool initialized_ = false;

    // Blit shader
    GLuint blitProgram_ = 0;
    GLuint blitVAO_ = 0;
    GLuint blitVBO_ = 0;

    // 降分辨率 FBO
    GLuint renderFBO_ = 0;
    GLuint renderTex_ = 0;
    int renderWidth_ = 0;
    int renderHeight_ = 0;
};
