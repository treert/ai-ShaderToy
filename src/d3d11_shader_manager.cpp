#ifdef _WIN32

#include "d3d11_shader_manager.h"
#include "glsl_to_hlsl.h"
#include <d3dcompiler.h>
#include <iostream>
#include <sstream>

D3D11ShaderManager::D3D11ShaderManager() = default;

D3D11ShaderManager::~D3D11ShaderManager() = default;

D3D11ShaderManager::D3D11ShaderManager(D3D11ShaderManager&& other) noexcept
    : device_(other.device_), context_(other.context_),
      pixelShader_(std::move(other.pixelShader_)),
      constantBuffer_(std::move(other.constantBuffer_)),
      lastError_(std::move(other.lastError_)),
      commonSource_(std::move(other.commonSource_)),
      isCubeMapPass_(other.isCubeMapPass_),
      channelTypes_(other.channelTypes_) {
    other.device_ = nullptr;
    other.context_ = nullptr;
}

D3D11ShaderManager& D3D11ShaderManager::operator=(D3D11ShaderManager&& other) noexcept {
    if (this != &other) {
        device_ = other.device_;
        context_ = other.context_;
        pixelShader_ = std::move(other.pixelShader_);
        constantBuffer_ = std::move(other.constantBuffer_);
        lastError_ = std::move(other.lastError_);
        commonSource_ = std::move(other.commonSource_);
        isCubeMapPass_ = other.isCubeMapPass_;
        channelTypes_ = other.channelTypes_;
        other.device_ = nullptr;
        other.context_ = nullptr;
    }
    return *this;
}

void D3D11ShaderManager::SetDevice(ID3D11Device* device, ID3D11DeviceContext* context) {
    device_ = device;
    context_ = context;
}

void D3D11ShaderManager::SetChannelTypes(const std::array<ChannelType, 4>& types) {
    channelTypes_ = types;
}

void D3D11ShaderManager::SetCommonSource(const std::string& common) {
    commonSource_ = common;
}

void D3D11ShaderManager::SetCubeMapPassMode(bool isCubeMap) {
    isCubeMapPass_ = isCubeMap;
}

std::string D3D11ShaderManager::WrapShaderToyHlsl(const std::string& translatedHlsl) const {
    std::ostringstream hlsl;

    // Constant buffer（与 ShaderToyConstants 结构体对齐）
    hlsl << R"hlsl(
// ShaderToy uniform constant buffer
cbuffer ShaderToyUniforms : register(b0) {
    float4 iResolution;           // xyz=resolution, w=padding
    float  iTime;
    float  iTimeDelta;
    int    iFrame;
    float  iFrameRate;
    float4 iMouse;
    float4 iDate;
    float  iSampleRate;
    float  iClickTime;
    float2 _pad0;
    float4 iChannelTime;
    float4 iChannelResolution[4]; // xyz=res, w=padding
    float4 _cubeFaceRight;
    float4 _cubeFaceUp;
    float4 _cubeFaceDir;
};

)hlsl";

    // 纹理和采样器声明
    for (int i = 0; i < 4; ++i) {
        switch (channelTypes_[i]) {
        case ChannelType::CubeMap:
            hlsl << "TextureCube iChannel" << i << " : register(t" << i << ");\n";
            break;
        case ChannelType::Texture3D:
            hlsl << "Texture3D<float4> iChannel" << i << " : register(t" << i << ");\n";
            break;
        default:
            hlsl << "Texture2D<float4> iChannel" << i << " : register(t" << i << ");\n";
            break;
        }
    }
    hlsl << "\n";

    // 采样器（所有通道共享线性采样 + Wrap）
    for (int i = 0; i < 4; ++i) {
        hlsl << "SamplerState sampler" << i << " : register(s" << i << ");\n";
    }
    hlsl << "\n";

    // GLSL texture() 兼容函数（不能用 "texture" 作函数名，它是 HLSL 保留字）
    hlsl << R"hlsl(
// GLSL texture() compatibility (renamed to avoid HLSL reserved word "texture")
float4 _sampleTex2D(Texture2D<float4> tex, SamplerState samp, float2 uv) {
    return tex.Sample(samp, uv);
}
float4 _sampleTexCube(TextureCube tex, SamplerState samp, float3 uvw) {
    return tex.Sample(samp, uvw);
}
float4 _sampleTex3D(Texture3D<float4> tex, SamplerState samp, float3 uvw) {
    return tex.Sample(samp, uvw);
}
float4 _sampleTex2DLod(Texture2D<float4> tex, SamplerState samp, float2 uv, float lod) {
    return tex.SampleLevel(samp, uv, lod);
}
float4 _sampleTexCubeLod(TextureCube tex, SamplerState samp, float3 uvw, float lod) {
    return tex.SampleLevel(samp, uvw, lod);
}
float4 _fetchTex2D(Texture2D<float4> tex, int2 coord, int lod) {
    return tex.Load(int3(coord, lod));
}

