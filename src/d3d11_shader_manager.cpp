#ifdef _WIN32

#include "d3d11_shader_manager.h"
#include "glsl_to_hlsl.h"
#include <d3dcompiler.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

// ============================================================
// ShaderIncludeHandler — ID3DInclude 实现，支持 #include 查找
// ============================================================

class ShaderIncludeHandler : public ID3DInclude {
public:
    ShaderIncludeHandler(const std::string& shaderDir, const std::string& assetsDir)
        : shaderDir_(shaderDir), assetsDir_(assetsDir) {}

    HRESULT __stdcall Open(D3D_INCLUDE_TYPE /*type*/, LPCSTR pFileName,
                            LPCVOID /*pParentData*/, LPCVOID* ppData, UINT* pBytes) override {
        // 按优先级查找：1. shader 目录 2. assets 目录
        std::string searchPaths[] = {
            shaderDir_ + "/" + pFileName,
            assetsDir_ + "/" + pFileName,
        };

        for (const auto& path : searchPaths) {
            std::ifstream file(path, std::ios::binary);
            if (file.is_open()) {
                file.seekg(0, std::ios::end);
                size_t size = static_cast<size_t>(file.tellg());
                file.seekg(0, std::ios::beg);

                char* data = new char[size];
                file.read(data, size);

                *ppData = data;
                *pBytes = static_cast<UINT>(size);
                return S_OK;
            }
        }

        std::cerr << "ShaderIncludeHandler: file not found: " << pFileName << std::endl;
        return E_FAIL;
    }

    HRESULT __stdcall Close(LPCVOID pData) override {
        delete[] static_cast<const char*>(pData);
        return S_OK;
    }

private:
    std::string shaderDir_;
    std::string assetsDir_;
};

D3D11ShaderManager::D3D11ShaderManager() = default;

D3D11ShaderManager::~D3D11ShaderManager() = default;

D3D11ShaderManager::D3D11ShaderManager(D3D11ShaderManager&& other) noexcept
    : device_(other.device_), context_(other.context_),
      pixelShader_(std::move(other.pixelShader_)),
      constantBuffer_(std::move(other.constantBuffer_)),
      lastError_(std::move(other.lastError_)),
      commonSource_(std::move(other.commonSource_)),
      isCubeMapPass_(other.isCubeMapPass_),
      flipFragCoordY_(other.flipFragCoordY_),
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
        flipFragCoordY_ = other.flipFragCoordY_;
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

void D3D11ShaderManager::SetFlipFragCoordY(bool flip) {
    flipFragCoordY_ = flip;
}

std::string D3D11ShaderManager::WrapShaderToyHlsl(const std::string& translatedHlsl) const {
    return ::WrapShaderToyHlsl(translatedHlsl, channelTypes_, commonSource_, isCubeMapPass_);
}

bool D3D11ShaderManager::LoadFromSource(const std::string& glslSource) {
    if (!device_) {
        lastError_ = "D3D11 device not set.";
        return false;
    }

    // 释放旧资源
    pixelShader_.Reset();

    // 翻译 GLSL -> HLSL（SPIRV-Cross 管线或旧正则翻译器）
    std::string translateErrors;
    std::string fullHlsl = TranslateGlslToFullHlsl(glslSource, channelTypes_,
                                                     commonSource_, isCubeMapPass_,
                                                     flipFragCoordY_,
                                                     &translateErrors);
    if (fullHlsl.empty()) {
        lastError_ = "GLSL->HLSL translation failed:\n" + translateErrors;
        std::cerr << lastError_ << std::endl;
        return false;
    }

    // 编译 HLSL
    ComPtr<ID3D10Blob> shaderBlob;
    ComPtr<ID3D10Blob> errorBlob;

    UINT compileFlags = 0;  // 不启用 ENABLE_STRICTNESS，兼容 GLSL 语义（如 out 参数未完全初始化）
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

bool D3D11ShaderManager::LoadFromHlsl(const std::string& hlslSource,
                                       const std::string& shaderDir,
                                       const std::string& assetsDir) {
    if (!device_) {
        lastError_ = "D3D11 device not set.";
        return false;
    }

    // 释放旧资源
    pixelShader_.Reset();

    // 直接编译 HLSL（跳过 GLSL→HLSL 翻译）
    ComPtr<ID3D10Blob> shaderBlob;
    ComPtr<ID3D10Blob> errorBlob;

    UINT compileFlags = 0;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ShaderIncludeHandler includeHandler(shaderDir, assetsDir);

    HRESULT hr = D3DCompile(
        hlslSource.c_str(),
        hlslSource.size(),
        "ShaderToyPS_HLSL",
        nullptr,            // defines
        &includeHandler,    // includes — 支持 #include
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
        std::cerr << "=== Full HLSL source ===\n" << hlslSource << "\n=== End HLSL ===\n";
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
