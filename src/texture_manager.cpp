#include "texture_manager.h"
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

TextureManager::TextureManager() = default;

TextureManager::~TextureManager() {
    for (auto& ch : channels_) {
        if (ch.texture && ch.isOwned) {
            glDeleteTextures(1, &ch.texture);
        }
    }
}

bool TextureManager::LoadTexture(int channel, const std::string& filePath) {
    if (channel < 0 || channel >= kMaxChannels) {
        std::cerr << "Invalid channel: " << channel << std::endl;
        return false;
    }

    // 释放旧纹理
    auto& ch = channels_[channel];
    if (ch.texture && ch.isOwned) {
        glDeleteTextures(1, &ch.texture);
        ch.texture = 0;
    }

    // 用 stb_image 加载图片
    stbi_set_flip_vertically_on_load(true);
    int width, height, nrChannels;
    unsigned char* data = stbi_load(filePath.c_str(), &width, &height, &nrChannels, 4);
    if (!data) {
        std::cerr << "Failed to load texture: " << filePath
                  << " (" << stbi_failure_reason() << ")" << std::endl;
        return false;
    }

    // 创建 OpenGL 纹理
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);

    ch.texture = tex;
    ch.width = width;
    ch.height = height;
    ch.isOwned = true;

    std::cout << "Texture loaded: channel " << channel
              << " [" << width << "x" << height << "] " << filePath << std::endl;
    return true;
}

void TextureManager::BindAll() const {
    for (int i = 0; i < kMaxChannels; ++i) {
        Bind(i);
    }
}

void TextureManager::Bind(int channel) const {
    if (channel < 0 || channel >= kMaxChannels) return;
    glActiveTexture(GL_TEXTURE0 + channel);
    if (channels_[channel].texture) {
        glBindTexture(GL_TEXTURE_2D, channels_[channel].texture);
    } else {
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void TextureManager::GetResolution(int channel, float& width, float& height) const {
    if (channel >= 0 && channel < kMaxChannels) {
        width = static_cast<float>(channels_[channel].width);
        height = static_cast<float>(channels_[channel].height);
    } else {
        width = height = 0.0f;
    }
}

void TextureManager::GetAllResolutions(float out[4][3]) const {
    for (int i = 0; i < kMaxChannels; ++i) {
        out[i][0] = static_cast<float>(channels_[i].width);
        out[i][1] = static_cast<float>(channels_[i].height);
        out[i][2] = 1.0f;
    }
}

bool TextureManager::HasTexture(int channel) const {
    return channel >= 0 && channel < kMaxChannels && channels_[channel].texture != 0;
}

void TextureManager::SetBufferTexture(int channel, GLuint texture, int width, int height) {
    if (channel < 0 || channel >= kMaxChannels) return;
    auto& ch = channels_[channel];
    if (ch.texture && ch.isOwned) {
        glDeleteTextures(1, &ch.texture);
    }
    ch.texture = texture;
    ch.width = width;
    ch.height = height;
    ch.isOwned = false;  // buffer 纹理由 MultiPassRenderer 管理
}
