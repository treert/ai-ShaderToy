#pragma once

#ifdef _WIN32

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>
#include <string>

using Microsoft::WRL::ComPtr;

/// SwapChain 信息（每个显示器一个）
struct D3D11SwapChainInfo {
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID3D11RenderTargetView> backBufferRTV;
    HWND hwnd = nullptr;
    int width = 0;
    int height = 0;
};

/// D3D11Renderer 管理 D3D11 设备、上下文和多个 SwapChain（壁纸模式多显示器）。
/// 提供全屏三角形渲染的基础设施。
class D3D11Renderer {
public:
    D3D11Renderer();
    ~D3D11Renderer();

    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

    /// 初始化 D3D11 设备（不创建 SwapChain，由 AddSwapChain 创建）
    bool Init();

    /// 为指定 HWND 创建 SwapChain，返回索引（-1=失败）
    int AddSwapChain(HWND hwnd, int width, int height);

    /// 调整指定 SwapChain 的大小
    bool ResizeSwapChain(int index, int width, int height);

    /// 开始渲染帧：设置 RTV + viewport
    void BeginFrame(int swapChainIndex);

    /// 清除 back buffer 为指定颜色
    void ClearBackBuffer(int swapChainIndex, const float color[4]);

    /// 呈现（Present）
    void Present(int swapChainIndex, int syncInterval = 0);

    /// 绘制全屏三角形（需要先绑定 PS 和相关资源）
    void DrawFullscreenTriangle();

    /// 获取设备
    ID3D11Device* GetDevice() const { return device_.Get(); }

    /// 获取设备上下文
    ID3D11DeviceContext* GetContext() const { return context_.Get(); }

    /// 获取指定 SwapChain 的 back buffer RTV
    ID3D11RenderTargetView* GetBackBufferRTV(int index) const;

    /// 获取 SwapChain 数量
    int GetSwapChainCount() const { return static_cast<int>(swapChains_.size()); }

    /// 获取 SwapChain 信息
    const D3D11SwapChainInfo& GetSwapChainInfo(int index) const { return swapChains_[index]; }

    /// 获取全屏三角形的顶点着色器（供其他模块的 PSO 使用）
    ID3D11VertexShader* GetFullscreenVS() const { return fullscreenVS_.Get(); }

    /// 获取初始化错误信息
    const std::string& GetLastError() const { return lastError_; }

private:
    bool CreateBackBufferRTV(D3D11SwapChainInfo& info);
    bool CreateFullscreenTriangleVS();

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIFactory2> dxgiFactory_;

    // 全屏三角形 VS（所有 pass 共享）
    ComPtr<ID3D11VertexShader> fullscreenVS_;
    ComPtr<ID3D10Blob> fullscreenVSBlob_;

    std::vector<D3D11SwapChainInfo> swapChains_;
    std::string lastError_;
};

#endif // _WIN32
