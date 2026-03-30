#pragma once

#ifdef _WIN32

#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <array>
#include "shader_manager.h"  // for ChannelType

using Microsoft::WRL::ComPtr;

/// D3D11TextureManager 管理 D3D11 纹理资源，与 TextureManager（OpenGL 版本）接口对齐。
class D3D11TextureManager {
public:
    static constexpr int kMaxChannels = 4;

    D3D11TextureManager();
    ~D3D11TextureManager();

    /// 设置 D3D11 设备
    void SetDevice(ID3D11Device* device, ID3D11DeviceContext* context);

    /// 为指定通道加载 2D 纹理
    bool LoadTexture(int channel, const std::string& filePath);

    /// 为指定通道加载 CubeMap
    bool LoadCubeMap(int channel, const std::string& filePath);

    /// 获取指定通道的 SRV
    ID3D11ShaderResourceView* GetSRV(int channel) const;

    /// 获取指定通道的纹理尺寸
    void GetResolution(int channel, float& width, float& height) const;

    /// 获取所有通道的分辨率
    void GetAllResolutions(float out[4][3]) const;

    /// 指定通道是否已加载纹理
    bool HasTexture(int channel) const;

    /// 清除所有纹理
    void Clear();

    /// 获取通道的采样器类型
    ChannelType GetChannelType(int channel) const;

    /// 获取默认采样器（线性 + Wrap）
    ID3D11SamplerState* GetDefaultSampler() const { return defaultSampler_.Get(); }

    /// 设置 Buffer pass 输出纹理的 SRV（类似 TextureManager::SetBufferTexture）
    void SetBufferTextureSRV(int channel, ID3D11ShaderResourceView* srv, int width, int height);

private:
    bool CreateDefaultSampler();

    struct ChannelInfo {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        int width = 0;
        int height = 0;
        bool isOwned = true;
        ChannelType type = ChannelType::None;
    };

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    std::array<ChannelInfo, kMaxChannels> channels_;
    ComPtr<ID3D11SamplerState> defaultSampler_;
};

#endif // _WIN32
