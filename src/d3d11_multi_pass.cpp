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

    // 创建 GPU timestamp query（双缓冲）
    for (int i = 0; i < 2; i++) {
        D3D11_QUERY_DESC qd = {};
        qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        device_->CreateQuery(&qd, &gpuTimerFrames_[i].disjoint);
        qd.Query = D3D11_QUERY_TIMESTAMP;
        device_->CreateQuery(&qd, &gpuTimerFrames_[i].tsBegin);
        device_->CreateQuery(&qd, &gpuTimerFrames_[i].tsEnd);
    }
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
    stoyImagePassNeedsFBO_ = false;
    stoyTexParamsCB_.Reset();
    stoyTexelSizeData_.clear();
}

void D3D11MultiPass::Resize(int width, int height) {
    width_ = width;
    height_ = height;
    for (auto& pass : bufferPasses_) {
        CreateFBO(pass, width, height);
    }
    if (stoyImagePassNeedsFBO_) {
        CreateFBO(imagePass_, width, height);
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
                                   const std::array<ChannelType, 4>& channelTypes,
                                   bool isHlsl,
                                   const std::string& shaderDir,
                                   const std::string& assetsDir) {
    D3D11RenderPass pass;
    pass.name = name;
    pass.shaderSource = source;
    pass.inputChannels = inputs;
    pass.channelTypes = channelTypes;

    pass.shader.SetDevice(device_, context_);
    pass.shader.SetChannelTypes(channelTypes);
    pass.shader.SetCommonSource(commonSource_);
    pass.shader.SetFlipFragCoordY(false);  // Buffer pass 不翻转 Y（保持 D3D11 原生 top-down）

    bool compileOk = isHlsl
        ? pass.shader.LoadFromHlsl(source, shaderDir, assetsDir)
        : pass.shader.LoadFromSource(source);
    if (!compileOk) {
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
                                   const std::array<ChannelType, 4>& channelTypes,
                                   bool isHlsl,
                                   const std::string& shaderDir,
                                   const std::string& assetsDir) {
    imagePass_ = D3D11RenderPass{};
    imagePass_.name = "Image";
    imagePass_.shaderSource = source;
    imagePass_.inputChannels = inputs;
    imagePass_.channelTypes = channelTypes;

    imagePass_.shader.SetDevice(device_, context_);
    imagePass_.shader.SetChannelTypes(channelTypes);
    imagePass_.shader.SetCommonSource(commonSource_);

    bool compileOk = isHlsl
        ? imagePass_.shader.LoadFromHlsl(source, shaderDir, assetsDir)
        : imagePass_.shader.LoadFromSource(source);
    if (!compileOk) {
        lastError_ = "Failed to compile Image pass: " + imagePass_.shader.GetLastError();
        return false;
    }

    return true;
}

bool D3D11MultiPass::SetCubeMapPass(const std::string& source, const std::array<int, 4>& inputs,
                                     const std::array<ChannelType, 4>& channelTypes, int cubeSize,
                                     bool isHlsl,
                                     const std::string& shaderDir,
                                     const std::string& assetsDir) {
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
    cubeMapPass_.shader.SetFlipFragCoordY(false);  // CubeMap pass 不翻转 Y

    bool compileOk = isHlsl
        ? cubeMapPass_.shader.LoadFromHlsl(source, shaderDir, assetsDir)
        : cubeMapPass_.shader.LoadFromSource(source);
    if (!compileOk) {
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

// ============================================================
// .stoy 纹理绑定
// ============================================================

void D3D11MultiPass::SetStoyExternalTextures(const std::vector<StoyTextureSRV>& textures) {
    stoyExternalTextures_ = textures;
}

void D3D11MultiPass::SetStoyPassOutputSlots(const std::vector<int>& passOutputSlots, int imagePassSlot) {
    stoyPassOutputSlots_ = passOutputSlots;
    stoyImagePassSlot_ = imagePassSlot;
}

bool D3D11MultiPass::EnableImagePassFBO() {
    stoyImagePassNeedsFBO_ = true;
    return CreateFBO(imagePass_, width_, height_);
}

void D3D11MultiPass::SetStoyTexelSizes(const std::vector<float>& texelSizeData) {
    stoyTexelSizeData_ = texelSizeData;
    if (texelSizeData.empty() || !device_) return;

    // 创建或重建 cbuffer（大小可能变化）
    UINT byteWidth = static_cast<UINT>(texelSizeData.size() * sizeof(float));
    // 对齐到 16 字节
    byteWidth = (byteWidth + 15) & ~15;

    bool needRecreate = !stoyTexParamsCB_;
    if (stoyTexParamsCB_) {
        D3D11_BUFFER_DESC existingDesc;
        stoyTexParamsCB_->GetDesc(&existingDesc);
        if (existingDesc.ByteWidth != byteWidth) needRecreate = true;
    }
    if (needRecreate) {
        stoyTexParamsCB_.Reset();
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = byteWidth;
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device_->CreateBuffer(&cbDesc, nullptr, &stoyTexParamsCB_);
    }
}

ID3D11ShaderResourceView* D3D11MultiPass::GetBufferOutputSRVPrev(int index) const {
    if (index < 0 || index >= static_cast<int>(bufferPasses_.size())) return nullptr;
    return bufferPasses_[index].outputSRVPrev.Get();
}

ID3D11ShaderResourceView* D3D11MultiPass::GetBufferOutputSRV(int index) const {
    if (index < 0 || index >= static_cast<int>(bufferPasses_.size())) return nullptr;
    return bufferPasses_[index].outputSRV.Get();
}

void D3D11MultiPass::BindStoyTextures(const D3D11RenderPass& pass, int passIndex) {
    ID3D11SamplerState* defSampler = defaultSampler_.Get();

    // 绑定外部纹理（按 register 槽位）
    for (const auto& tex : stoyExternalTextures_) {
        if (tex.registerSlot >= 0) {
            ID3D11ShaderResourceView* srv = tex.srv;
            ID3D11SamplerState* sampler = tex.sampler ? tex.sampler : defSampler;
            context_->PSSetShaderResources(static_cast<UINT>(tex.registerSlot), 1, &srv);
            context_->PSSetSamplers(static_cast<UINT>(tex.registerSlot), 1, &sampler);
        }
    }

    // 绑定 pass 输出纹理
    // 引用语义：已执行的 pass → 当前帧(outputSRV)，未执行 / 自引用 → 上一帧(outputSRVPrev)
    int numBufferPasses = static_cast<int>(bufferPasses_.size());
    for (int i = 0; i < numBufferPasses; i++) {
        if (i < static_cast<int>(stoyPassOutputSlots_.size())) {
            int slot = stoyPassOutputSlots_[i];
            ID3D11ShaderResourceView* srv = nullptr;
            if (i < passIndex) {
                // 已执行的 buffer pass → 当前帧
                srv = bufferPasses_[i].outputSRV.Get();
            } else {
                // 未执行或自引用 → 上一帧
                srv = bufferPasses_[i].outputSRVPrev.Get();
            }
            context_->PSSetShaderResources(static_cast<UINT>(slot), 1, &srv);
            context_->PSSetSamplers(static_cast<UINT>(slot), 1, &defSampler);
        }
    }

    // Image pass 的输出纹理（如果被引用，总是上一帧）
    if (stoyImagePassSlot_ >= 0 && imagePass_.outputSRVPrev) {
        ID3D11ShaderResourceView* srv = imagePass_.outputSRVPrev.Get();
        context_->PSSetShaderResources(static_cast<UINT>(stoyImagePassSlot_), 1, &srv);
        context_->PSSetSamplers(static_cast<UINT>(stoyImagePassSlot_), 1, &defSampler);
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
    if (isStoyMode_) {
        // .stoy 模式：按 register 槽位绑定所有纹理
        // 确定当前 pass 的 index
        int passIdx = -1;  // -1 = image pass
        for (int i = 0; i < static_cast<int>(bufferPasses_.size()); i++) {
            if (&bufferPasses_[i] == &pass) { passIdx = i; break; }
        }
        BindStoyTextures(pass, passIdx);
    } else {
        BindInputTextures(pass, channelRes);
    }
    memcpy(cb.iChannelResolution, channelRes, sizeof(channelRes));

    pass.shader.UpdateConstants(cb);
    pass.shader.Use();

    // .stoy 模式：更新并绑定 TextureParams cbuffer (b1)
    if (isStoyMode_ && stoyTexParamsCB_ && !stoyTexelSizeData_.empty()) {
        // 更新 pass 输出纹理的 TexelSize（尺寸可能随窗口变化）
        // 外部纹理的 TexelSize 在 SetStoyTexelSizes 时已设置，这里只需更新 pass 输出的
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = context_->Map(stoyTexParamsCB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            memcpy(mapped.pData, stoyTexelSizeData_.data(),
                   stoyTexelSizeData_.size() * sizeof(float));
            context_->Unmap(stoyTexParamsCB_.Get(), 0);
        }
        ID3D11Buffer* cb1 = stoyTexParamsCB_.Get();
        context_->PSSetConstantBuffers(1, 1, &cb1);
    }

    DrawFullscreenTriangle();

    // 解绑 SRV（防止 SRV/RTV 冲突）
    if (isStoyMode_) {
        // .stoy 模式下纹理可能绑定在高于 4 的槽位
        int maxSlot = 4;
        for (const auto& tex : stoyExternalTextures_) {
            if (tex.registerSlot >= maxSlot) maxSlot = tex.registerSlot + 1;
        }
        for (int s : stoyPassOutputSlots_) {
            if (s >= maxSlot) maxSlot = s + 1;
        }
        if (stoyImagePassSlot_ >= maxSlot) maxSlot = stoyImagePassSlot_ + 1;
        std::vector<ID3D11ShaderResourceView*> nullSRVs(maxSlot, nullptr);
        context_->PSSetShaderResources(0, static_cast<UINT>(maxSlot), nullSRVs.data());
    } else {
        ID3D11ShaderResourceView* nullSRVs[4] = {};
        context_->PSSetShaderResources(0, 4, nullSRVs);
    }
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
    // .stoy 模式：Image pass 也需要双缓冲交换
    if (stoyImagePassNeedsFBO_ && imagePass_.outputTexture) {
        std::swap(imagePass_.outputTexture, imagePass_.outputTexturePrev);
        std::swap(imagePass_.outputSRV, imagePass_.outputSRVPrev);
        imagePass_.outputRTV.Reset();
        device_->CreateRenderTargetView(imagePass_.outputTexture.Get(), nullptr, &imagePass_.outputRTV);
    }
}

std::vector<std::string> D3D11MultiPass::GetPassNames() const {
    std::vector<std::string> names;
    if (hasCubeMapPass_) names.push_back("Cube A");
    for (const auto& p : bufferPasses_) names.push_back(p.name);
    names.push_back(imagePass_.name.empty() ? "Image" : imagePass_.name);
    return names;
}

// ============================================================
// GPU 渲染计时
// ============================================================

void D3D11MultiPass::BeginGpuTimer() {
    if (!context_) return;
    auto& frame = gpuTimerFrames_[gpuTimerWriteIdx_];
    if (!frame.disjoint || !frame.tsBegin) return;
    context_->Begin(frame.disjoint.Get());
    context_->End(frame.tsBegin.Get());
    frame.active = true;
}

void D3D11MultiPass::EndGpuTimer() {
    if (!context_) return;
    auto& frame = gpuTimerFrames_[gpuTimerWriteIdx_];
    if (!frame.active || !frame.tsEnd || !frame.disjoint) return;
    context_->End(frame.tsEnd.Get());
    context_->End(frame.disjoint.Get());

    // 读取上一帧（另一个 slot）的结果
    int readIdx = 1 - gpuTimerWriteIdx_;
    auto& readFrame = gpuTimerFrames_[readIdx];
    if (readFrame.active) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;
        UINT64 tsBegin = 0, tsEnd = 0;
        if (context_->GetData(readFrame.disjoint.Get(), &disjointData, sizeof(disjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            context_->GetData(readFrame.tsBegin.Get(), &tsBegin, sizeof(tsBegin), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            context_->GetData(readFrame.tsEnd.Get(), &tsEnd, sizeof(tsEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
            if (!disjointData.Disjoint && disjointData.Frequency > 0) {
                gpuRenderTime_ = static_cast<float>(tsEnd - tsBegin) / static_cast<float>(disjointData.Frequency);
            }
            readFrame.active = false;
        }
        // 如果 GetData 返回 S_FALSE（数据未就绪），保留上一次的 gpuRenderTime_ 不变
    }

    // 翻转写入 slot
    gpuTimerWriteIdx_ = 1 - gpuTimerWriteIdx_;
}

float D3D11MultiPass::GetGpuRenderTime() const {
    return gpuRenderTime_;
}

#endif // _WIN32