// Redirect texture(iChannelN, ...) calls to use corresponding sampler
)hlsl";

    // 为每个通道生成 texture 重载
    for (int i = 0; i < 4; ++i) {
        std::string chName = "iChannel" + std::to_string(i);
        std::string sampName = "sampler" + std::to_string(i);

        switch (channelTypes_[i]) {
        case ChannelType::CubeMap:
            hlsl << "#define texture_" << chName << "(uv) _sampleTexCube(" << chName << ", " << sampName << ", (uv))\n";
            hlsl << "#define textureLod_" << chName << "(uv, lod) _sampleTexCubeLod(" << chName << ", " << sampName << ", (uv), (lod))\n";
            break;
        case ChannelType::Texture3D:
            hlsl << "#define texture_" << chName << "(uv) _sampleTex3D(" << chName << ", " << sampName << ", (uv))\n";
            hlsl << "#define textureLod_" << chName << "(uv, lod) _sampleTex3D(" << chName << ", " << sampName << ", (uv))\n";
            break;
        default:
            hlsl << "#define texture_" << chName << "(uv) _sampleTex2D(" << chName << ", " << sampName << ", (uv))\n";
            hlsl << "#define textureLod_" << chName << "(uv, lod) _sampleTex2DLod(" << chName << ", " << sampName << ", (uv), (lod))\n";
            hlsl << "#define texelFetch_" << chName << "(coord, lod) _fetchTex2D(" << chName << ", (coord), (lod))\n";
            break;
        }
    }
    hlsl << "\n";

    // 注入 Common 共享代码段
    if (!commonSource_.empty()) {
        std::string commonHlsl = TranslateGlslToHlsl(commonSource_);
        hlsl << "// === Common code begin ===\n";
        hlsl << commonHlsl;
        hlsl << "\n// === Common code end ===\n\n";
    }

    // 用户 shader 代码（已翻译为 HLSL）
    hlsl << translatedHlsl;
    hlsl << "\n";

    // main 函数
    if (isCubeMapPass_) {
        hlsl << R"hlsl(
// CubeMap pass entry point
struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target {
    float4 fragColor = float4(0.0, 0.0, 0.0, 1.0);
    // D3D11 SV_Position.y is top-down, ShaderToy fragCoord.y is bottom-up
    float2 fragCoord = float2(input.position.x, iResolution.y - input.position.y);
    float2 uv = fragCoord / iResolution.xy * 2.0 - 1.0;
    float3 rayDir = normalize(_cubeFaceDir.xyz + uv.x * _cubeFaceRight.xyz + uv.y * _cubeFaceUp.xyz);
    float3 rayOri = float3(0.0, 0.0, 0.0);
    mainCubemap(fragColor, fragCoord, rayOri, rayDir);
    return fragColor;
}
)hlsl";
    } else {
        hlsl << R"hlsl(
// Image/Buffer pass entry point
struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target {
    float4 fragColor = float4(0.0, 0.0, 0.0, 1.0);
    // D3D11 SV_Position.y is top-down, ShaderToy fragCoord.y is bottom-up
    float2 fragCoord = float2(input.position.x, iResolution.y - input.position.y);
    mainImage(fragColor, fragCoord);
    return fragColor;
}
)hlsl";
    }

    return hlsl.str();
}

bool D3D11ShaderManager::LoadFromSource(const std::string& glslSource) {
    if (!device_) {
        lastError_ = "D3D11 device not set.";
        return false;
    }

    // 释放旧资源
    pixelShader_.Reset();

    // 翻译 GLSL -> HLSL
    std::string translatedHlsl = TranslateGlslToHlsl(glslSource);

    // 用 HLSL 模板包装
    std::string fullHlsl = WrapShaderToyHlsl(translatedHlsl);

    // 编译 HLSL
    ComPtr<ID3D10Blob> shaderBlob;
    ComPtr<ID3D10Blob> errorBlob;

    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    HRESULT hr = D3DCompile(
        fullHlsl.c_str(),
        fullHlsl.size(),
        "ShaderToyPS",
        nullptr,    // defines
        nullptr,    // includes
        "main",
        "ps_5_0",
        compileFlags,
        0,
        &shaderBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        lastError_ = "HLSL compilation error:\n";
        if (errorBlob) {
            lastError_ += static_cast<const char*>(errorBlob->GetBufferPointer());
        }
        std::cerr << lastError_ << std::endl;

        // 输出完整的 HLSL 源码以便调试
        std::cerr << "=== Full HLSL source ===\n" << fullHlsl << "\n=== End HLSL ===\n";
        return false;
    }

    // 创建 Pixel Shader
    hr = device_->CreatePixelShader(
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        nullptr,
        &pixelShader_
    );

    if (FAILED(hr)) {
        lastError_ = "CreatePixelShader failed.";
        return false;
    }

    // 创建 Constant Buffer（如果还没有）
    if (!constantBuffer_) {
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(ShaderToyConstants);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        hr = device_->CreateBuffer(&cbDesc, nullptr, &constantBuffer_);
        if (FAILED(hr)) {
            lastError_ = "CreateBuffer for constant buffer failed.";
            return false;
        }
    }

    return true;
}

void D3D11ShaderManager::Use() {
    if (!context_ || !pixelShader_) return;
    context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
    if (constantBuffer_) {
        ID3D11Buffer* cb = constantBuffer_.Get();
        context_->PSSetConstantBuffers(0, 1, &cb);
    }
}

void D3D11ShaderManager::UpdateConstants(const ShaderToyConstants& constants) {
    if (!context_ || !constantBuffer_) return;

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context_->Map(constantBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        memcpy(mapped.pData, &constants, sizeof(ShaderToyConstants));
        context_->Unmap(constantBuffer_.Get(), 0);
    }
}

void D3D11ShaderManager::BindTextureSRV(int slot, ID3D11ShaderResourceView* srv) {
    if (!context_) return;
    context_->PSSetShaderResources(static_cast<UINT>(slot), 1, &srv);
}

void D3D11ShaderManager::BindSampler(int slot, ID3D11SamplerState* sampler) {
    if (!context_) return;
    context_->PSSetSamplers(static_cast<UINT>(slot), 1, &sampler);
}

#endif // _WIN32
