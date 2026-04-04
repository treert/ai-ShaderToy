#pragma once

#include <string>
#include <vector>

// ============================================================
// .stoy File Parser
// 解析 .stoy 自定义 shader 描述格式，输出 StoyFileData 中间表示。
// 语法参考：Docs/stoy_grammar.bnf
// ============================================================

/// 全局渲染设置
struct StoyGlobalSetting {
    std::string format = "R8G8B8A8_UNORM";
    int msaa = 1;
    bool vsync = true;
    float resolutionScale = 1.0f;
};

/// 外部纹理声明
struct StoyTextureDecl {
    std::string name;               // 纹理变量名（合法标识符）
    std::string path;               // 文件路径（相对于 .stoy 文件所在目录）
    std::string filter = "linear";  // "linear" | "point"
    std::string wrap = "clamp";     // "repeat" | "clamp" | "mirror"
    int declOrder = 0;              // 声明顺序（用于 register 槽位分配）
};

/// Pass 初始值
struct StoyPassInit {
    enum class Type { None, Color, Texture };
    Type type = Type::None;
    float color[4] = {0, 0, 0, 0};
    std::string texturePath;        // init = texture "path"
};

/// Pass 数据
struct StoyPassData {
    std::string name;               // pass 名称（合法标识符）
    std::string code;               // HLSL 用户代码（mainImage 函数体）
    StoyPassInit init;
    int declOrder = 0;              // 声明顺序
};

/// .stoy 文件完整解析结果
struct StoyFileData {
    StoyGlobalSetting globalSetting;
    bool hasGlobalSetting = false;

    std::vector<std::string> innerVars;     // 声明的内置变量名列表
    bool hasInnerVars = false;

    std::vector<StoyTextureDecl> textures;  // 外部纹理声明（有序）
    std::string commonCode;                 // common 块 HLSL 代码
    bool hasCommon = false;

    std::vector<StoyPassData> passes;       // pass 列表（有序，最后一个为 Image 输出）

    std::string stoyDir;                    // .stoy 文件所在目录（用于路径解析）
    std::string stoyPath;                   // .stoy 文件完整路径

    /// 检查最后一个 pass 是否被其他 pass 引用（决定是否需要双缓冲）
    bool IsLastPassReferenced() const;
};

/// .stoy 文件解析器
/// 手写递归下降 parser，提供行号+列号的精确错误报告。
class StoyParser {
public:
    /// 从文件路径解析 .stoy 文件
    /// @param path .stoy 文件路径
    /// @return 成功返回 true
    bool ParseFile(const std::string& path);

    /// 从字符串内容解析（测试用）
    /// @param source .stoy 文件内容
    /// @param sourcePath 虚拟路径（用于错误消息和 stoyDir 推断）
    /// @return 成功返回 true
    bool ParseString(const std::string& source, const std::string& sourcePath = "<string>");

    /// 获取解析结果
    const StoyFileData& GetData() const { return data_; }

    /// 获取错误信息
    const std::string& GetError() const { return error_; }

private:
    // ---- 词法分析 ----

    struct Token {
        enum class Type {
            // 标点
            LBrace, RBrace, LParen, RParen, Equals, Comma,
            // 字面量
            Identifier, StringLiteral, NumberLiteral, LongString,
            // 关键字
            KW_global_setting, KW_inner_vars, KW_texture, KW_common, KW_pass,
            KW_init, KW_code, KW_float4, KW_true, KW_false,
            // 特殊
            Eof, Error
        };

        Type type = Type::Eof;
        std::string value;          // token 原始文本或解析后的值
        int line = 0;
        int col = 0;
    };

    void InitLexer(const std::string& source);
    Token NextToken();
    Token PeekToken();
    void SkipWhitespaceAndComments();
    Token ReadString();
    Token ReadLongString();
    Token ReadNumber();
    Token ReadIdentifierOrKeyword();
    char CurrentChar() const;
    char PeekChar(int offset = 1) const;
    void Advance();
    bool IsAtEnd() const;

    // ---- 语法分析 ----

    bool Parse();
    bool ParseTopLevel();
    bool ParseGlobalSetting();
    bool ParseInnerVars();
    bool ParseTextureDecl();
    bool ParseCommonBlock();
    bool ParsePassBlock();
    bool ParsePassInit(StoyPassInit& init);
    bool ParseFloat4(float out[4]);

    // ---- 校验 ----

    bool IsReservedName(const std::string& name) const;
    bool IsNameUsed(const std::string& name) const;
    bool ValidateInnerVarName(const std::string& name) const;

    // ---- 错误报告 ----

    void Error(const std::string& msg);
    void ErrorAt(const Token& tok, const std::string& msg);
    std::string FormatLocation(int line, int col) const;

    // ---- 状态 ----

    std::string source_;
    int pos_ = 0;
    int line_ = 1;
    int col_ = 1;

    Token peeked_;
    bool hasPeeked_ = false;

    StoyFileData data_;
    std::string error_;
    bool hasError_ = false;

    // 已使用的名称集合（texture 名 + pass 名，全局唯一）
    std::vector<std::string> usedNames_;
};
