#pragma once

#include <string>
#include <array>
#include <glad/glad.h>

/// TextureManager 负责加载图片为 OpenGL 纹理，用于 ShaderToy 的 iChannel 输入。
class TextureManager {
public:
    static constexpr int kMaxChannels = 4;

    TextureManager();
    ~TextureManager();

    /// 为指定通道加载图片纹理
    /// @param channel 通道编号 (0-3)
    /// @param filePath 图片文件路径
    /// @return true 如果加载成功
    bool LoadTexture(int channel, const std::string& filePath);

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

    /// 将指定通道绑定为 FBO 纹理（用于 Buffer pass 输出）
    void SetBufferTexture(int channel, GLuint texture, int width, int height);

private:
    struct ChannelInfo {
        GLuint texture = 0;
        int width = 0;
        int height = 0;
        bool isOwned = true;  // 是否由 TextureManager 拥有（需要负责删除）
    };

    std::array<ChannelInfo, kMaxChannels> channels_;
};
