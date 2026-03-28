#pragma once

#include <string>
#include <array>
#include <glad/glad.h>
#include "shader_manager.h"  // for ChannelType

/// TextureManager 负责加载图片为 OpenGL 纹理，用于 ShaderToy 的 iChannel 输入。
/// 支持 2D 纹理和 CubeMap。
class TextureManager {
public:
    static constexpr int kMaxChannels = 4;

    TextureManager();
    ~TextureManager();

    /// 为指定通道加载 2D 纹理
    bool LoadTexture(int channel, const std::string& filePath);

    /// 为指定通道加载 CubeMap
    /// 支持两种模式：
    ///   1. 单张图片（自动检测十字/横条/竖条布局并切割为6面）
    ///   2. 路径含通配符 %s，自动替换为 px/nx/py/ny/pz/nz 加载6张文件
    bool LoadCubeMap(int channel, const std::string& filePath);

    /// 绑定所有通道纹理到对应纹理单元
    void BindAll() const;

    /// 绑定指定通道到对应纹理单元
    void Bind(int channel) const;

    /// 获取指定通道的纹理尺寸
    void GetResolution(int channel, float& width, float& height) const;

    /// 获取所有通道的分辨率（用于 iChannelResolution uniform）
    void GetAllResolutions(float out[4][3]) const;

    /// 指定通道是否已加载纹理
    bool HasTexture(int channel) const;

    /// 清除所有纹理（释放 owned 纹理的 GL 资源）
    void Clear();

    /// 获取通道的采样器类型
    ChannelType GetChannelType(int channel) const;

    /// 获取指定通道的 OpenGL 纹理 ID（0 表示未加载）
    GLuint GetTextureID(int channel) const;

    /// 将指定通道绑定为 FBO 纹理（用于 Buffer pass 输出）
    void SetBufferTexture(int channel, GLuint texture, int width, int height);

private:
    /// 从单张图片切割6面生成 CubeMap
    GLuint CreateCubeMapFromSingleImage(unsigned char* data, int imgW, int imgH,
                                        int faceSize);

    struct ChannelInfo {
        GLuint texture = 0;
        int width = 0;
        int height = 0;
        bool isOwned = true;
        ChannelType type = ChannelType::None;
    };

    std::array<ChannelInfo, kMaxChannels> channels_;
};
