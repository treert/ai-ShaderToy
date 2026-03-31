#pragma once

#ifdef _WIN32

#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <array>
#include "shader_manager.h"  // for ChannelType, UniformLocations (reuse enum)

using Microsoft::WRL::ComPtr;

/// ShaderToy uniform 常量缓冲区布局（必须 16 字节对齐）
/// 注意：HLSL cbuffer 中 float3 后面会填充到 16 字节
struct alignas(16) ShaderToyConstants {
    float iResolution[4];           // xyz=分辨率, w=padding (16B)
    float iTime;                    // (4B)
    float iTimeDelta;               // (4B)
    int   iFrame;                   // (4B)
    float iFrameRate;               // (4B)
    float iMouse[4];                // (16B)
    float iDate[4];                 // (16B)
    float iSampleRate;              // (4B)
    float iClickTime;               // (4B)
    float _pad0[2];                 // padding to 16B boundary (8B)
    float iChannelTime[4];          // (16B)
    float iChannelResolution[4][4]; // 4 x float4 (每个 xyz + padding) (64B)
    // CubeMap pass uniforms
    float cubeFaceRight[4];         // xyz + padding (16B)
    float cubeFaceUp[4];            // xyz + padding (16B)
    float cubeFaceDir[4];           // xyz + padding (16B)
};

/// D3D11ShaderManager 管理 HLSL shader 编译和 uniform 更新。
/// 接口与 ShaderManager（OpenGL 版本）对齐。
class D3D11ShaderManager {
public:
    D3D11ShaderManager();
    ~D3D11ShaderManager();

    D3D11ShaderManager(D3D11ShaderManager&& other) noexcept;
    D3D11ShaderManager& operator=(D3D11ShaderManager&& other) noexcept;
    D3D11ShaderManager(const D3D11ShaderManager&) = delete;
    D3D11ShaderManager& operator=(const D3D11ShaderManager&) = delete;

    /// 设置 D3D11 设备（在 LoadFromSource 之前调用）
    void SetDevice(ID3D11Device* device, ID3D11DeviceContext* context);

    /// 设置各通道的采样器类型（在 Load 之前调用）
    void SetChannelTypes(const std::array<ChannelType, 4>& types);

    /// 设置 Common 共享代码段
    void SetCommonSource(const std::string& common);

    /// 设置是否为 CubeMap pass
    void SetCubeMapPassMode(bool isCubeMap);

    /// 设置是否翻转 gl_FragCoord.y（Image pass = true，Buffer pass = false）
    void SetFlipFragCoordY(bool flip);

    /// 从 GLSL 源码加载（内部翻译为 HLSL 并编译）
    bool LoadFromSource(const std::string& glslSource);

    /// 从 HLSL 源码直接编译（HLSL 原生模式，跳过翻译）
    /// @param hlslSource 完整的 HLSL 源码（用户需 #include "shadertoy_uniforms.hlsl"）
    /// @param shaderDir  shader 文件所在目录（用于 #include 查找）
    /// @param assetsDir  assets 目录路径（用于查找头文件模板）
    bool LoadFromHlsl(const std::string& hlslSource,
                      const std::string& shaderDir,
                      const std::string& assetsDir);

    /// 激活 pixel shader 并绑定 constant buffer
    void Use();

    /// 更新 uniform constant buffer
    void UpdateConstants(const ShaderToyConstants& constants);

    /// 将纹理 SRV 绑定到 PS 的指定槽位
    void BindTextureSRV(int slot, ID3D11ShaderResourceView* srv);

    /// 将采样器绑定到 PS 的指定槽位
    void BindSampler(int slot, ID3D11SamplerState* sampler);

    /// 获取 Pixel Shader
    ID3D11PixelShader* GetPixelShader() const { return pixelShader_.Get(); }

    /// 获取最近的编译错误信息
    const std::string& GetLastError() const { return lastError_; }

    /// 获取当前通道类型
    const std::array<ChannelType, 4>& GetChannelTypes() const { return channelTypes_; }

private:
    /// 将 ShaderToy GLSL 包装成完整的 HLSL Pixel Shader
    std::string WrapShaderToyHlsl(const std::string& translatedHlsl) const;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;

    ComPtr<ID3D11PixelShader> pixelShader_;
    ComPtr<ID3D11Buffer> constantBuffer_;

    std::string lastError_;
    std::string commonSource_;
    bool isCubeMapPass_ = false;
    bool flipFragCoordY_ = true;  // Image pass = true, Buffer pass = false
    std::array<ChannelType, 4> channelTypes_ = {
        ChannelType::Texture2D, ChannelType::Texture2D,
        ChannelType::Texture2D, ChannelType::Texture2D
    };
};

#endif // _WIN32
