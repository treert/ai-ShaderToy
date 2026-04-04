#pragma once

#ifdef _WIN32

#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <array>
#include "d3d11_shader_manager.h"
#include "shader_manager.h"  // for ChannelType

using Microsoft::WRL::ComPtr;

/// D3D11 渲染 Pass
struct D3D11RenderPass {
    std::string name;
    std::string shaderSource;       // 原始 GLSL 源码
    D3D11ShaderManager shader;

    // Buffer pass FBO（RTV + 双缓冲纹理）
    ComPtr<ID3D11Texture2D> outputTexture;
    ComPtr<ID3D11Texture2D> outputTexturePrev;
    ComPtr<ID3D11RenderTargetView> outputRTV;
    ComPtr<ID3D11ShaderResourceView> outputSRV;
    ComPtr<ID3D11ShaderResourceView> outputSRVPrev;
    int width = 0;
    int height = 0;
    bool isCubeMap = false;

    // CubeMap pass（6面）
    ComPtr<ID3D11Texture2D> cubeMapTexture;
    ComPtr<ID3D11Texture2D> cubeMapTexturePrev;
    ComPtr<ID3D11ShaderResourceView> cubeMapSRV;
    ComPtr<ID3D11ShaderResourceView> cubeMapSRVPrev;
    ComPtr<ID3D11RenderTargetView> cubeFaceRTV[6];

    std::array<int, 4> inputChannels = {-1, -1, -1, -1};
    std::array<ChannelType, 4> channelTypes = {
        ChannelType::Texture2D, ChannelType::Texture2D,
        ChannelType::Texture2D, ChannelType::Texture2D
    };
};

/// D3D11 外部纹理信息
struct D3D11ExternalTextureInfo {
    ID3D11ShaderResourceView* srv = nullptr;
    int width = 0;
    int height = 0;
    ChannelType type = ChannelType::Texture2D;
};

/// D3D11MultiPass 管理 D3D11 多 Pass 渲染，与 MultiPassRenderer 接口对齐。
class D3D11MultiPass {
public:
    D3D11MultiPass();
    ~D3D11MultiPass();

    /// 设置 D3D11 设备和渲染器
    void SetDevice(ID3D11Device* device, ID3D11DeviceContext* context);

    /// 初始化
    bool Init(int width, int height);

    /// 清除所有 pass
    void Clear();

    /// 调整大小
    void Resize(int width, int height);

    /// 设置 Common 源码
    void SetCommonSource(const std::string& common);

    /// 设置外部纹理 SRV
    void SetExternalTexture(int channel, ID3D11ShaderResourceView* srv,
                            int width, int height, ChannelType type = ChannelType::Texture2D);

    /// 添加 Buffer pass
    int AddBufferPass(const std::string& name, const std::string& source,
                      const std::array<int, 4>& inputs,
                      const std::array<ChannelType, 4>& channelTypes,
                      bool isHlsl = false,
                      const std::string& shaderDir = "",
                      const std::string& assetsDir = "");

    /// 设置 Image pass
    bool SetImagePass(const std::string& source, const std::array<int, 4>& inputs,
                      const std::array<ChannelType, 4>& channelTypes,
                      bool isHlsl = false,
                      const std::string& shaderDir = "",
                      const std::string& assetsDir = "");

    /// 设置 CubeMap pass
    bool SetCubeMapPass(const std::string& source, const std::array<int, 4>& inputs,
                        const std::array<ChannelType, 4>& channelTypes, int cubeSize = 1024,
                        bool isHlsl = false,
                        const std::string& shaderDir = "",
                        const std::string& assetsDir = "");

    /// 设置 Image pass 目标 RTV（nullptr = 渲染到 SwapChain back buffer）
    void SetImageTargetRTV(ID3D11RenderTargetView* rtv, int width = 0, int height = 0);

    /// 渲染所有 pass
    void RenderAllPasses(ID3D11DeviceContext* ctx, float time, float timeDelta,
                         int frame, const float mouse[4], const float date[4],
                         int viewportW, int viewportH, float clickTime = -10.0f);

    /// 仅渲染 Buffer passes
    void RenderBufferPasses(ID3D11DeviceContext* ctx, float time, float timeDelta,
                            int frame, const float mouse[4], const float date[4],
                            int viewportW, int viewportH, float clickTime = -10.0f);

    /// 仅渲染 Image pass
    void RenderImagePass(ID3D11DeviceContext* ctx, float time, float timeDelta,
                         int frame, const float mouse[4], const float date[4],
                         int viewportW, int viewportH, float clickTime = -10.0f);

    int GetBufferPassCount() const { return static_cast<int>(bufferPasses_.size()); }
    bool IsMultiPass() const { return !bufferPasses_.empty() || hasCubeMapPass_; }
    bool HasCubeMapPass() const { return hasCubeMapPass_; }
    const std::string& GetLastError() const { return lastError_; }
    std::vector<std::string> GetPassNames() const;

    /// 获取默认采样器
    ID3D11SamplerState* GetDefaultSampler() const { return defaultSampler_.Get(); }

