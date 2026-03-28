#include "texture_manager.h"
#include <iostream>
#include <cstring>
#include <vector>

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

    auto& ch = channels_[channel];
    if (ch.texture && ch.isOwned) {
        glDeleteTextures(1, &ch.texture);
        ch.texture = 0;
    }

    stbi_set_flip_vertically_on_load(true);
    int width, height, nrChannels;
    unsigned char* data = stbi_load(filePath.c_str(), &width, &height, &nrChannels, 4);
    if (!data) {
        std::cerr << "Failed to load texture: " << filePath
                  << " (" << stbi_failure_reason() << ")" << std::endl;
        return false;
    }

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
    ch.type = ChannelType::Texture2D;

    std::cout << "Texture2D loaded: channel " << channel
              << " [" << width << "x" << height << "] " << filePath << std::endl;
    return true;
}

bool TextureManager::LoadCubeMap(int channel, const std::string& filePath) {
    if (channel < 0 || channel >= kMaxChannels) {
        std::cerr << "Invalid channel: " << channel << std::endl;
        return false;
    }

    auto& ch = channels_[channel];
    if (ch.texture && ch.isOwned) {
        glDeleteTextures(1, &ch.texture);
        ch.texture = 0;
    }

    // 尝试方式1：路径含 %s，加载6张面文件
    if (filePath.find("%s") != std::string::npos) {
        const char* faceNames[] = {"px", "nx", "py", "ny", "pz", "nz"};
        GLenum faceTargets[] = {
            GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
            GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
            GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
        };

        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_CUBE_MAP, tex);

        stbi_set_flip_vertically_on_load(false);
        int faceSize = 0;
        for (int i = 0; i < 6; ++i) {
            // 替换 %s
            std::string facePath = filePath;
            size_t pos = facePath.find("%s");
            facePath.replace(pos, 2, faceNames[i]);

            int w, h, nc;
            unsigned char* data = stbi_load(facePath.c_str(), &w, &h, &nc, 4);
            if (!data) {
                std::cerr << "Failed to load cubemap face: " << facePath
                          << " (" << stbi_failure_reason() << ")" << std::endl;
                glDeleteTextures(1, &tex);
                return false;
            }
            if (i == 0) faceSize = w;
            glTexImage2D(faceTargets[i], 0, GL_RGBA8, w, h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

        ch.texture = tex;
        ch.width = faceSize;
        ch.height = faceSize;
        ch.isOwned = true;
        ch.type = ChannelType::CubeMap;

        std::cout << "CubeMap loaded (6 files): channel " << channel
                  << " [" << faceSize << "x" << faceSize << "]" << std::endl;
        return true;
    }

    // 方式2：单张图片，自动检测布局并切割
    stbi_set_flip_vertically_on_load(false);
    int imgW, imgH, nc;
    unsigned char* data = stbi_load(filePath.c_str(), &imgW, &imgH, &nc, 4);
    if (!data) {
        std::cerr << "Failed to load cubemap image: " << filePath
                  << " (" << stbi_failure_reason() << ")" << std::endl;
        return false;
    }

    // 判断布局：
    //   横条 6:1 → faceSize = imgH
    //   竖条 1:6 → faceSize = imgW
    //   十字 4:3 → faceSize = imgW/4
    //   十字 3:4 → faceSize = imgH/4
    int faceSize = 0;
    enum Layout { Horizontal, Vertical, CrossH, CrossV, Unknown } layout = Unknown;

    if (imgW == imgH * 6) {
        faceSize = imgH;
        layout = Horizontal;
    } else if (imgH == imgW * 6) {
        faceSize = imgW;
        layout = Vertical;
    } else if (imgW * 3 == imgH * 4) {
        faceSize = imgW / 4;
        layout = CrossH;
    } else if (imgH * 3 == imgW * 4) {
        faceSize = imgH / 4;
        layout = CrossV;
    } else {
        std::cerr << "CubeMap: unrecognized layout " << imgW << "x" << imgH
                  << ". Expected 6:1, 1:6, 4:3, or 3:4 aspect ratio." << std::endl;
        stbi_image_free(data);
        return false;
    }

    GLuint tex = CreateCubeMapFromSingleImage(data, imgW, imgH, faceSize);
    stbi_image_free(data);

    if (!tex) return false;

    ch.texture = tex;
    ch.width = faceSize;
    ch.height = faceSize;
    ch.isOwned = true;
    ch.type = ChannelType::CubeMap;

    std::cout << "CubeMap loaded (single image): channel " << channel
              << " [" << faceSize << "x" << faceSize << "] " << filePath << std::endl;
    return true;
}

GLuint TextureManager::CreateCubeMapFromSingleImage(unsigned char* data,
                                                     int imgW, int imgH,
                                                     int faceSize) {
    // 确定布局和每个面的偏移
    // 十字布局 (4:3 横向):
    //        [+Y]
    //  [-X]  [+Z]  [+X]  [-Z]
    //        [-Y]
    // 横条: +X -X +Y -Y +Z -Z
    struct FaceOffset { int x, y; };

    FaceOffset offsets[6];
    if (imgW == imgH * 6) {
        // 横条: 依次 +X -X +Y -Y +Z -Z
        for (int i = 0; i < 6; ++i) {
            offsets[i] = {i * faceSize, 0};
        }
    } else if (imgH == imgW * 6) {
        // 竖条
        for (int i = 0; i < 6; ++i) {
            offsets[i] = {0, i * faceSize};
        }
    } else if (imgW * 3 == imgH * 4) {
        // 十字横向 4:3
        // +X(2,1) -X(0,1) +Y(1,0) -Y(1,2) +Z(1,1) -Z(3,1)
        offsets[0] = {2 * faceSize, 1 * faceSize}; // +X
        offsets[1] = {0 * faceSize, 1 * faceSize}; // -X
        offsets[2] = {1 * faceSize, 0 * faceSize}; // +Y
        offsets[3] = {1 * faceSize, 2 * faceSize}; // -Y
        offsets[4] = {1 * faceSize, 1 * faceSize}; // +Z
        offsets[5] = {3 * faceSize, 1 * faceSize}; // -Z
    } else {
        // 十字竖向 3:4
        offsets[0] = {2 * faceSize, 1 * faceSize};
        offsets[1] = {0 * faceSize, 1 * faceSize};
        offsets[2] = {1 * faceSize, 0 * faceSize};
        offsets[3] = {1 * faceSize, 2 * faceSize};
        offsets[4] = {1 * faceSize, 1 * faceSize};
        offsets[5] = {1 * faceSize, 3 * faceSize};
    }

    GLenum faceTargets[] = {
        GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
    };

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);

    // 提取每个面的像素数据
    std::vector<unsigned char> faceData(faceSize * faceSize * 4);
    int stride = imgW * 4;

    for (int face = 0; face < 6; ++face) {
        int ox = offsets[face].x;
        int oy = offsets[face].y;
        for (int row = 0; row < faceSize; ++row) {
            memcpy(&faceData[row * faceSize * 4],
                   &data[(oy + row) * stride + ox * 4],
                   faceSize * 4);
        }
        glTexImage2D(faceTargets[face], 0, GL_RGBA8, faceSize, faceSize, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, faceData.data());
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    return tex;
}

void TextureManager::BindAll() const {
    for (int i = 0; i < kMaxChannels; ++i) {
        Bind(i);
    }
}

void TextureManager::Bind(int channel) const {
    if (channel < 0 || channel >= kMaxChannels) return;
    glActiveTexture(GL_TEXTURE0 + channel);
    const auto& ch = channels_[channel];
    if (ch.texture) {
        GLenum target = (ch.type == ChannelType::CubeMap) ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        glBindTexture(target, ch.texture);
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

ChannelType TextureManager::GetChannelType(int channel) const {
    if (channel >= 0 && channel < kMaxChannels) {
        return channels_[channel].type;
    }
    return ChannelType::None;
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
    ch.isOwned = false;
    ch.type = ChannelType::Texture2D;
}
