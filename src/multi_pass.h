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
    bool isCubeMap = false;     // 是否为 CubeMap pass（渲染到 6 面 cubemap）

    // CubeMap pass 专用：6 面 FBO 和纹理
    GLuint cubeMapTexture = 0;      // 当前帧输出 cubemap 纹理
    GLuint cubeMapTexturePrev = 0;  // 上一帧输出 cubemap 纹理（双缓冲）
    GLuint cubeFBO[6] = {};         // 每个面的 FBO

    // 每个 pass 的 4 个输入通道，值为:
    //   -1 = 无输入
    //   0~3 = Buffer A~D 的输出纹理
    //   100~103 = 外部纹理 iChannel 0~3
    //   200 = CubeMap pass 的输出 (samplerCube)
    std::array<int, 4> inputChannels = {-1, -1, -1, -1};

    // 每个通道的采样器类型（用于正确的 sampler 声明）
    std::array<ChannelType, 4> channelTypes = {
        ChannelType::Texture2D, ChannelType::Texture2D,
        ChannelType::Texture2D, ChannelType::Texture2D
    };
};

/// 外部纹理信息（由 TextureManager 管理的纹理）
struct ExternalTextureInfo {
    GLuint textureId = 0;
    int width = 0;
    int height = 0;
    ChannelType type = ChannelType::Texture2D;
};

/// MultiPassRenderer 管理多 Pass 渲染流程。
/// ShaderToy 支持 Buffer A/B/C/D（最多4个中间 buffer）+ 1个 Image 输出。
/// 也用于单 Pass 渲染（只有 Image pass，无 Buffer pass）。
class MultiPassRenderer {
public:
    MultiPassRenderer();
    ~MultiPassRenderer();

    /// 初始化：设置视口大小
    bool Init(int width, int height);

    /// 清除所有 pass 和 FBO（用于热加载时完全重置）
    void Clear();

    /// 调整视口大小时重新创建 FBO
    void Resize(int width, int height);

    /// 设置 Common 共享代码段（在 AddBufferPass/SetImagePass 之前调用）
    void SetCommonSource(const std::string& common);

    /// 设置外部纹理（由 TextureManager 管理的纹理，用于 inputChannels >= 100 的情况）
    void SetExternalTexture(int channel, GLuint textureId, int width, int height,
                            ChannelType type = ChannelType::Texture2D);

    /// 添加一个 buffer pass (Buffer A/B/C/D)
    /// @param name pass 名称
    /// @param source ShaderToy 格式着色器源码
    /// @param inputs 4个输入通道映射
    /// @param channelTypes 各通道采样器类型
    /// @return pass 索引，失败返回 -1
    int AddBufferPass(const std::string& name, const std::string& source,
                      const std::array<int, 4>& inputs,
                      const std::array<ChannelType, 4>& channelTypes = {
                          ChannelType::Texture2D, ChannelType::Texture2D,
                          ChannelType::Texture2D, ChannelType::Texture2D
                      });

    /// 设置 Image pass（最终输出）
    /// @param targetFBO 目标 FBO（0=屏幕，非0=降分辨率 FBO）
    bool SetImagePass(const std::string& source, const std::array<int, 4>& inputs,
                      const std::array<ChannelType, 4>& channelTypes = {
                          ChannelType::Texture2D, ChannelType::Texture2D,
                          ChannelType::Texture2D, ChannelType::Texture2D
                      });

    /// 设置 CubeMap pass (Cube A)
    /// @param source ShaderToy 格式源码（使用 mainCubemap 函数）
    /// @param inputs 4个输入通道映射
    /// @param channelTypes 各通道采样器类型
    /// @param cubeSize 每面的分辨率（默认 1024）
    /// @return 成功返回 true
    bool SetCubeMapPass(const std::string& source,
                        const std::array<int, 4>& inputs,
                        const std::array<ChannelType, 4>& channelTypes = {
                            ChannelType::Texture2D, ChannelType::Texture2D,
                            ChannelType::Texture2D, ChannelType::Texture2D
                        },
                        int cubeSize = 1024);

