#ifdef _WIN32

#include "d3d11_renderer.h"
#include <d3dcompiler.h>
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// 全屏三角形顶点着色器 — 无输入，由 SV_VertexID 生成覆盖整个视口的三角形
static const char* kFullscreenTriangleVS = R"hlsl(
struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(uint vertexId : SV_VertexID) {
    VSOutput output;
    // 生成覆盖 [-1,1] NDC 的大三角形
    // vertexId: 0 -> (-1,-1), 1 -> (3,-1), 2 -> (-1,3)
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    // 翻转 Y：修正三角形绕序（防止被 D3D11 默认背面剔除）
    // 同时使 SV_Position.y 在底部=小值、顶部=大值（模拟 OpenGL gl_FragCoord.y）
    output.position.y = -output.position.y;
    output.texcoord = uv;
    return output;
}
)hlsl";

D3D11Renderer::D3D11Renderer() = default;

D3D11Renderer::~D3D11Renderer() {
    // ComPtr 自动释放
    swapChains_.clear();
}

bool D3D11Renderer::Init() {
    // 创建 DXGI Factory
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory_));
    if (FAILED(hr)) {
        lastError_ = "CreateDXGIFactory1 failed.";
        return false;
    }

    // Check Allow Tearing support (bypass DWM VSync throttling for multi-SwapChain scenarios)
    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(dxgiFactory_.As(&factory5))) {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing)))) {
            tearingSupported_ = (allowTearing == TRUE);
        }
    }
    std::cout << "DXGI Allow Tearing: " << (tearingSupported_ ? "supported" : "not supported") << std::endl;

    // 创建 D3D11 设备
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    UINT createFlags = 0;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(
        nullptr,                    // 默认适配器
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,                    // 不使用软件渲染
        createFlags,
        featureLevels,
        _countof(featureLevels),
        D3D11_SDK_VERSION,
        &device_,
        &featureLevel,
        &context_
    );

    if (FAILED(hr)) {
        lastError_ = "D3D11CreateDevice failed.";
        return false;
    }

    std::cout << "D3D11 device created (Feature Level "
              << ((featureLevel >> 12) & 0xF) << "."
              << ((featureLevel >> 8) & 0xF) << ")" << std::endl;

    // Set maximum frame latency to reduce DXGI frame queue blocking
    // Default is 3 frames; setting to 1 prevents DWM from throttling background SwapChains
    ComPtr<IDXGIDevice1> dxgiDevice;
    if (SUCCEEDED(device_.As(&dxgiDevice))) {
        dxgiDevice->SetMaximumFrameLatency(1);
        std::cout << "DXGI MaxFrameLatency set to 1" << std::endl;
    }

    // 创建全屏三角形 VS
    if (!CreateFullscreenTriangleVS()) {
        return false;
    }

    return true;
}

bool D3D11Renderer::CreateFullscreenTriangleVS() {
    ComPtr<ID3D10Blob> errorBlob;
    HRESULT hr = D3DCompile(
        kFullscreenTriangleVS,
        strlen(kFullscreenTriangleVS),
        "FullscreenTriangleVS",
        nullptr,    // defines
        nullptr,    // includes
        "main",
        "vs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &fullscreenVSBlob_,
        &errorBlob
    );

    if (FAILED(hr)) {
        std::string errorMsg = "Fullscreen triangle VS compile failed";
        if (errorBlob) {
            errorMsg += ": ";
            errorMsg += static_cast<const char*>(errorBlob->GetBufferPointer());
        }
        lastError_ = errorMsg;
        std::cerr << lastError_ << std::endl;
        return false;
    }

    hr = device_->CreateVertexShader(
        fullscreenVSBlob_->GetBufferPointer(),
        fullscreenVSBlob_->GetBufferSize(),
        nullptr,
        &fullscreenVS_
    );

    if (FAILED(hr)) {
        lastError_ = "CreateVertexShader failed for fullscreen triangle.";
        return false;
    }

    return true;
}

