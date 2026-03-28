#pragma once

#include <string>
#include <vector>
#include <array>
#include <glad/glad.h>
#include "shader_manager.h"

/// RenderPass 表示一个渲染 pass（Buffer A/B/C/D 或 Image）
struct RenderPass {
    std::string name;           // "Image", "Buffer A", "Buffer B", ...
    std::string shaderSource;   // ShaderToy 着色器源码
    ShaderManager shader;       // 编译后的 shader program

    GLuint fbo = 0;             // Framebuffer Object（Image pass 为 0，直接渲染到屏幕）
    GLuint outputTexture = 0;   // FBO 颜色附件纹理（双缓冲用 [0]）
    GLuint outputTexturePrev = 0; // 上一帧的输出纹理（双缓冲用）
    int width = 0;
    int height = 0;

    // 每个 pass 的 4 个输入通道，值为:
    //   -1 = 无输入
    //   0~3 = Buffer A~D 的输出纹理
    //   100~103 = 外部纹理 iChannel 0~3
    std::array<int, 4> inputChannels = {-1, -1, -1, -1};
};

/// MultiPassRenderer 管理多 Pass 渲染流程。
/// ShaderToy 支持 Buffer A/B/C/D（最多4个中间 buffer）+ 1个 Image 输出。
class MultiPassRenderer {
public:
    MultiPassRenderer();
    ~MultiPassRenderer();

    /// 初始化：设置视口大小，创建 FBO
    bool Init(int width, int height);

    /// 调整视口大小时重新创建 FBO
    void Resize(int width, int height);

    /// 添加一个 buffer pass (Buffer A/B/C/D)
    /// @param name pass 名称
    /// @param source ShaderToy 格式着色器源码
    /// @param inputs 4个输入通道映射
    /// @return pass 索引，失败返回 -1
    int AddBufferPass(const std::string& name, const std::string& source,
                      const std::array<int, 4>& inputs);

    /// 设置 Image pass（最终输出）
    bool SetImagePass(const std::string& source, const std::array<int, 4>& inputs);

    /// 渲染所有 pass
    void RenderAllPasses(GLuint quadVAO, float time, float timeDelta,
                         int frame, const float mouse[4], const float date[4],
                         int viewportW, int viewportH);

    /// 获取 buffer pass 数量
    int GetBufferPassCount() const { return static_cast<int>(bufferPasses_.size()); }

    /// 获取指定 buffer 的输出纹理（当前帧）
    GLuint GetBufferOutputTexture(int bufferIndex) const;

    /// 获取编译错误信息
    const std::string& GetLastError() const { return lastError_; }

private:
    /// 创建一个 FBO 和对应的颜色附件纹理
    bool CreateFBO(RenderPass& pass, int width, int height);

    /// 销毁 FBO 资源
    void DestroyFBO(RenderPass& pass);

    /// 渲染单个 pass
    void RenderSinglePass(RenderPass& pass, GLuint quadVAO,
                          float time, float timeDelta, int frame,
                          const float mouse[4], const float date[4],
                          int viewportW, int viewportH);

    /// 交换双缓冲纹理
    void SwapBuffers();

    std::vector<RenderPass> bufferPasses_;  // Buffer A, B, C, D
    RenderPass imagePass_;                  // 最终 Image pass
    std::string lastError_;
    int width_ = 0;
    int height_ = 0;
};
