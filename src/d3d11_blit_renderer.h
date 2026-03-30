#pragma once

#ifdef _WIN32

#include <d3d11.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

/// D3D11BlitRenderer 封装降分辨率渲染的 RTV 管理和 blit 逻辑。
/// 与 BlitRenderer（OpenGL 版本）接口对齐。
class D3D11BlitRenderer {
public:
    D3D11BlitRenderer();
    ~D3D11BlitRenderer();

    D3D11BlitRenderer(const D3D11BlitRenderer&) = delete;
    D3D11BlitRenderer& operator=(const D3D11BlitRenderer&) = delete;

    /// 初始化 blit shader（需要 D3D11Renderer 的 VS）
    bool Init(ID3D11Device* device, ID3D11DeviceContext* context);

    bool IsInitialized() const { return initialized_; }

    /// 创建/重建降分辨率 RTV
    bool CreateRenderTarget(int displayWidth, int displayHeight, float renderScale);

    int GetRenderWidth() const { return renderWidth_; }
    int GetRenderHeight() const { return renderHeight_; }

    /// 获取渲染目标 RTV（shader 渲染到此 RTV）
    ID3D11RenderTargetView* GetRenderRTV() const { return renderRTV_.Get(); }

    /// 获取渲染目标 SRV（blit 时采样）
    ID3D11ShaderResourceView* GetRenderSRV() const { return renderSRV_.Get(); }

    /// 将降分辨率纹理 blit 到当前绑定的 RTV
    void BlitToTarget(ID3D11RenderTargetView* targetRTV, int viewportWidth, int viewportHeight);

    void Cleanup();

private:
    bool initialized_ = false;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;

    // Blit shader
    ComPtr<ID3D11PixelShader> blitPS_;
    ComPtr<ID3D11SamplerState> blitSampler_;

    // 降分辨率纹理
    ComPtr<ID3D11Texture2D> renderTex_;
    ComPtr<ID3D11RenderTargetView> renderRTV_;
    ComPtr<ID3D11ShaderResourceView> renderSRV_;
    int renderWidth_ = 0;
    int renderHeight_ = 0;
};

#endif // _WIN32
