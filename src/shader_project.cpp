#include "shader_project.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ============================================================
// ShaderProjectData
// ============================================================

std::vector<std::string> ShaderProjectData::GetExternalTexturePaths() const {
    std::vector<std::string> paths;

    auto collectFromPass = [&](const PassData& pass) {
        for (const auto& ch : pass.channels) {
            if (ch.source == ChannelBinding::Source::ExternalTexture && !ch.texturePath.empty()) {
                if (std::find(paths.begin(), paths.end(), ch.texturePath) == paths.end()) {
                    paths.push_back(ch.texturePath);
                }
            }
        }
    };

    collectFromPass(imagePass);
    for (const auto& buf : bufferPasses) {
        collectFromPass(buf);
    }
    return paths;
}

// ============================================================
// ShaderProject
// ============================================================

bool ShaderProject::Load(const std::string& path) {
    data_ = ShaderProjectData{};
    allFiles_.clear();
    lastError_.clear();
    sourcePath_ = path;

    // 判断路径类型
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        return LoadFromDirectory(path);
    }

    // 根据扩展名判断
    fs::path p(path);
    std::string ext = p.extension().string();
    // 转小写
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (ext == ".json") {
        return LoadFromJSON(path);
    } else {
        // 默认作为单文件 .glsl
        return LoadSingleFile(path);
    }
}

// ============================================================
// 单文件模式
// ============================================================

bool ShaderProject::LoadSingleFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        lastError_ = "Failed to open shader file: " + path;
        return false;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    std::string code = ss.str();

    if (code.empty()) {
        lastError_ = "Shader file is empty: " + path;
        return false;
    }

    data_.projectName = fs::path(path).stem().string();
    data_.imagePass.name = "Image";
    data_.imagePass.code = code;
    data_.isMultiPass = false;
    allFiles_.push_back(path);

    std::cout << "ShaderProject: loaded single file '" << path << "'" << std::endl;
    return true;
}

// ============================================================
// JSON 模式
// ============================================================

