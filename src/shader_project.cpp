#include "shader_project.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <regex>

namespace fs = std::filesystem;
using json = nlohmann::json;

// Cube A pass 在 outputIdMap 中使用的特殊索引（区别于 Buffer 0~3）
static constexpr int CUBEMAP_PASS_INDEX = 10;

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
    if (hasCubeMapPass) {
        collectFromPass(cubeMapPass);
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

    // ---- 第一遍：扫描 outputs，建立 output id → buffer index 映射 ----
    // ShaderToy 导出的 JSON 中，每个 buffer pass 有 outputs[0].id，
    // 其他 pass 的 inputs 通过相同的 id 引用此 buffer。
    // Cube A pass 使用特殊索引 CUBEMAP_PASS_INDEX 表示。
    std::unordered_map<std::string, int> outputIdMap;
    {
        // 按出现顺序为 buffer 分配索引
        int bufCount = 0;
        for (const auto& rp : renderPasses) {
            std::string rpType = rp.value("type", "");
            if (rpType == "buffer") {
                // 从 name 字段解析 buffer 索引（如 "Buffer A" -> 0）
                std::string name = rp.value("name", "");
                int bufIdx = ParseBufferIndex(name);
                if (bufIdx < 0) {
                    // name 解析失败，按出现顺序分配
                    bufIdx = bufCount;
                }
                bufCount++;

                // 记录 output id -> buffer index
                if (rp.contains("outputs") && rp["outputs"].is_array()) {
                    for (const auto& out : rp["outputs"]) {
                        std::string outId = out.value("id", "");
                        if (!outId.empty()) {
                            outputIdMap[outId] = bufIdx;
                        }
                    }
                }
            } else if (rpType == "cubemap") {
                // Cube A pass: 记录 output id -> CUBEMAP_PASS_INDEX
                if (rp.contains("outputs") && rp["outputs"].is_array()) {
                    for (const auto& out : rp["outputs"]) {
                        std::string outId = out.value("id", "");
                        if (!outId.empty()) {
                            outputIdMap[outId] = CUBEMAP_PASS_INDEX;
                        }
                    }
                }
            }
        }
    }

    // ---- 第二遍：解析各 renderpass ----
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
                    ParseInputBinding(inp, data_.imagePass.channels[channel],
                                      jsonDir, outputIdMap);
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
                    ParseInputBinding(inp, bufferSlots[bufIdx].channels[channel],
                                      jsonDir, outputIdMap);
                }
            }
            continue;
        }

        // cubemap pass — Cube A
        if (type == "cubemap") {
            data_.cubeMapPass.name = "Cube A";
            data_.cubeMapPass.code = code;
            data_.hasCubeMapPass = true;

            // 解析 inputs
            if (rp.contains("inputs") && rp["inputs"].is_array()) {
                for (const auto& inp : rp["inputs"]) {
                    int channel = inp.value("channel", -1);
                    if (channel < 0 || channel >= 4) continue;
                    ParseInputBinding(inp, data_.cubeMapPass.channels[channel],
                                      jsonDir, outputIdMap);
                }
            }
            continue;
        }

        // sound pass — 暂不支持
        if (type == "sound") {
            std::cerr << "Warning: pass type 'sound' not supported, skipping" << std::endl;
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
    data_.isMultiPass = !data_.bufferPasses.empty() || data_.hasCubeMapPass;

    std::cout << "ShaderProject: loaded JSON '" << path << "' — "
              << data_.bufferPasses.size() << " buffer pass(es)"
              << (data_.hasCubeMapPass ? " + Cube A" : "")
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

    // cube_a.glsl (可选, CubeMap pass)
    fs::path cubeAPath = dir / "cube_a.glsl";
    if (fs::exists(cubeAPath)) {
        data_.cubeMapPass.name = "Cube A";
        data_.cubeMapPass.code = readFileContent(cubeAPath);
        data_.hasCubeMapPass = true;
        data_.isMultiPass = true;
        allFiles_.push_back(cubeAPath.string());
    }

    // channels.json (可选，配置各 pass 的 iChannel 绑定)
    bool channelsParsed = false;
    fs::path channelsPath = dir / "channels.json";
    if (fs::exists(channelsPath)) {
        allFiles_.push_back(channelsPath.string());

        std::ifstream cf(channelsPath);
        if (cf.is_open()) {
            json channelsJson;
            bool parseOk = true;
            try {
                cf >> channelsJson;
            } catch (const json::parse_error& e) {
                std::cerr << "Warning: failed to parse channels.json: " << e.what() << std::endl;
                parseOk = false;
            }

            if (parseOk) {
                // 解析每个 pass 的通道配置
                auto parsePassChannels = [&](const std::string& passKey, PassData& pass) {
                    if (!channelsJson.contains(passKey)) return;
                    const auto& passConf = channelsJson[passKey];

                    for (int ch = 0; ch < 4; ++ch) {
                        std::string chKey = "iChannel" + std::to_string(ch);
                        if (!passConf.contains(chKey)) continue;

                        const auto& chVal = passConf[chKey];
                        auto& binding = pass.channels[ch];

                        // 支持两种格式：
                        //   字符串: "buf_a" 或 "/media/a/xxx.jpg"
                        //   对象:   {"path": "/media/a/xxx.png", "type": "cubemap"}
                        std::string value;
                        std::string texType;
                        if (chVal.is_object()) {
                            value = chVal.value("path", "");
                            texType = chVal.value("type", "");
                        } else {
                            value = chVal.get<std::string>();
                        }

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
                        } else if (value == "cube_a" || value == "Cube A") {
                            binding.source = ChannelBinding::Source::CubeMapPass;
                            binding.textureType = ChannelType::CubeMap;
                        } else {
                            // 视为外部纹理路径，使用统一路径解析
                            binding.source = ChannelBinding::Source::ExternalTexture;
                            binding.texturePath = ResolveTexturePath(value, dirPath);
                            // 根据 type 字段判断纹理类型
                            binding.textureType = (texType == "cubemap")
                                ? ChannelType::CubeMap : ChannelType::Texture2D;
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

                // cube_a pass
                if (data_.hasCubeMapPass) {
                    parsePassChannels("cube_a", data_.cubeMapPass);
                }

                std::cout << "ShaderProject: parsed channels.json" << std::endl;
                channelsParsed = true;
            }
        }
    }

    // 默认绑定（channels.json 不存在或解析失败时使用）
    if (!channelsParsed && !data_.bufferPasses.empty()) {
        // 找到 Buffer A，Image 的 iChannel0 绑定 Buffer A
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

    std::cout << "ShaderProject: loaded directory '" << dirPath << "' — "
              << data_.bufferPasses.size() << " buffer pass(es)"
              << (data_.hasCubeMapPass ? " + Cube A" : "")
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

// ============================================================
// ParseInputBinding — 解析单个 input 条目
// ============================================================

void ShaderProject::ParseInputBinding(const json& inp, ChannelBinding& binding,
                                       const std::string& jsonDir,
                                       const std::unordered_map<std::string, int>& outputIdMap) {
    // 兼容两套字段名：
    //   ShaderToy 网站导出格式: "type", "filepath", "id"
    //   自定义/文档格式:        "ctype", "src"
    std::string inputType;
    if (inp.contains("type") && inp["type"].is_string()) {
        inputType = inp["type"].get<std::string>();
    }
    if (inputType.empty()) {
        inputType = inp.value("ctype", "");
    }

    std::string inputPath;
    if (inp.contains("filepath") && inp["filepath"].is_string()) {
        inputPath = inp["filepath"].get<std::string>();
    }
    if (inputPath.empty()) {
        inputPath = inp.value("src", "");
    }

    std::string inputId = inp.value("id", "");

    // ---- buffer 类型 ----
    if (inputType == "buffer") {
        // 优先通过 output id 映射表查找 buffer 索引
        auto it = outputIdMap.find(inputId);
        if (it != outputIdMap.end()) {
            if (it->second == CUBEMAP_PASS_INDEX) {
                // 引用的是 Cube A pass 的输出（samplerCube）
                binding.source = ChannelBinding::Source::CubeMapPass;
                binding.textureType = ChannelType::CubeMap;
            } else {
                binding.source = ChannelBinding::Source::Buffer;
                binding.bufferIndex = it->second;
            }
        } else {
            binding.source = ChannelBinding::Source::Buffer;
            // fallback: 从 filepath 的 bufferNN 中提取索引
            // 例如 "/media/previz/buffer00.png" -> 0
            std::regex bufRe(R"(buffer0*(\d+))");
            std::smatch match;
            if (std::regex_search(inputPath, match, bufRe)) {
                binding.bufferIndex = std::stoi(match[1].str());
            } else {
                // 再 fallback 到旧逻辑
                binding.bufferIndex = ParseBufferSrcIndex(inputPath.empty() ? inputId : inputPath);
            }
        }
        return;
    }

    // ---- texture 类型 ----
    if (inputType == "texture") {
        binding.source = ChannelBinding::Source::ExternalTexture;
        binding.texturePath = ResolveTexturePath(inputPath, jsonDir);
        binding.textureType = ChannelType::Texture2D;
        return;
    }

    // ---- cubemap 类型 ----
    if (inputType == "cubemap") {
        binding.source = ChannelBinding::Source::ExternalTexture;
        binding.texturePath = ResolveTexturePath(inputPath, jsonDir);
        binding.textureType = ChannelType::CubeMap;
        return;
    }

    // ---- keyboard 类型 ----
    if (inputType == "keyboard") {
        binding.source = ChannelBinding::Source::Keyboard;
        std::cout << "Note: keyboard input on channel (ignored, not yet supported)" << std::endl;
        return;
    }

    // ---- 未知类型 ----
    if (!inputType.empty()) {
        std::cerr << "Warning: unknown input type '" << inputType << "', skipping" << std::endl;
    }
}

// ============================================================
// ResolveTexturePath — 纹理路径查找
// ============================================================

std::string ShaderProject::ResolveTexturePath(const std::string& filepath,
                                               const std::string& jsonDir) {
    if (filepath.empty()) return filepath;

    fs::path srcPath(filepath);
    std::string filename = srcPath.filename().string();

    // 1. JSON 同目录 + 文件名
    {
        std::string localPath = jsonDir + "/" + filename;
        if (fs::exists(localPath, std::error_code{})) {
            return localPath;
        }
    }

    // 2. assets/ + filepath（去掉前导斜杠），支持 /media/a/xxx.jpg -> assets/media/a/xxx.jpg
    {
        std::string relPath = filepath;
        // 去掉前导斜杠
        while (!relPath.empty() && (relPath[0] == '/' || relPath[0] == '\\')) {
            relPath = relPath.substr(1);
        }
        std::string assetsFullPath = "assets/" + relPath;
        if (fs::exists(assetsFullPath, std::error_code{})) {
            return assetsFullPath;
        }
    }

    // 3. assets/ + 文件名
    {
        std::string assetsPath = "assets/" + filename;
        if (fs::exists(assetsPath, std::error_code{})) {
            return assetsPath;
        }
    }

    // 4. 原始路径直接尝试
    if (fs::exists(filepath, std::error_code{})) {
        return filepath;
    }

    // 5. 保留原始路径 + warning
    std::cerr << "Warning: texture not found locally: " << filepath << std::endl;
    return filepath;
}
