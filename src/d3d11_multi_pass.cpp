#ifdef _WIN32

#include "d3d11_multi_pass.h"
#include <iostream>
#include <algorithm>
#include <cstring>

D3D11MultiPass::D3D11MultiPass() = default;
D3D11MultiPass::~D3D11MultiPass() { Clear(); }

void D3D11MultiPass::SetDevice(ID3D11Device* device, ID3D11DeviceContext* context) {
    device_ = device;
    context_ = context;

    // 创建默认采样器
    D3D11_SAMPLER_DESC desc = {};
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.MaxLOD = D3D11_FLOAT32_MAX;
    device_->CreateSamplerState(&desc, &defaultSampler_);
}

bool D3D11MultiPass::Init(int width, int height) {
    width_ = width;
    height_ = height;
    return true;
}

void D3D11MultiPass::Clear() {
    bufferPasses_.clear();
    imagePass_ = D3D11RenderPass{};
    cubeMapPass_ = D3D11RenderPass{};
    hasCubeMapPass_ = false;
    commonSource_.clear();
    lastError_.clear();
    externalTextures_ = {};
    imageTargetRTV_ = nullptr;
}

void D3D11MultiPass::Resize(int width, int height) {
    width_ = width;
    height_ = height;
    for (auto& pass : bufferPasses_) {
        CreateFBO(pass, width, height);
    }
}

void D3D11MultiPass::SetCommonSource(const std::string& common) {
    commonSource_ = common;
}

void D3D11MultiPass::SetExternalTexture(int channel, ID3D11ShaderResourceView* srv,
                                         int width, int height, ChannelType type) {
    if (channel >= 0 && channel < 4) {
        externalTextures_[channel] = {srv, width, height, type};
    }
}

bool D3D11MultiPass::CreateFBO(D3D11RenderPass& pass, int width, int height) {
    if (!device_) return false;
    pass.width = width;
    pass.height = height;

    // 创建双缓冲纹理
    for (int i = 0; i < 2; ++i) {
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = static_cast<UINT>(width);
        texDesc.Height = static_cast<UINT>(height);
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        ComPtr<ID3D11Texture2D> tex;
        HRESULT hr = device_->CreateTexture2D(&texDesc, nullptr, &tex);
        if (FAILED(hr)) {
            lastError_ = "CreateTexture2D failed for " + pass.name;
            return false;
        }

        ComPtr<ID3D11ShaderResourceView> srv;
        hr = device_->CreateShaderResourceView(tex.Get(), nullptr, &srv);
        if (FAILED(hr)) {
            lastError_ = "CreateSRV failed for " + pass.name;
            return false;
        }

        if (i == 0) {
            pass.outputTexture = std::move(tex);
            pass.outputSRV = std::move(srv);

            // 创建 RTV
            hr = device_->CreateRenderTargetView(pass.outputTexture.Get(), nullptr, &pass.outputRTV);
            if (FAILED(hr)) {
                lastError_ = "CreateRTV failed for " + pass.name;
                return false;
            }
        } else {
            pass.outputTexturePrev = std::move(tex);
            pass.outputSRVPrev = std::move(srv);
        }
    }

    // 清除初始内容
    float clearColor[4] = {0, 0, 0, 0};
    context_->ClearRenderTargetView(pass.outputRTV.Get(), clearColor);

    return true;
}

bool D3D11MultiPass::CreateCubeMapFBO(D3D11RenderPass& pass, int cubeSize) {
    if (!device_) return false;
    pass.width = cubeSize;
    pass.height = cubeSize;

    // 创建双缓冲 cubemap 纹理
    for (int t = 0; t < 2; ++t) {
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = static_cast<UINT>(cubeSize);
        texDesc.Height = static_cast<UINT>(cubeSize);
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 6;
        texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        texDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

        ComPtr<ID3D11Texture2D> tex;
        HRESULT hr = device_->CreateTexture2D(&texDesc, nullptr, &tex);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = texDesc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = 1;

        ComPtr<ID3D11ShaderResourceView> srv;
        hr = device_->CreateShaderResourceView(tex.Get(), &srvDesc, &srv);
        if (FAILED(hr)) return false;

        if (t == 0) {
            pass.cubeMapTexture = std::move(tex);
            pass.cubeMapSRV = std::move(srv);

            // 为每个面创建 RTV
            for (int face = 0; face < 6; ++face) {
                D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
                rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                rtvDesc.Texture2DArray.FirstArraySlice = static_cast<UINT>(face);
                rtvDesc.Texture2DArray.ArraySize = 1;

                hr = device_->CreateRenderTargetView(pass.cubeMapTexture.Get(), &rtvDesc, &pass.cubeFaceRTV[face]);
                if (FAILED(hr)) return false;

                float clearColor[4] = {0, 0, 0, 0};
                context_->ClearRenderTargetView(pass.cubeFaceRTV[face].Get(), clearColor);
            }
        } else {
            pass.cubeMapTexturePrev = std::move(tex);
            pass.cubeMapSRVPrev = std::move(srv);
        }
    }

    return true;
}

