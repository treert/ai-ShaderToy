#pragma once

#include <string>
#include <vector>
#include <array>
#include <map>
#include <unordered_map>
#include "shader_manager.h"  // for ChannelType
#include <nlohmann/json_fwd.hpp>

/// 单个 pass 的通道输入配置
struct ChannelBinding {
    enum class Source { None, Buffer, ExternalTexture, Keyboard };
    Source source = Source::None;
    int bufferIndex = -1;           // Buffer A=0, B=1, C=2, D=3
    std::string texturePath;        // 外部纹理文件路径
    ChannelType textureType = ChannelType::Texture2D;
};

/// 单个 pass 的数据
struct PassData {
    std::string name;               // "Image", "Buffer A", "Buffer B", ...
    std::string code;               // ShaderToy GLSL 源码（mainImage 函数）
    std::array<ChannelBinding, 4> channels;  // iChannel0~3 绑定
};

/// 整个 shader 项目的数据
struct ShaderProjectData {
    std::string projectName;
    std::string commonSource;               // Common 段共享 GLSL 代码
    std::vector<PassData> bufferPasses;     // Buffer A~D（按索引排序，最多4个）
    PassData imagePass;                     // Image pass（必须存在）
    bool isMultiPass = false;               // 是否有 buffer pass

    /// 获取所有外部纹理路径（去重）
    std::vector<std::string> GetExternalTexturePaths() const;
};

/// ShaderProject 加载器
/// 统一管理三种 shader 加载模式（单文件 / JSON / 目录）
class ShaderProject {
public:
    /// 加载 shader 项目
    /// @param path 可以是 .glsl 文件、.json 文件或目录路径
    /// @return 成功返回 true
    bool Load(const std::string& path);

    /// 获取解析后的项目数据
    const ShaderProjectData& GetData() const { return data_; }

    /// 获取所有相关文件路径（用于热加载监控）
    std::vector<std::string> GetAllFiles() const { return allFiles_; }

    /// 获取最近的错误信息
    const std::string& GetLastError() const { return lastError_; }

    /// 获取加载的原始路径
    const std::string& GetSourcePath() const { return sourcePath_; }

private:
    /// 单文件模式：.glsl 文件
    bool LoadSingleFile(const std::string& path);

    /// JSON 模式：ShaderToy API 导出的 JSON
    bool LoadFromJSON(const std::string& path);

    /// 目录模式：按约定文件名扫描目录
    bool LoadFromDirectory(const std::string& dirPath);

    /// 从 buffer 名称解析索引 (A=0, B=1, C=2, D=3)
    static int ParseBufferIndex(const std::string& name);

    /// 解析 ShaderToy JSON 中 buffer 引用的 src 字段
    static int ParseBufferSrcIndex(const std::string& src);

    /// 解析单个 input 条目，填充 ChannelBinding
    /// outputIdMap: output id -> buffer index 映射表（用于 ShaderToy 导出格式）
    void ParseInputBinding(const nlohmann::json& inp, ChannelBinding& binding,
                           const std::string& jsonDir,
                           const std::unordered_map<std::string, int>& outputIdMap);

    /// 解析纹理路径：尝试多种本地路径查找策略
    std::string ResolveTexturePath(const std::string& filepath, const std::string& jsonDir);

    ShaderProjectData data_;
    std::vector<std::string> allFiles_;
    std::string sourcePath_;
    std::string lastError_;
};
