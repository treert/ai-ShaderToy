#ifdef _WIN32

#include "d3d11_texture_manager.h"
#include <iostream>
#include <vector>
#include <cstring>

// stb_image 已在 texture_manager.cpp 中定义实现，这里只声明
#include "stb_image.h"

D3D11TextureManager::D3D11TextureManager() = default;

D3D11TextureManager::~D3D11TextureManager() {
    Clear();
}

void D3D11TextureManager::SetDevice(ID3D11Device* device, ID3D11DeviceContext* context) {
    device_ = device;
    context_ = context;
    CreateDefaultSampler();
}

bool D3D11TextureManager::CreateDefaultSampler() {
    if (!device_) return false;

    D3D11_SAMPLER_DESC desc = {};
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.MipLODBias = 0.0f;
    desc.MaxAnisotropy = 1;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.MinLOD = 0;
    desc.MaxLOD = D3D11_FLOAT32_MAX;

    HRESULT hr = device_->CreateSamplerState(&desc, &defaultSampler_);
    if (FAILED(hr)) {
        std::cerr << "D3D11TextureManager: CreateSamplerState failed." << std::endl;
        return false;
    }
    return true;
}

bool D3D11TextureManager::LoadTexture(int channel, const std::string& filePath) {
    if (channel < 0 || channel >= kMaxChannels || !device_) return false;

    int imgW, imgH, channels;
    unsigned char* data = stbi_load(filePath.c_str(), &imgW, &imgH, &channels, 4);
    if (!data) {
        std::cerr << "D3D11TextureManager: Failed to load image: " << filePath << std::endl;
        return false;
    }

    // 创建 D3D11 Texture2D
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = static_cast<UINT>(imgW);
    texDesc.Height = static_cast<UINT>(imgH);
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data;
    initData.SysMemPitch = static_cast<UINT>(imgW * 4);

    auto& ch = channels_[channel];
    ch.texture.Reset();
    ch.srv.Reset();

    HRESULT hr = device_->CreateTexture2D(&texDesc, &initData, &ch.texture);
    stbi_image_free(data);

    if (FAILED(hr)) {
        std::cerr << "D3D11TextureManager: CreateTexture2D failed for " << filePath << std::endl;
        return false;
    }

    // 创建 SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = device_->CreateShaderResourceView(ch.texture.Get(), &srvDesc, &ch.srv);
    if (FAILED(hr)) {
        std::cerr << "D3D11TextureManager: CreateSRV failed for " << filePath << std::endl;
        ch.texture.Reset();
        return false;
    }

    ch.width = imgW;
    ch.height = imgH;
    ch.isOwned = true;
    ch.type = ChannelType::Texture2D;

    std::cout << "D3D11 Texture loaded: " << filePath
              << " (" << imgW << "x" << imgH << ")" << std::endl;
    return true;
}