bool ShaderProject::LoadFromJSON(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        lastError_ = "Failed to open JSON file: " + path;
        return false;
    }

    json jsonData;
    try {
        file >> jsonData;
    } catch (const json::parse_error& e) {
        lastError_ = std::string("JSON parse error: ") + e.what();
        return false;
    }

    allFiles_.push_back(path);

    // 兼容两种 JSON 格式：
    //   格式1（API）: { "Shader": { "info": {...}, "renderpass": [...] } }
    //   格式2（直接导出）: { "info": {...}, "renderpass": [...] }
    const json* shaderObjPtr = nullptr;
    if (jsonData.contains("Shader")) {
        shaderObjPtr = &jsonData["Shader"];
    } else if (jsonData.contains("renderpass")) {
        shaderObjPtr = &jsonData;
    } else {
        lastError_ = "JSON missing 'Shader' key or 'renderpass' array";
        return false;
    }
    const auto& shaderObj = *shaderObjPtr;

    // 提取项目名称
    if (shaderObj.contains("info") && shaderObj["info"].contains("name")) {
        data_.projectName = shaderObj["info"]["name"].get<std::string>();
    } else {
        data_.projectName = fs::path(path).stem().string();
    }

    // 解析 renderpass 数组
    if (!shaderObj.contains("renderpass") || !shaderObj["renderpass"].is_array()) {
        lastError_ = "JSON missing 'renderpass' array";
        return false;
    }

    const auto& renderPasses = shaderObj["renderpass"];

    // 临时存储 buffer passes（按索引排列）
    // index 0~3 对应 Buffer A~D
    PassData bufferSlots[4];
    bool bufferUsed[4] = {false, false, false, false};
    bool hasImagePass = false;
    std::string jsonDir = fs::path(path).parent_path().string();

    for (const auto& rp : renderPasses) {
        std::string type = rp.value("type", "");
        std::string code = rp.value("code", "");

        if (type == "common") {
            data_.commonSource = code;
            continue;
        }

        if (type == "image") {
            data_.imagePass.name = "Image";
            data_.imagePass.code = code;
            hasImagePass = true;

            // 解析 inputs
            if (rp.contains("inputs") && rp["inputs"].is_array()) {
                for (const auto& inp : rp["inputs"]) {
                    int channel = inp.value("channel", -1);
                    if (channel < 0 || channel >= 4) continue;

                    std::string ctype = inp.value("ctype", "");
                    auto& binding = data_.imagePass.channels[channel];

                    if (ctype == "buffer") {
                        binding.source = ChannelBinding::Source::Buffer;
                        std::string src = inp.value("src", "");
                        binding.bufferIndex = ParseBufferSrcIndex(src);
                    } else if (ctype == "texture") {
                        binding.source = ChannelBinding::Source::ExternalTexture;
                        std::string src = inp.value("src", "");
                        // 尝试定位本地纹理文件
                        // 优先在 JSON 同目录下查找文件名部分
                        fs::path srcPath(src);
                        std::string filename = srcPath.filename().string();
                        std::string localPath = jsonDir + "/" + filename;
                        if (fs::exists(localPath, std::error_code{})) {
                            binding.texturePath = localPath;
                        } else if (fs::exists(src, std::error_code{})) {
                            binding.texturePath = src;
                        } else {
                            // 尝试 assets/ 下
                            std::string assetsPath = "assets/" + filename;
                            if (fs::exists(assetsPath, std::error_code{})) {
                                binding.texturePath = assetsPath;
                            } else {
                                binding.texturePath = src;  // 保留原始路径
                                std::cerr << "Warning: texture not found locally: " << src << std::endl;
                            }
                        }
                        binding.textureType = ChannelType::Texture2D;
                    } else if (ctype == "cubemap") {
                        binding.source = ChannelBinding::Source::ExternalTexture;
                        std::string src = inp.value("src", "");
                        fs::path srcPath(src);
                        std::string filename = srcPath.filename().string();
                        std::string localPath = jsonDir + "/" + filename;
                        if (fs::exists(localPath, std::error_code{})) {
                            binding.texturePath = localPath;
                        } else {
                            binding.texturePath = src;
                        }
                        binding.textureType = ChannelType::CubeMap;
                    }
                }
            }
            continue;
        }

        if (type == "buffer") {
            std::string name = rp.value("name", "");
            int bufIdx = ParseBufferIndex(name);
            if (bufIdx < 0 || bufIdx >= 4) {
                std::cerr << "Warning: unknown buffer name '" << name << "', skipping" << std::endl;
                continue;
            }

            bufferSlots[bufIdx].name = "Buffer " + std::string(1, 'A' + bufIdx);
            bufferSlots[bufIdx].code = code;
            bufferUsed[bufIdx] = true;

            // 解析 inputs
            if (rp.contains("inputs") && rp["inputs"].is_array()) {
                for (const auto& inp : rp["inputs"]) {
                    int channel = inp.value("channel", -1);
                    if (channel < 0 || channel >= 4) continue;

                    std::string ctype = inp.value("ctype", "");
                    auto& binding = bufferSlots[bufIdx].channels[channel];

                    if (ctype == "buffer") {
                        binding.source = ChannelBinding::Source::Buffer;
                        std::string src = inp.value("src", "");
                        binding.bufferIndex = ParseBufferSrcIndex(src);
                    } else if (ctype == "texture") {
                        binding.source = ChannelBinding::Source::ExternalTexture;
                        std::string src = inp.value("src", "");
                        fs::path srcPath(src);
                        std::string filename = srcPath.filename().string();
                        std::string localPath = jsonDir + "/" + filename;
                        if (fs::exists(localPath, std::error_code{})) {
                            binding.texturePath = localPath;
                        } else if (fs::exists(src, std::error_code{})) {
                            binding.texturePath = src;
                        } else {
                            std::string assetsPath = "assets/" + filename;
                            if (fs::exists(assetsPath, std::error_code{})) {
                                binding.texturePath = assetsPath;
                            } else {
                                binding.texturePath = src;
                                std::cerr << "Warning: texture not found locally: " << src << std::endl;
                            }
                        }
                        binding.textureType = ChannelType::Texture2D;
                    } else if (ctype == "cubemap") {
                        binding.source = ChannelBinding::Source::ExternalTexture;
                        std::string src = inp.value("src", "");
                        fs::path srcPath(src);
                        std::string filename = srcPath.filename().string();
                        std::string localPath = jsonDir + "/" + filename;
                        if (fs::exists(localPath, std::error_code{})) {
                            binding.texturePath = localPath;
                        } else {
                            binding.texturePath = src;
                        }
                        binding.textureType = ChannelType::CubeMap;
                    }
                }
            }
            continue;
        }

        // sound / cubemap pass — 暂不支持
        if (type == "sound" || type == "cubemap") {
            std::cerr << "Warning: pass type '" << type << "' not supported, skipping" << std::endl;
            continue;
        }
    }

    if (!hasImagePass) {
        lastError_ = "JSON has no 'image' renderpass";
        return false;
    }

    // 收集 buffer passes（按索引顺序）
    for (int i = 0; i < 4; ++i) {
        if (bufferUsed[i]) {
            data_.bufferPasses.push_back(bufferSlots[i]);
        }
    }
    data_.isMultiPass = !data_.bufferPasses.empty();

    std::cout << "ShaderProject: loaded JSON '" << path << "' — "
              << data_.bufferPasses.size() << " buffer pass(es)"
              << (data_.commonSource.empty() ? "" : " + common")
              << std::endl;
    return true;
}