int D3D11MultiPass::AddBufferPass(const std::string& name, const std::string& source,
                                   const std::array<int, 4>& inputs,
                                   const std::array<ChannelType, 4>& channelTypes) {
    D3D11RenderPass pass;
    pass.name = name;
    pass.shaderSource = source;
    pass.inputChannels = inputs;
    pass.channelTypes = channelTypes;

    pass.shader.SetDevice(device_, context_);
    pass.shader.SetChannelTypes(channelTypes);
    pass.shader.SetCommonSource(commonSource_);
    if (!pass.shader.LoadFromSource(source)) {
        lastError_ = "Failed to compile " + name + ": " + pass.shader.GetLastError();
        return -1;
    }

    if (!CreateFBO(pass, width_, height_)) return -1;

    int index = static_cast<int>(bufferPasses_.size());
    bufferPasses_.push_back(std::move(pass));
    std::cout << "D3D11 Buffer pass added: " << name << " (index " << index << ")" << std::endl;
    return index;
}

bool D3D11MultiPass::SetImagePass(const std::string& source, const std::array<int, 4>& inputs,
                                   const std::array<ChannelType, 4>& channelTypes) {
    imagePass_ = D3D11RenderPass{};
    imagePass_.name = "Image";
    imagePass_.shaderSource = source;
    imagePass_.inputChannels = inputs;
    imagePass_.channelTypes = channelTypes;

    imagePass_.shader.SetDevice(device_, context_);
    imagePass_.shader.SetChannelTypes(channelTypes);
    imagePass_.shader.SetCommonSource(commonSource_);
    if (!imagePass_.shader.LoadFromSource(source)) {
        lastError_ = "Failed to compile Image pass: " + imagePass_.shader.GetLastError();
        return false;
    }

    return true;
}

bool D3D11MultiPass::SetCubeMapPass(const std::string& source, const std::array<int, 4>& inputs,
                                     const std::array<ChannelType, 4>& channelTypes, int cubeSize) {
    cubeMapPass_ = D3D11RenderPass{};
    cubeMapPass_.name = "Cube A";
    cubeMapPass_.shaderSource = source;
    cubeMapPass_.inputChannels = inputs;
    cubeMapPass_.channelTypes = channelTypes;
    cubeMapPass_.isCubeMap = true;

    cubeMapPass_.shader.SetDevice(device_, context_);
    cubeMapPass_.shader.SetChannelTypes(channelTypes);
    cubeMapPass_.shader.SetCommonSource(commonSource_);
    cubeMapPass_.shader.SetCubeMapPassMode(true);
    if (!cubeMapPass_.shader.LoadFromSource(source)) {
        lastError_ = "Failed to compile Cube A: " + cubeMapPass_.shader.GetLastError();
        return false;
    }

    if (!CreateCubeMapFBO(cubeMapPass_, cubeSize)) {
        lastError_ = "Failed to create CubeMap FBO";
        return false;
    }

    hasCubeMapPass_ = true;
    return true;
}

void D3D11MultiPass::SetImageTargetRTV(ID3D11RenderTargetView* rtv, int width, int height) {
    imageTargetRTV_ = rtv;
    imageTargetWidth_ = width;
    imageTargetHeight_ = height;
}

void D3D11MultiPass::FillConstants(ShaderToyConstants& cb, float time, float timeDelta,
                                    int frame, const float mouse[4], const float date[4],
                                    int viewportW, int viewportH, float clickTime) {
    memset(&cb, 0, sizeof(cb));
    cb.iResolution[0] = static_cast<float>(viewportW);
    cb.iResolution[1] = static_cast<float>(viewportH);
    cb.iResolution[2] = 1.0f;
    cb.iTime = time;
    cb.iTimeDelta = timeDelta;
    cb.iFrame = frame;
    cb.iFrameRate = (timeDelta > 0.0f) ? (1.0f / timeDelta) : 60.0f;
    cb.iSampleRate = 44100.0f;
    cb.iClickTime = clickTime;
    memcpy(cb.iMouse, mouse, sizeof(float) * 4);
    memcpy(cb.iDate, date, sizeof(float) * 4);
    for (int i = 0; i < 4; ++i) cb.iChannelTime[i] = time;
}