int D3D11Renderer::AddSwapChain(HWND hwnd, int width, int height) {
    if (!device_ || !dxgiFactory_) return -1;

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    // Allow Tearing: bypass DWM composition throttling when multiple SwapChains exist
    desc.Flags = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    D3D11SwapChainInfo info;
    info.hwnd = hwnd;
    info.width = width;
    info.height = height;

    HRESULT hr = dxgiFactory_->CreateSwapChainForHwnd(
        device_.Get(),
        hwnd,
        &desc,
        nullptr,    // fullscreen desc
        nullptr,    // restrict output
        &info.swapChain
    );

    if (FAILED(hr)) {
        lastError_ = "CreateSwapChainForHwnd failed.";
        std::cerr << lastError_ << std::endl;
        return -1;
    }

    // 禁用 Alt+Enter 全屏切换
    dxgiFactory_->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    if (!CreateBackBufferRTV(info)) {
        return -1;
    }

    int index = static_cast<int>(swapChains_.size());
    swapChains_.push_back(std::move(info));

    std::cout << "D3D11 SwapChain created for HWND " << hwnd
              << " (" << width << "x" << height << ")" << std::endl;
    return index;
}

bool D3D11Renderer::CreateBackBufferRTV(D3D11SwapChainInfo& info) {
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = info.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) {
        lastError_ = "GetBuffer(0) failed.";
        return false;
    }

    hr = device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &info.backBufferRTV);
    if (FAILED(hr)) {
        lastError_ = "CreateRenderTargetView for back buffer failed.";
        return false;
    }
    return true;
}

bool D3D11Renderer::ResizeSwapChain(int index, int width, int height) {
    if (index < 0 || index >= static_cast<int>(swapChains_.size())) return false;

    auto& info = swapChains_[index];
    info.backBufferRTV.Reset();

    // Flags must match SwapChain creation flags (ALLOW_TEARING must be preserved)
    UINT resizeFlags = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    HRESULT hr = info.swapChain->ResizeBuffers(
        0, // 保持 buffer 数量
        static_cast<UINT>(width),
        static_cast<UINT>(height),
        DXGI_FORMAT_UNKNOWN, // 保持格式
        resizeFlags
    );
    if (FAILED(hr)) {
        lastError_ = "ResizeBuffers failed.";
        return false;
    }

    info.width = width;
    info.height = height;
    return CreateBackBufferRTV(info);
}

void D3D11Renderer::BeginFrame(int swapChainIndex) {
    if (swapChainIndex < 0 || swapChainIndex >= static_cast<int>(swapChains_.size())) return;

    auto& info = swapChains_[swapChainIndex];

    // 设置 RTV
    ID3D11RenderTargetView* rtv = info.backBufferRTV.Get();
    context_->OMSetRenderTargets(1, &rtv, nullptr);

    // 设置 viewport
    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(info.width);
    vp.Height = static_cast<float>(info.height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);
}

void D3D11Renderer::ClearBackBuffer(int swapChainIndex, const float color[4]) {
    if (swapChainIndex < 0 || swapChainIndex >= static_cast<int>(swapChains_.size())) return;
    context_->ClearRenderTargetView(swapChains_[swapChainIndex].backBufferRTV.Get(), color);
}

void D3D11Renderer::Present(int swapChainIndex, int syncInterval) {
    if (swapChainIndex < 0 || swapChainIndex >= static_cast<int>(swapChains_.size())) return;
    // syncInterval=0 + tearing supported: use DXGI_PRESENT_ALLOW_TEARING to bypass DWM throttling
    UINT presentFlags = (tearingSupported_ && syncInterval == 0) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    swapChains_[swapChainIndex].swapChain->Present(static_cast<UINT>(syncInterval), presentFlags);
}

void D3D11Renderer::DrawFullscreenTriangle() {
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->IASetInputLayout(nullptr); // 无输入布局，VS 从 SV_VertexID 生成
    context_->VSSetShader(fullscreenVS_.Get(), nullptr, 0);
    context_->Draw(3, 0);
}

ID3D11RenderTargetView* D3D11Renderer::GetBackBufferRTV(int index) const {
    if (index < 0 || index >= static_cast<int>(swapChains_.size())) return nullptr;
    return swapChains_[index].backBufferRTV.Get();
}

#endif // _WIN32