    /// 设置 Image pass 的目标 FBO（0=默认帧缓冲即屏幕，非0=降分辨率渲染用 FBO）
    void SetImageTargetFBO(GLuint fbo);

    /// 渲染所有 pass（完整流程，窗口模式使用）
    /// @param quadVAO 全屏四边形 VAO
    /// @param clickTime 最近点击时间（自定义 uniform iClickTime）
    void RenderAllPasses(GLuint quadVAO, float time, float timeDelta,
                         int frame, const float mouse[4], const float date[4],
                         int viewportW, int viewportH, float clickTime = -10.0f);

    /// 仅渲染 Buffer pass + CubeMap pass + 交换双缓冲（壁纸模式：只需调用一次）
    void RenderBufferPasses(GLuint quadVAO, float time, float timeDelta,
                            int frame, const float mouse[4], const float date[4],
                            int viewportW, int viewportH, float clickTime = -10.0f);

    /// 仅渲染 Image pass（壁纸模式：每个显示器各调用一次）
    void RenderImagePass(GLuint quadVAO, float time, float timeDelta,
                         int frame, const float mouse[4], const float date[4],
                         int viewportW, int viewportH, float clickTime = -10.0f);

    /// 获取 buffer pass 数量
    int GetBufferPassCount() const { return static_cast<int>(bufferPasses_.size()); }

    /// 获取指定 buffer 的输出纹理（上一帧，用于其他 pass 读取）
    GLuint GetBufferOutputTexture(int bufferIndex) const;

    /// 获取编译错误信息
    const std::string& GetLastError() const { return lastError_; }

    /// 是否为多 Pass 模式
    bool IsMultiPass() const { return !bufferPasses_.empty() || hasCubeMapPass_; }

    /// 是否有 CubeMap pass
    bool HasCubeMapPass() const { return hasCubeMapPass_; }

    /// 获取 CubeMap pass 的输出纹理（上一帧，samplerCube）
    GLuint GetCubeMapOutputTexture() const;

    /// 获取 Image pass 的 shader（单 Pass 模式下用于外部设置 uniform）
    ShaderManager& GetImageShader() { return imagePass_.shader; }

    /// 获取 buffer pass 名称列表（用于调试 UI）
    std::vector<std::string> GetPassNames() const;

private:
    /// 创建一个 FBO 和对应的颜色附件纹理
    bool CreateFBO(RenderPass& pass, int width, int height);

    /// 创建 CubeMap FBO（6面 × 双缓冲）
    bool CreateCubeMapFBO(RenderPass& pass, int cubeSize);

    /// 销毁 FBO 资源
    void DestroyFBO(RenderPass& pass);

    /// 渲染单个 pass
    void RenderSinglePass(RenderPass& pass, GLuint quadVAO,
                          float time, float timeDelta, int frame,
                          const float mouse[4], const float date[4],
                          int viewportW, int viewportH, float clickTime);

    /// 渲染 CubeMap pass（6 个面）
    void RenderCubeMapPass(RenderPass& pass, GLuint quadVAO,
                           float time, float timeDelta, int frame,
                           const float mouse[4], const float date[4],
                           float clickTime);

    /// 交换双缓冲纹理
    void SwapBuffers();

    std::vector<RenderPass> bufferPasses_;  // Buffer A, B, C, D
    RenderPass imagePass_;                  // 最终 Image pass
    RenderPass cubeMapPass_;                // CubeMap pass (Cube A)
    bool hasCubeMapPass_ = false;           // 是否有 CubeMap pass
    std::string commonSource_;              // Common 共享代码段
    std::string lastError_;
    int width_ = 0;
    int height_ = 0;

    // 外部纹理（inputChannels 100~103 对应 externalTextures_[0~3]）
    std::array<ExternalTextureInfo, 4> externalTextures_;
};