// ============================================================
// 目录模式
// ============================================================

bool ShaderProject::LoadFromDirectory(const std::string& dirPath) {
    fs::path dir(dirPath);
    if (!fs::is_directory(dir)) {
        lastError_ = "Not a directory: " + dirPath;
        return false;
    }

    data_.projectName = dir.filename().string();

    // 读取文件辅助函数
    auto readFileContent = [&](const fs::path& filePath) -> std::string {
        std::ifstream f(filePath);
        if (!f.is_open()) return "";
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };

    // image.glsl (必须)
    fs::path imagePath = dir / "image.glsl";
    if (!fs::exists(imagePath)) {
        lastError_ = "Directory mode: missing required 'image.glsl' in " + dirPath;
        return false;
    }
    data_.imagePass.name = "Image";
    data_.imagePass.code = readFileContent(imagePath);
    allFiles_.push_back(imagePath.string());

    // common.glsl (可选)
    fs::path commonPath = dir / "common.glsl";
    if (fs::exists(commonPath)) {
        data_.commonSource = readFileContent(commonPath);
        allFiles_.push_back(commonPath.string());
    }

    // Buffer A~D (可选)
    const char* bufferFileNames[] = {"buf_a.glsl", "buf_b.glsl", "buf_c.glsl", "buf_d.glsl"};
    const char* bufferNames[] = {"Buffer A", "Buffer B", "Buffer C", "Buffer D"};
    for (int i = 0; i < 4; ++i) {
        fs::path bufPath = dir / bufferFileNames[i];
        if (fs::exists(bufPath)) {
            PassData pass;
            pass.name = bufferNames[i];
            pass.code = readFileContent(bufPath);
            data_.bufferPasses.push_back(pass);
            allFiles_.push_back(bufPath.string());
        }
    }

    data_.isMultiPass = !data_.bufferPasses.empty();

    // channels.json (可选，配置各 pass 的 iChannel 绑定)
    fs::path channelsPath = dir / "channels.json";
    if (fs::exists(channelsPath)) {
        allFiles_.push_back(channelsPath.string());

        std::ifstream cf(channelsPath);
        if (cf.is_open()) {
            json channelsJson;
            try {
                cf >> channelsJson;
            } catch (const json::parse_error& e) {
                std::cerr << "Warning: failed to parse channels.json: " << e.what() << std::endl;
                // 继续使用默认绑定
                goto applyDefaults;
            }

            // 解析每个 pass 的通道配置
            auto parsePassChannels = [&](const std::string& passKey, PassData& pass) {
                if (!channelsJson.contains(passKey)) return;
                const auto& passConf = channelsJson[passKey];

                for (int ch = 0; ch < 4; ++ch) {
                    std::string chKey = "iChannel" + std::to_string(ch);
                    if (!passConf.contains(chKey)) continue;

                    std::string value = passConf[chKey].get<std::string>();
                    auto& binding = pass.channels[ch];

                    // 检查是否引用 buffer
                    if (value == "buf_a" || value == "buffer_a" || value == "Buffer A") {
                        binding.source = ChannelBinding::Source::Buffer;
                        binding.bufferIndex = 0;
                    } else if (value == "buf_b" || value == "buffer_b" || value == "Buffer B") {
                        binding.source = ChannelBinding::Source::Buffer;
                        binding.bufferIndex = 1;
                    } else if (value == "buf_c" || value == "buffer_c" || value == "Buffer C") {
                        binding.source = ChannelBinding::Source::Buffer;
                        binding.bufferIndex = 2;
                    } else if (value == "buf_d" || value == "buffer_d" || value == "Buffer D") {
                        binding.source = ChannelBinding::Source::Buffer;
                        binding.bufferIndex = 3;
                    } else {
                        // 视为外部纹理路径
                        binding.source = ChannelBinding::Source::ExternalTexture;
                        // 如果是相对路径，相对于目录解析
                        fs::path texPath(value);
                        if (texPath.is_relative()) {
                            fs::path resolved = dir / texPath;
                            if (fs::exists(resolved)) {
                                binding.texturePath = resolved.string();
                            } else {
                                binding.texturePath = value;  // 保留原始
                            }
                        } else {
                            binding.texturePath = value;
                        }
                        binding.textureType = ChannelType::Texture2D;
                    }
                }
            };

            parsePassChannels("image", data_.imagePass);

            // buffer passes
            const char* bufKeys[] = {"buf_a", "buf_b", "buf_c", "buf_d"};
            for (auto& bp : data_.bufferPasses) {
                int idx = ParseBufferIndex(bp.name);
                if (idx >= 0 && idx < 4) {
                    parsePassChannels(bufKeys[idx], bp);
                }
            }

            std::cout << "ShaderProject: parsed channels.json" << std::endl;
            goto done;
        }
    }

applyDefaults:
    // 默认绑定：如果有 Buffer A，Image 的 iChannel0 绑定 Buffer A
    if (!data_.bufferPasses.empty()) {
        // 找到 Buffer A
        for (size_t i = 0; i < data_.bufferPasses.size(); ++i) {
            if (ParseBufferIndex(data_.bufferPasses[i].name) == 0) {
                data_.imagePass.channels[0].source = ChannelBinding::Source::Buffer;
                data_.imagePass.channels[0].bufferIndex = 0;
                break;
            }
        }
        // Buffer A 默认自引用（iChannel0 = Buffer A）
        for (auto& bp : data_.bufferPasses) {
            int idx = ParseBufferIndex(bp.name);
            if (idx == 0) {
                bp.channels[0].source = ChannelBinding::Source::Buffer;
                bp.channels[0].bufferIndex = 0;
                break;
            }
        }
    }

done:
    std::cout << "ShaderProject: loaded directory '" << dirPath << "' — "
              << data_.bufferPasses.size() << " buffer pass(es)"
              << (data_.commonSource.empty() ? "" : " + common")
              << std::endl;
    return true;
}