void D3D11MultiPass::BindInputTextures(const D3D11RenderPass& pass, float channelRes[4][4]) {
    ID3D11SamplerState* sampler = defaultSampler_.Get();

    for (int i = 0; i < 4; ++i) {
        int input = pass.inputChannels[i];
        ID3D11ShaderResourceView* srv = nullptr;
        channelRes[i][0] = channelRes[i][1] = 0.0f;
        channelRes[i][2] = 1.0f;
        channelRes[i][3] = 0.0f;

        if (input >= 0 && input < static_cast<int>(bufferPasses_.size())) {
            srv = bufferPasses_[input].outputSRVPrev.Get();
            channelRes[i][0] = static_cast<float>(bufferPasses_[input].width);
            channelRes[i][1] = static_cast<float>(bufferPasses_[input].height);
        } else if (input == 200 && hasCubeMapPass_) {
            srv = pass.isCubeMap ? cubeMapPass_.cubeMapSRVPrev.Get() : cubeMapPass_.cubeMapSRVPrev.Get();
            channelRes[i][0] = static_cast<float>(cubeMapPass_.width);
            channelRes[i][1] = static_cast<float>(cubeMapPass_.height);
        } else if (input >= 100 && input <= 103) {
            int extIdx = input - 100;
            const auto& ext = externalTextures_[extIdx];
            srv = ext.srv;
            channelRes[i][0] = static_cast<float>(ext.width);
            channelRes[i][1] = static_cast<float>(ext.height);
        }

        context_->PSSetShaderResources(static_cast<UINT>(i), 1, &srv);
        context_->PSSetSamplers(static_cast<UINT>(i), 1, &sampler);
    }
}

void D3D11MultiPass::DrawFullscreenTriangle() {
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->IASetInputLayout(nullptr);
    // VS 由调用方设置（D3D11Renderer::GetFullscreenVS()）
    context_->Draw(3, 0);
}

void D3D11MultiPass::RenderSinglePass(D3D11RenderPass& pass, float time, float timeDelta,
                                        int frame, const float mouse[4], const float date[4],
                                        int viewportW, int viewportH, float clickTime) {
    // 确定目标 RTV 和 viewport
    ID3D11RenderTargetView* rtv = nullptr;
    int targetW = viewportW, targetH = viewportH;

    if (pass.outputRTV) {
        // Buffer pass
        rtv = pass.outputRTV.Get();
        targetW = pass.width;
        targetH = pass.height;
    } else if (imageTargetRTV_) {
        // Image pass 渲染到外部 RTV（降分辨率）
        rtv = imageTargetRTV_;
        if (imageTargetWidth_ > 0) targetW = imageTargetWidth_;
        if (imageTargetHeight_ > 0) targetH = imageTargetHeight_;
    }
    // 如果 rtv == nullptr，调用方应已经设置好 back buffer RTV

    if (rtv) {
        context_->OMSetRenderTargets(1, &rtv, nullptr);
    }

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(targetW);
    vp.Height = static_cast<float>(targetH);
    vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);

    float clearColor[4] = {0, 0, 0, 0};
    if (rtv) {
        context_->ClearRenderTargetView(rtv, clearColor);
    }

    // 设置 constants
    ShaderToyConstants cb;
    FillConstants(cb, time, timeDelta, frame, mouse, date, targetW, targetH, clickTime);

    // 绑定输入纹理并获取 channelResolution
    float channelRes[4][4] = {};
    BindInputTextures(pass, channelRes);
    memcpy(cb.iChannelResolution, channelRes, sizeof(channelRes));

    pass.shader.UpdateConstants(cb);
    pass.shader.Use();

    DrawFullscreenTriangle();

    // 解绑 SRV（防止 SRV/RTV 冲突）
    ID3D11ShaderResourceView* nullSRVs[4] = {};
    context_->PSSetShaderResources(0, 4, nullSRVs);
}