    // ---- GPU 渲染计时 ----

    /// 开始 GPU 计时（在渲染 pass 之前调用）
    void BeginGpuTimer();

    /// 结束 GPU 计时（在渲染 pass 之后调用）
    void EndGpuTimer();

    /// 获取上一帧的 GPU 渲染耗时（秒），如果数据未就绪返回 -1
    float GetGpuRenderTime() const;

    // ---- .stoy 专用接口 ----

    /// 设置 .stoy 纹理绑定信息（register 槽位 → SRV 映射）
    struct StoyTextureSRV {
        int registerSlot = -1;
        ID3D11ShaderResourceView* srv = nullptr;
        ID3D11SamplerState* sampler = nullptr;  // nullptr 使用默认采样器
        int width = 0;
        int height = 0;
    };

    /// 设置 .stoy 模式的外部纹理绑定
    void SetStoyExternalTextures(const std::vector<StoyTextureSRV>& textures);

    /// 启用/禁用 .stoy 模式
    void SetStoyMode(bool enabled) { isStoyMode_ = enabled; }
    bool IsStoyMode() const { return isStoyMode_; }

    /// 获取 buffer pass 的输出 SRV（供 .stoy 纹理绑定使用）
    ID3D11ShaderResourceView* GetBufferOutputSRVPrev(int index) const;
    ID3D11ShaderResourceView* GetBufferOutputSRV(int index) const;

    /// 设置 .stoy 的 pass 输出纹理 register 映射
    /// passOutputSlots[i] = buffer pass i 的输出纹理 register 槽位
    /// imagePassSlot = image pass 的输出纹理 register 槽位
    void SetStoyPassOutputSlots(const std::vector<int>& passOutputSlots, int imagePassSlot);

    /// 设置 .stoy TexelSize 数据（外部纹理 + pass 输出纹理的 1/width, 1/height, width, height）
    void SetStoyTexelSizes(const std::vector<float>& texelSizeData);

    /// 为 Image pass 创建双缓冲 FBO（当 Image pass 被其他 pass 引用时调用）
    bool EnableImagePassFBO();

private:
    bool CreateFBO(D3D11RenderPass& pass, int width, int height);
    bool CreateCubeMapFBO(D3D11RenderPass& pass, int cubeSize);
    void FillConstants(ShaderToyConstants& cb, float time, float timeDelta, int frame,
                       const float mouse[4], const float date[4],
                       int viewportW, int viewportH, float clickTime);
    void RenderSinglePass(D3D11RenderPass& pass, float time, float timeDelta,
                          int frame, const float mouse[4], const float date[4],
                          int viewportW, int viewportH, float clickTime);
    void RenderCubeMapPass(D3D11RenderPass& pass, float time, float timeDelta,
                           int frame, const float mouse[4], const float date[4], float clickTime);
    void BindInputTextures(const D3D11RenderPass& pass, float channelRes[4][4]);
    void BindStoyTextures(const D3D11RenderPass& pass, int passIndex);
    void SwapBuffers();
    void DrawFullscreenTriangle();

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;

    std::vector<D3D11RenderPass> bufferPasses_;
    D3D11RenderPass imagePass_;
    D3D11RenderPass cubeMapPass_;
    bool hasCubeMapPass_ = false;
    std::string commonSource_;
    std::string lastError_;
    int width_ = 0;
    int height_ = 0;

    // Image pass 外部目标 RTV（降分辨率渲染时使用）
    ID3D11RenderTargetView* imageTargetRTV_ = nullptr;
    int imageTargetWidth_ = 0;
    int imageTargetHeight_ = 0;

    std::array<D3D11ExternalTextureInfo, 4> externalTextures_;
    ComPtr<ID3D11SamplerState> defaultSampler_;

    // GPU 渲染计时（双缓冲 timestamp query，读取上一帧结果避免 stall）
    struct GpuTimerFrame {
        ComPtr<ID3D11Query> disjoint;
        ComPtr<ID3D11Query> tsBegin;
        ComPtr<ID3D11Query> tsEnd;
        bool recording = false;   // 正在录制（Begin 已调用，End 未调用）
        bool submitted = false;   // 已提交待读取（End 已调用，结果未读取）
    };
    GpuTimerFrame gpuTimerFrames_[2];
    int gpuTimerWriteIdx_ = 0;
    float gpuRenderTime_ = -1.0f;  // 上一帧 GPU 渲染耗时（秒）

    // .stoy 模式
    bool isStoyMode_ = false;
    std::vector<StoyTextureSRV> stoyExternalTextures_;
    std::vector<int> stoyPassOutputSlots_;      // buffer pass i → register 槽位
    int stoyImagePassSlot_ = -1;                // image pass → register 槽位
    bool stoyImagePassNeedsFBO_ = false;        // image pass 被引用时需要双缓冲 FBO
    ComPtr<ID3D11Buffer> stoyTexParamsCB_;      // cbuffer TextureParams : register(b1)
    std::vector<float> stoyTexelSizeData_;      // TexelSize float4 数组（CPU 侧数据）
};

#endif // _WIN32