// ============================================================
// 工具函数
// ============================================================

int ShaderProject::ParseBufferIndex(const std::string& name) {
    // 从 buffer 名称的最后一个字符解析索引
    // 支持 "Buf A", "Buffer A", "A" 等格式
    if (name.empty()) return -1;
    char lastChar = std::toupper(name.back());
    if (lastChar >= 'A' && lastChar <= 'D') {
        return lastChar - 'A';
    }
    return -1;
}

int ShaderProject::ParseBufferSrcIndex(const std::string& src) {
    // ShaderToy JSON 中 buffer 的 src 字段格式不固定
    // 通常包含 buffer ID，我们需要从中解析出 A/B/C/D 索引
    //
    // 常见模式：
    //   - 直接包含 "Buf A" 这样的名字
    //   - 或者是一个数字 ID（如 "257" 对应 Buf A, "258" 对应 Buf B 等）
    //
    // ShaderToy output ID 对应关系：
    //   257 = Buffer A
    //   258 = Buffer B
    //   259 = Buffer C
    //   260 = Buffer D
    //
    // pygfx/shadertoy 的做法是取 src 倒数第5个字符

    if (src.empty()) return -1;

    // 尝试匹配名称中的 A/B/C/D
    for (int i = static_cast<int>(src.size()) - 1; i >= 0; --i) {
        char c = std::toupper(src[i]);
        if (c >= 'A' && c <= 'D') {
            return c - 'A';
        }
    }

    // 尝试解析为数字 ID
    try {
        int id = std::stoi(src);
        if (id >= 257 && id <= 260) {
            return id - 257;
        }
    } catch (...) {}

    return -1;
}
