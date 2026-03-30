#define NOMINMAX
#ifdef _WIN32

#include "d3d11_blit_renderer.h"
#include <d3dcompiler.h>
#include <iostream>
#include <algorithm>

static const char* kBlitPS = R"hlsl(
Texture2D<float4> srcTex : register(t0);
SamplerState srcSampler : register(s0);

struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target {
    return srcTex.Sample(srcSampler, input.texcoord);
}
)hlsl";

D3D11BlitRenderer::D3D11BlitRenderer() = default;
D3D11BlitRenderer::~D3D11BlitRenderer() { Cleanup(); }

bool D3D11BlitRenderer::Init(ID3D11Device* device, ID3D11DeviceContext* context) {
    device_ = device;
    context_ = context;

    // 编译 blit PS
    ComPtr<ID3D10Blob> blob, errorBlob;
    HRESULT hr = D3DCompile(kBlitPS, strlen(kBlitPS), "BlitPS", nullptr, nullptr,
                             "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                             &blob, &errorBlob);
    if (FAILED(hr)) {
        std::cerr << "D3D11BlitRenderer: Blit PS compile failed." << std::endl;
        return false;
    }

    hr = device_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                     nullptr, &blitPS_);
    if (FAILED(hr)) return false;

    // 创建采样器（线性插值 + Clamp）
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device_->CreateSamplerState(&sampDesc, &blitSampler_);
    if (FAILED(hr)) return false;

    initialized_ = true;
    return true;
}

bool D3D11BlitRenderer::CreateRenderTarget(int displayWidth, int displayHeight, float renderScale) {
    if (!device_) return false;

    renderWidth_ = std::max(1, static_cast<int>(displayWidth * renderScale));
    renderHeight_ = std::max(1, static_cast<int>(displayHeight * renderScale));

    renderTex_.Reset();
    renderRTV_.Reset();
    renderSRV_.Reset();

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = static_cast<UINT>(renderWidth_);
    texDesc.Height = static_cast<UINT>(renderHeight_);
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device_->CreateTexture2D(&texDesc, nullptr, &renderTex_);
    if (FAILED(hr)) return false;

    hr = device_->CreateRenderTargetView(renderTex_.Get(), nullptr, &renderRTV_);
    if (FAILED(hr)) return false;

    hr = device_->CreateShaderResourceView(renderTex_.Get(), nullptr, &renderSRV_);
    if (FAILED(hr)) return false;

    return true;
}

void D3D11BlitRenderer::BlitToTarget(ID3D11RenderTargetView* targetRTV,
                                      int viewportWidth, int viewportHeight) {
    if (!initialized_ || !renderSRV_ || !targetRTV) return;

    // 设置目标 RTV
    context_->OMSetRenderTargets(1, &targetRTV, nullptr);

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(viewportWidth);
    vp.Height = static_cast<float>(viewportHeight);
    vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);

    // 绑定 blit PS 和源纹理
    context_->PSSetShader(blitPS_.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srv = renderSRV_.Get();
    context_->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* sampler = blitSampler_.Get();
    context_->PSSetSamplers(0, 1, &sampler);

    // 绘制全屏三角形（VS 由外部 D3D11Renderer 设置）
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->IASetInputLayout(nullptr);
    context_->Draw(3, 0);

    // 解绑
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context_->PSSetShaderResources(0, 1, &nullSRV);
}

void D3D11BlitRenderer::Cleanup() {
    blitPS_.Reset();
    blitSampler_.Reset();
    renderTex_.Reset();
    renderRTV_.Reset();
    renderSRV_.Reset();
    initialized_ = false;
}

#endif // _WIN32