void D3D11MultiPass::RenderCubeMapPass(D3D11RenderPass& pass, float time, float timeDelta,
                                         int frame, const float mouse[4], const float date[4],
                                         float clickTime) {
    static const struct { float right[3]; float up[3]; float dir[3]; } faces[6] = {
        {{ 0, 0,-1}, { 0,-1, 0}, { 1, 0, 0}},
        {{ 0, 0, 1}, { 0,-1, 0}, {-1, 0, 0}},
        {{ 1, 0, 0}, { 0, 0, 1}, { 0, 1, 0}},
        {{ 1, 0, 0}, { 0, 0,-1}, { 0,-1, 0}},
        {{ 1, 0, 0}, { 0,-1, 0}, { 0, 0, 1}},
        {{-1, 0, 0}, { 0,-1, 0}, { 0, 0,-1}},
    };

    float channelRes[4][4] = {};
    BindInputTextures(pass, channelRes);

    for (int face = 0; face < 6; ++face) {
        ID3D11RenderTargetView* rtv = pass.cubeFaceRTV[face].Get();
        context_->OMSetRenderTargets(1, &rtv, nullptr);

        D3D11_VIEWPORT vp = {};
        vp.Width = static_cast<float>(pass.width);
        vp.Height = static_cast<float>(pass.height);
        vp.MaxDepth = 1.0f;
        context_->RSSetViewports(1, &vp);

        float clearColor[4] = {0, 0, 0, 0};
        context_->ClearRenderTargetView(rtv, clearColor);

        ShaderToyConstants cb;
        FillConstants(cb, time, timeDelta, frame, mouse, date, pass.width, pass.height, clickTime);
        memcpy(cb.iChannelResolution, channelRes, sizeof(channelRes));
        memcpy(cb.cubeFaceRight, faces[face].right, sizeof(float) * 3);
        memcpy(cb.cubeFaceUp, faces[face].up, sizeof(float) * 3);
        memcpy(cb.cubeFaceDir, faces[face].dir, sizeof(float) * 3);

        pass.shader.UpdateConstants(cb);
        pass.shader.Use();
        DrawFullscreenTriangle();
    }

    ID3D11ShaderResourceView* nullSRVs[4] = {};
    context_->PSSetShaderResources(0, 4, nullSRVs);
}

void D3D11MultiPass::RenderAllPasses(ID3D11DeviceContext* ctx, float time, float timeDelta,
                                      int frame, const float mouse[4], const float date[4],
                                      int viewportW, int viewportH, float clickTime) {
    RenderBufferPasses(ctx, time, timeDelta, frame, mouse, date, viewportW, viewportH, clickTime);
    RenderImagePass(ctx, time, timeDelta, frame, mouse, date, viewportW, viewportH, clickTime);
}

void D3D11MultiPass::RenderBufferPasses(ID3D11DeviceContext* ctx, float time, float timeDelta,
                                         int frame, const float mouse[4], const float date[4],
                                         int viewportW, int viewportH, float clickTime) {
    if (hasCubeMapPass_) {
        RenderCubeMapPass(cubeMapPass_, time, timeDelta, frame, mouse, date, clickTime);
    }
    for (auto& pass : bufferPasses_) {
        RenderSinglePass(pass, time, timeDelta, frame, mouse, date, viewportW, viewportH, clickTime);
    }
    SwapBuffers();
}

void D3D11MultiPass::RenderImagePass(ID3D11DeviceContext* ctx, float time, float timeDelta,
                                      int frame, const float mouse[4], const float date[4],
                                      int viewportW, int viewportH, float clickTime) {
    RenderSinglePass(imagePass_, time, timeDelta, frame, mouse, date, viewportW, viewportH, clickTime);
}

void D3D11MultiPass::SwapBuffers() {
    for (auto& pass : bufferPasses_) {
        std::swap(pass.outputTexture, pass.outputTexturePrev);
        std::swap(pass.outputSRV, pass.outputSRVPrev);
        // 重建 RTV 指向新的 outputTexture
        pass.outputRTV.Reset();
        device_->CreateRenderTargetView(pass.outputTexture.Get(), nullptr, &pass.outputRTV);
    }
    if (hasCubeMapPass_) {
        std::swap(cubeMapPass_.cubeMapTexture, cubeMapPass_.cubeMapTexturePrev);
        std::swap(cubeMapPass_.cubeMapSRV, cubeMapPass_.cubeMapSRVPrev);
        for (int face = 0; face < 6; ++face) {
            cubeMapPass_.cubeFaceRTV[face].Reset();
            D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            rtvDesc.Texture2DArray.FirstArraySlice = static_cast<UINT>(face);
            rtvDesc.Texture2DArray.ArraySize = 1;
            device_->CreateRenderTargetView(cubeMapPass_.cubeMapTexture.Get(), &rtvDesc, &cubeMapPass_.cubeFaceRTV[face]);
        }
    }
}

std::vector<std::string> D3D11MultiPass::GetPassNames() const {
    std::vector<std::string> names;
    if (hasCubeMapPass_) names.push_back("Cube A");
    for (const auto& p : bufferPasses_) names.push_back(p.name);
    names.push_back(imagePass_.name.empty() ? "Image" : imagePass_.name);
    return names;
}

#endif // _WIN32