bool D3D11TextureManager::LoadCubeMap(int channel, const std::string& filePath) {
    if (channel < 0 || channel >= kMaxChannels || !device_) return false;

    // 加载单张图片
    int imgW, imgH, channels;
    unsigned char* data = stbi_load(filePath.c_str(), &imgW, &imgH, &channels, 4);
    if (!data) {
        std::cerr << "D3D11TextureManager: Failed to load cubemap image: " << filePath << std::endl;
        return false;
    }

    // 检测布局：横条 (6:1)、竖条 (1:6)、十字 (4:3 或 3:4)
    int faceSize = 0;
    enum Layout { CrossH, CrossV, StripH, StripV, Unknown } layout = Unknown;

    if (imgW == imgH * 6) {
        faceSize = imgH;
        layout = StripH;
    } else if (imgH == imgW * 6) {
        faceSize = imgW;
        layout = StripV;
    } else if (imgW * 3 == imgH * 4) {
        faceSize = imgW / 4;
        layout = CrossH;
    } else if (imgW * 4 == imgH * 3) {
        faceSize = imgH / 4;
        layout = CrossV;
    } else {
        std::cerr << "D3D11TextureManager: Unrecognized cubemap layout: "
                  << imgW << "x" << imgH << std::endl;
        stbi_image_free(data);
        return false;
    }

    // 面的偏移（十字布局: +X, -X, +Y, -Y, +Z, -Z）
    struct FaceOffset { int x, y; };
    FaceOffset offsets[6];
    if (layout == CrossH) {
        // 水平十字: 4宽 x 3高
        offsets[0] = {2, 1}; // +X
        offsets[1] = {0, 1}; // -X
        offsets[2] = {1, 0}; // +Y
        offsets[3] = {1, 2}; // -Y
        offsets[4] = {1, 1}; // +Z
        offsets[5] = {3, 1}; // -Z
    } else if (layout == StripH) {
        for (int i = 0; i < 6; ++i) offsets[i] = {i, 0};
    } else if (layout == StripV) {
        for (int i = 0; i < 6; ++i) offsets[i] = {0, i};
    } else { // CrossV
        offsets[0] = {1, 2}; // +X
        offsets[1] = {1, 0}; // -X
        offsets[2] = {0, 1}; // +Y
        offsets[3] = {2, 1}; // -Y
        offsets[4] = {1, 1}; // +Z
        offsets[5] = {1, 3}; // -Z
    }

    // 提取6面数据
    std::vector<std::vector<unsigned char>> faceData(6);
    for (int face = 0; face < 6; ++face) {
        faceData[face].resize(faceSize * faceSize * 4);
        int srcX = offsets[face].x * faceSize;
        int srcY = offsets[face].y * faceSize;
        for (int y = 0; y < faceSize; ++y) {
            memcpy(&faceData[face][y * faceSize * 4],
                   &data[((srcY + y) * imgW + srcX) * 4],
                   faceSize * 4);
        }
    }
    stbi_image_free(data);

    // 创建 D3D11 CubeMap Texture
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = static_cast<UINT>(faceSize);
    texDesc.Height = static_cast<UINT>(faceSize);
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 6;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    D3D11_SUBRESOURCE_DATA initData[6] = {};
    for (int i = 0; i < 6; ++i) {
        initData[i].pSysMem = faceData[i].data();
        initData[i].SysMemPitch = static_cast<UINT>(faceSize * 4);
    }

    auto& ch = channels_[channel];
    ch.texture.Reset();
    ch.srv.Reset();

    HRESULT hr = device_->CreateTexture2D(&texDesc, initData, &ch.texture);
    if (FAILED(hr)) {
        std::cerr << "D3D11TextureManager: CreateTexture2D (cubemap) failed." << std::endl;
        return false;
    }

    // 创建 CubeMap SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MipLevels = 1;

    hr = device_->CreateShaderResourceView(ch.texture.Get(), &srvDesc, &ch.srv);
    if (FAILED(hr)) {
        std::cerr << "D3D11TextureManager: CreateSRV (cubemap) failed." << std::endl;
        ch.texture.Reset();
        return false;
    }

    ch.width = faceSize;
    ch.height = faceSize;
    ch.isOwned = true;
    ch.type = ChannelType::CubeMap;

    std::cout << "D3D11 CubeMap loaded: " << filePath
              << " (" << faceSize << "x" << faceSize << " per face)" << std::endl;
    return true;
}

ID3D11ShaderResourceView* D3D11TextureManager::GetSRV(int channel) const {
    if (channel < 0 || channel >= kMaxChannels) return nullptr;
    return channels_[channel].srv.Get();
}

void D3D11TextureManager::GetResolution(int channel, float& width, float& height) const {
    if (channel < 0 || channel >= kMaxChannels) {
        width = height = 0.0f;
        return;
    }
    width = static_cast<float>(channels_[channel].width);
    height = static_cast<float>(channels_[channel].height);
}

void D3D11TextureManager::GetAllResolutions(float out[4][3]) const {
    for (int i = 0; i < kMaxChannels; ++i) {
        out[i][0] = static_cast<float>(channels_[i].width);
        out[i][1] = static_cast<float>(channels_[i].height);
        out[i][2] = 1.0f;
    }
}

bool D3D11TextureManager::HasTexture(int channel) const {
    if (channel < 0 || channel >= kMaxChannels) return false;
    return channels_[channel].srv != nullptr;
}

void D3D11TextureManager::Clear() {
    for (auto& ch : channels_) {
        if (ch.isOwned) {
            ch.texture.Reset();
        }
        ch.srv.Reset();
        ch.width = 0;
        ch.height = 0;
        ch.isOwned = true;
        ch.type = ChannelType::None;
    }
}

ChannelType D3D11TextureManager::GetChannelType(int channel) const {
    if (channel < 0 || channel >= kMaxChannels) return ChannelType::None;
    return channels_[channel].type;
}

void D3D11TextureManager::SetBufferTextureSRV(int channel, ID3D11ShaderResourceView* srv,
                                               int width, int height) {
    if (channel < 0 || channel >= kMaxChannels) return;
    auto& ch = channels_[channel];
    ch.srv = srv;
    ch.width = width;
    ch.height = height;
    ch.isOwned = false;
    ch.type = ChannelType::Texture2D;
}

#endif // _WIN32
