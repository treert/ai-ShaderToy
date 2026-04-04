#include "stoy_parser.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_set>
#include <regex>

namespace fs = std::filesystem;

// ============================================================
// StoyFileData
// ============================================================

bool StoyFileData::IsLastPassReferenced() const {
    if (passes.empty()) return false;
    const std::string& lastName = passes.back().name;
    // 扫描所有 pass 的代码，检查是否引用了最后一个 pass 的名字
    for (size_t i = 0; i < passes.size(); ++i) {
        const auto& code = passes[i].code;
        // 简单的正则匹配：pass 名后面跟着 . 或 _（如 Final.Sample 或 Final_Sampler）
        std::regex re("\\b" + lastName + "\\b");
        if (i < passes.size() - 1) {
            // 其他 pass 引用了最后一个 pass
            if (std::regex_search(code, re)) return true;
        } else {
            // 自引用检查：最后一个 pass 引用自己
            if (std::regex_search(code, re)) return true;
        }
    }
    return false;
}

// ============================================================
// 保留字表
// ============================================================

static const std::unordered_set<std::string> kReservedNames = {
    // .stoy 关键字
    "global_setting", "inner_vars", "texture", "common", "pass",
    "init", "code", "float4", "true", "false",
    // 内置变量名
    "iResolution", "iTime", "iTimeDelta", "iFrame", "iFrameRate",
    "iMouse", "iDate", "iSampleRate",
    // HLSL 关键字和内置类型（常见子集）
    "float", "float2", "float3", "int", "int2", "int3", "int4",
    "uint", "uint2", "uint3", "uint4", "bool", "half", "double", "void",
    "return", "if", "else", "for", "while", "do", "switch", "case",
    "break", "continue", "discard", "struct", "cbuffer", "static", "const",
    "inout", "in", "out", "Texture2D", "Texture3D", "TextureCube",
    "SamplerState", "SV_Position", "SV_Target0", "register",
    "main", "mainImage"
};

static const std::unordered_set<std::string> kBuiltinVarNames = {
    "iResolution", "iTime", "iTimeDelta", "iFrame", "iFrameRate",
    "iMouse", "iDate", "iSampleRate"
};

// ============================================================
// 词法分析 — 基础
// ============================================================

void StoyParser::InitLexer(const std::string& source) {
    source_ = source;
    pos_ = 0;
    line_ = 1;
    col_ = 1;
    hasPeeked_ = false;
    hasError_ = false;
    error_.clear();
    data_ = StoyFileData{};
    usedNames_.clear();
}

char StoyParser::CurrentChar() const {
    if (pos_ >= (int)source_.size()) return '\0';
    return source_[pos_];
}

char StoyParser::PeekChar(int offset) const {
    int idx = pos_ + offset;
    if (idx >= (int)source_.size()) return '\0';
    return source_[idx];
}

void StoyParser::Advance() {
    if (pos_ < (int)source_.size()) {
        if (source_[pos_] == '\n') {
            line_++;
            col_ = 1;
        } else {
            col_++;
        }
        pos_++;
    }
}

bool StoyParser::IsAtEnd() const {
    return pos_ >= (int)source_.size();
}

void StoyParser::SkipWhitespaceAndComments() {
    while (!IsAtEnd()) {
        char c = CurrentChar();
        // 空白字符
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            Advance();
            continue;
        }
        // Lua 风格注释: --
        if (c == '-' && PeekChar() == '-') {
            // 跳到行末
            while (!IsAtEnd() && CurrentChar() != '\n') {
                Advance();
            }
            continue;
        }
        break;
    }
}

// ============================================================
// 词法分析 — Token 读取
// ============================================================

StoyParser::Token StoyParser::ReadString() {
    Token tok;
    tok.line = line_;
    tok.col = col_;
    tok.type = Token::Type::StringLiteral;

    Advance(); // skip opening "
    std::string val;
    while (!IsAtEnd() && CurrentChar() != '"') {
        if (CurrentChar() == '\\') {
            Advance();
            if (IsAtEnd()) break;
            char esc = CurrentChar();
            switch (esc) {
                case '"':  val += '"'; break;
                case '\\': val += '\\'; break;
                case 'n':  val += '\n'; break;
                case 't':  val += '\t'; break;
                case 'r':  val += '\r'; break;
                default:   val += '\\'; val += esc; break;
            }
        } else if (CurrentChar() == '\n') {
            // 字符串不允许跨行
            tok.type = Token::Type::Error;
            tok.value = "unterminated string literal (newline in string)";
            return tok;
        } else {
            val += CurrentChar();
        }
        Advance();
    }
    if (IsAtEnd()) {
        tok.type = Token::Type::Error;
        tok.value = "unterminated string literal";
        return tok;
    }
    Advance(); // skip closing "
    tok.value = val;
    return tok;
}

StoyParser::Token StoyParser::ReadLongString() {
    Token tok;
    tok.line = line_;
    tok.col = col_;
    tok.type = Token::Type::LongString;

    Advance(); // skip first [
    // 计算 = 的数量
    int eqCount = 0;
    while (!IsAtEnd() && CurrentChar() == '=') {
        eqCount++;
        Advance();
    }
    if (IsAtEnd() || CurrentChar() != '[') {
        tok.type = Token::Type::Error;
        tok.value = "malformed long string (expected '[' after '=')";
        return tok;
    }
    Advance(); // skip second [

    // 构造结束标记: ]=...=]
    std::string endMark = "]";
    for (int i = 0; i < eqCount; i++) endMark += "=";
    endMark += "]";

    std::string val;
    while (!IsAtEnd()) {
        // 检查是否遇到结束标记
        if (CurrentChar() == ']') {
            bool match = true;
            for (int i = 0; i < (int)endMark.size(); i++) {
                int idx = pos_ + i;
                if (idx >= (int)source_.size() || source_[idx] != endMark[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                // 跳过结束标记
                for (int i = 0; i < (int)endMark.size(); i++) {
                    Advance();
                }
                tok.value = val;
                return tok;
            }
        }
        val += CurrentChar();
        Advance();
    }

    tok.type = Token::Type::Error;
    tok.value = "unterminated long string (expected '" + endMark + "')";
    return tok;
}

StoyParser::Token StoyParser::ReadNumber() {
    Token tok;
    tok.line = line_;
    tok.col = col_;
    tok.type = Token::Type::NumberLiteral;

    std::string val;
    if (CurrentChar() == '-') {
        val += '-';
        Advance();
    }
    while (!IsAtEnd() && std::isdigit((unsigned char)CurrentChar())) {
        val += CurrentChar();
        Advance();
    }
    if (!IsAtEnd() && CurrentChar() == '.') {
        val += '.';
        Advance();
        while (!IsAtEnd() && std::isdigit((unsigned char)CurrentChar())) {
            val += CurrentChar();
            Advance();
        }
    }
    tok.value = val;
    return tok;
}

StoyParser::Token StoyParser::ReadIdentifierOrKeyword() {
    Token tok;
    tok.line = line_;
    tok.col = col_;

    std::string val;
    while (!IsAtEnd() && (std::isalnum((unsigned char)CurrentChar()) || CurrentChar() == '_')) {
        val += CurrentChar();
        Advance();
    }

    // 关键字检测
    if (val == "global_setting") tok.type = Token::Type::KW_global_setting;
    else if (val == "inner_vars") tok.type = Token::Type::KW_inner_vars;
    else if (val == "texture")    tok.type = Token::Type::KW_texture;
    else if (val == "common")     tok.type = Token::Type::KW_common;
    else if (val == "pass")       tok.type = Token::Type::KW_pass;
    else if (val == "init")       tok.type = Token::Type::KW_init;
    else if (val == "code")       tok.type = Token::Type::KW_code;
    else if (val == "float4")     tok.type = Token::Type::KW_float4;
    else if (val == "true")       tok.type = Token::Type::KW_true;
    else if (val == "false")      tok.type = Token::Type::KW_false;
    else                          tok.type = Token::Type::Identifier;

    tok.value = val;
    return tok;
}

StoyParser::Token StoyParser::NextToken() {
    if (hasPeeked_) {
        hasPeeked_ = false;
        return peeked_;
    }

    SkipWhitespaceAndComments();

    if (IsAtEnd()) {
        return Token{Token::Type::Eof, "", line_, col_};
    }

    char c = CurrentChar();
    int tokLine = line_, tokCol = col_;

    // 标点
    switch (c) {
        case '{': Advance(); return Token{Token::Type::LBrace, "{", tokLine, tokCol};
        case '}': Advance(); return Token{Token::Type::RBrace, "}", tokLine, tokCol};
        case '(': Advance(); return Token{Token::Type::LParen, "(", tokLine, tokCol};
        case ')': Advance(); return Token{Token::Type::RParen, ")", tokLine, tokCol};
        case '=': Advance(); return Token{Token::Type::Equals, "=", tokLine, tokCol};
        case ',': Advance(); return Token{Token::Type::Comma,  ",", tokLine, tokCol};
        default: break;
    }

    // 字符串
    if (c == '"') return ReadString();

    // 长字符串 [=[ ... ]=]
    if (c == '[' && (PeekChar() == '=' || PeekChar() == '[')) {
        return ReadLongString();
    }

    // 数字（含负数）
    if (std::isdigit((unsigned char)c) || (c == '-' && std::isdigit((unsigned char)PeekChar()))) {
        return ReadNumber();
    }

    // 标识符 / 关键字
    if (std::isalpha((unsigned char)c) || c == '_') {
        return ReadIdentifierOrKeyword();
    }

    // 未知字符
    Advance();
    return Token{Token::Type::Error, std::string("unexpected character '") + c + "'", tokLine, tokCol};
}

StoyParser::Token StoyParser::PeekToken() {
    if (!hasPeeked_) {
        peeked_ = NextToken();
        hasPeeked_ = true;
    }
    return peeked_;
}

// ============================================================
// 错误报告
// ============================================================

std::string StoyParser::FormatLocation(int line, int col) const {
    return std::to_string(line) + ":" + std::to_string(col);
}

void StoyParser::Error(const std::string& msg) {
    if (!hasError_) {
        error_ = FormatLocation(line_, col_) + ": error: " + msg;
        hasError_ = true;
    }
}

void StoyParser::ErrorAt(const Token& tok, const std::string& msg) {
    if (!hasError_) {
        error_ = FormatLocation(tok.line, tok.col) + ": error: " + msg;
        hasError_ = true;
    }
}

// ============================================================
// 校验
// ============================================================

bool StoyParser::IsReservedName(const std::string& name) const {
    return kReservedNames.count(name) > 0;
}

bool StoyParser::IsNameUsed(const std::string& name) const {
    for (const auto& n : usedNames_) {
        if (n == name) return true;
    }
    return false;
}

bool StoyParser::ValidateInnerVarName(const std::string& name) const {
    return kBuiltinVarNames.count(name) > 0;
}

// ============================================================
// 入口
// ============================================================

bool StoyParser::ParseFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        error_ = "failed to open file: " + path;
        return false;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    InitLexer(source);
    data_.stoyPath = path;
    data_.stoyDir = fs::path(path).parent_path().string();

    return Parse();
}

bool StoyParser::ParseString(const std::string& source, const std::string& sourcePath) {
    InitLexer(source);
    data_.stoyPath = sourcePath;
    if (sourcePath != "<string>") {
        data_.stoyDir = fs::path(sourcePath).parent_path().string();
    }
    return Parse();
}

// ============================================================
// 语法分析 — 主循环
// ============================================================

bool StoyParser::Parse() {
    while (!hasError_) {
        Token tok = PeekToken();
        if (tok.type == Token::Type::Eof) break;
        if (tok.type == Token::Type::Error) {
            ErrorAt(tok, tok.value);
            return false;
        }
        if (!ParseTopLevel()) return false;
    }

    // 验证：至少一个 pass
    if (!hasError_ && data_.passes.empty()) {
        Error("at least one 'pass' block is required");
        return false;
    }

    return !hasError_;
}

bool StoyParser::ParseTopLevel() {
    Token tok = PeekToken();

    switch (tok.type) {
        case Token::Type::KW_global_setting: return ParseGlobalSetting();
        case Token::Type::KW_inner_vars:     return ParseInnerVars();
        case Token::Type::KW_texture:        return ParseTextureDecl();
        case Token::Type::KW_common:         return ParseCommonBlock();
        case Token::Type::KW_pass:           return ParsePassBlock();
        default:
            ErrorAt(tok, "expected top-level keyword (global_setting, inner_vars, texture, common, pass), got '" + tok.value + "'");
            return false;
    }
}

// ============================================================
// 语法分析 — global_setting
// ============================================================

bool StoyParser::ParseGlobalSetting() {
    Token kw = NextToken(); // consume 'global_setting'
    if (data_.hasGlobalSetting) {
        ErrorAt(kw, "duplicate 'global_setting' block (at most one allowed)");
        return false;
    }
    data_.hasGlobalSetting = true;

    Token lb = NextToken();
    if (lb.type != Token::Type::LBrace) {
        ErrorAt(lb, "expected '{' after 'global_setting'");
        return false;
    }

    while (!hasError_) {
        Token peek = PeekToken();
        if (peek.type == Token::Type::RBrace) { NextToken(); break; }
        if (peek.type == Token::Type::Eof) { ErrorAt(peek, "unexpected end of file in global_setting block"); return false; }

        // key = value
        Token key = NextToken();
        if (key.type != Token::Type::Identifier) {
            ErrorAt(key, "expected setting key, got '" + key.value + "'");
            return false;
        }
        Token eq = NextToken();
        if (eq.type != Token::Type::Equals) {
            ErrorAt(eq, "expected '=' after setting key '" + key.value + "'");
            return false;
        }
        Token val = NextToken();

        if (key.value == "format") {
            if (val.type != Token::Type::StringLiteral) { ErrorAt(val, "'format' expects a string value"); return false; }
            data_.globalSetting.format = val.value;
        } else if (key.value == "msaa") {
            if (val.type != Token::Type::NumberLiteral) { ErrorAt(val, "'msaa' expects a number value"); return false; }
            data_.globalSetting.msaa = std::atoi(val.value.c_str());
        } else if (key.value == "vsync") {
            if (val.type == Token::Type::KW_true) data_.globalSetting.vsync = true;
            else if (val.type == Token::Type::KW_false) data_.globalSetting.vsync = false;
            else { ErrorAt(val, "'vsync' expects true or false"); return false; }
        } else if (key.value == "resolution_scale") {
            if (val.type != Token::Type::NumberLiteral) { ErrorAt(val, "'resolution_scale' expects a number value"); return false; }
            data_.globalSetting.resolutionScale = (float)std::atof(val.value.c_str());
        } else {
            ErrorAt(key, "unknown global_setting key '" + key.value + "'");
            return false;
        }
    }
    return !hasError_;
}

// ============================================================
// 语法分析 — inner_vars
// ============================================================

bool StoyParser::ParseInnerVars() {
    Token kw = NextToken(); // consume 'inner_vars'
    if (data_.hasInnerVars) {
        ErrorAt(kw, "duplicate 'inner_vars' block (at most one allowed)");
        return false;
    }
    data_.hasInnerVars = true;

    Token lb = NextToken();
    if (lb.type != Token::Type::LBrace) {
        ErrorAt(lb, "expected '{' after 'inner_vars'");
        return false;
    }

    while (!hasError_) {
        Token peek = PeekToken();
        if (peek.type == Token::Type::RBrace) { NextToken(); break; }
        if (peek.type == Token::Type::Eof) { ErrorAt(peek, "unexpected end of file in inner_vars block"); return false; }

        Token varName = NextToken();
        if (varName.type != Token::Type::Identifier) {
            ErrorAt(varName, "expected built-in variable name, got '" + varName.value + "'");
            return false;
        }
        if (!ValidateInnerVarName(varName.value)) {
            ErrorAt(varName, "'" + varName.value + "' is not a valid built-in variable name");
            return false;
        }
        // 重复检查
        for (const auto& v : data_.innerVars) {
            if (v == varName.value) {
                ErrorAt(varName, "duplicate inner_var '" + varName.value + "'");
                return false;
            }
        }
        data_.innerVars.push_back(varName.value);
    }
    return !hasError_;
}

// ============================================================
// 语法分析 — texture
// ============================================================

bool StoyParser::ParseTextureDecl() {
    Token kw = NextToken(); // consume 'texture'

    // texture Name = "path" { ... }
    Token nameTok = NextToken();
    if (nameTok.type != Token::Type::Identifier) {
        ErrorAt(nameTok, "expected texture name (identifier), got '" + nameTok.value + "'");
        return false;
    }
    if (IsReservedName(nameTok.value)) {
        ErrorAt(nameTok, "'" + nameTok.value + "' is a reserved name and cannot be used as a texture name");
        return false;
    }
    if (IsNameUsed(nameTok.value)) {
        ErrorAt(nameTok, "name '" + nameTok.value + "' is already used by another texture or pass");
        return false;
    }

    Token eq = NextToken();
    if (eq.type != Token::Type::Equals) {
        ErrorAt(eq, "expected '=' after texture name");
        return false;
    }

    Token pathTok = NextToken();
    if (pathTok.type != Token::Type::StringLiteral) {
        ErrorAt(pathTok, "expected string literal for texture path");
        return false;
    }

    StoyTextureDecl tex;
    tex.name = nameTok.value;
    tex.path = pathTok.value;
    tex.declOrder = (int)data_.textures.size();

    // 可选的 { filter = ..., wrap = ... }
    Token peek = PeekToken();
    if (peek.type == Token::Type::LBrace) {
        NextToken(); // consume {
        while (!hasError_) {
            peek = PeekToken();
            if (peek.type == Token::Type::RBrace) { NextToken(); break; }
            if (peek.type == Token::Type::Eof) { ErrorAt(peek, "unexpected end of file in texture config"); return false; }

            Token key = NextToken();
            if (key.type != Token::Type::Identifier) {
                ErrorAt(key, "expected texture config key, got '" + key.value + "'");
                return false;
            }
            Token eqTok = NextToken();
            if (eqTok.type != Token::Type::Equals) {
                ErrorAt(eqTok, "expected '=' after texture config key");
                return false;
            }
            Token val = NextToken();
            if (val.type != Token::Type::StringLiteral) {
                ErrorAt(val, "expected string value for texture config key '" + key.value + "'");
                return false;
            }

            if (key.value == "filter") {
                if (val.value != "linear" && val.value != "point") {
                    ErrorAt(val, "filter must be \"linear\" or \"point\"");
                    return false;
                }
                tex.filter = val.value;
            } else if (key.value == "wrap") {
                if (val.value != "repeat" && val.value != "clamp" && val.value != "mirror") {
                    ErrorAt(val, "wrap must be \"repeat\", \"clamp\", or \"mirror\"");
                    return false;
                }
                tex.wrap = val.value;
            } else {
                ErrorAt(key, "unknown texture config key '" + key.value + "'");
                return false;
            }
        }
    }

    usedNames_.push_back(tex.name);
    data_.textures.push_back(tex);
    return !hasError_;
}

// ============================================================
// 语法分析 — common
// ============================================================

bool StoyParser::ParseCommonBlock() {
    Token kw = NextToken(); // consume 'common'
    if (data_.hasCommon) {
        ErrorAt(kw, "duplicate 'common' block (at most one allowed)");
        return false;
    }
    data_.hasCommon = true;

    Token ls = NextToken();
    if (ls.type != Token::Type::LongString) {
        ErrorAt(ls, "expected long string [=[ ... ]=] after 'common'");
        return false;
    }
    data_.commonCode = ls.value;
    return true;
}

// ============================================================
// 语法分析 — pass
// ============================================================

bool StoyParser::ParsePassBlock() {
    Token kw = NextToken(); // consume 'pass'

    Token nameTok = NextToken();
    if (nameTok.type != Token::Type::Identifier) {
        ErrorAt(nameTok, "expected pass name (identifier), got '" + nameTok.value + "'");
        return false;
    }
    if (IsReservedName(nameTok.value)) {
        ErrorAt(nameTok, "'" + nameTok.value + "' is a reserved name and cannot be used as a pass name");
        return false;
    }
    if (IsNameUsed(nameTok.value)) {
        ErrorAt(nameTok, "name '" + nameTok.value + "' is already used by another texture or pass");
        return false;
    }

    Token lb = NextToken();
    if (lb.type != Token::Type::LBrace) {
        ErrorAt(lb, "expected '{' after pass name");
        return false;
    }

    StoyPassData pass;
    pass.name = nameTok.value;
    pass.declOrder = (int)data_.passes.size();
    bool hasCode = false;

    while (!hasError_) {
        Token peek = PeekToken();
        if (peek.type == Token::Type::RBrace) { NextToken(); break; }
        if (peek.type == Token::Type::Eof) { ErrorAt(peek, "unexpected end of file in pass block"); return false; }

        if (peek.type == Token::Type::KW_init) {
            NextToken(); // consume 'init'
            Token eqTok = NextToken();
            if (eqTok.type != Token::Type::Equals) {
                ErrorAt(eqTok, "expected '=' after 'init'");
                return false;
            }
            if (!ParsePassInit(pass.init)) return false;
        } else if (peek.type == Token::Type::KW_code) {
            NextToken(); // consume 'code'
            Token ls = NextToken();
            if (ls.type != Token::Type::LongString) {
                ErrorAt(ls, "expected long string [=[ ... ]=] after 'code'");
                return false;
            }
            pass.code = ls.value;
            hasCode = true;
        } else {
            ErrorAt(peek, "expected 'init' or 'code' in pass block, got '" + peek.value + "'");
            return false;
        }
    }

    if (!hasCode && !hasError_) {
        Error("pass '" + pass.name + "' is missing required 'code' block");
        return false;
    }

    usedNames_.push_back(pass.name);
    data_.passes.push_back(pass);
    return !hasError_;
}

// ============================================================
// 语法分析 — init 值
// ============================================================

bool StoyParser::ParsePassInit(StoyPassInit& init) {
    Token peek = PeekToken();

    // init = float4(r, g, b, a)
    if (peek.type == Token::Type::KW_float4) {
        NextToken(); // consume 'float4'
        init.type = StoyPassInit::Type::Color;
        return ParseFloat4(init.color);
    }

    // init = texture "path"
    if (peek.type == Token::Type::KW_texture) {
        NextToken(); // consume 'texture'
        Token pathTok = NextToken();
        if (pathTok.type != Token::Type::StringLiteral) {
            ErrorAt(pathTok, "expected string literal for init texture path");
            return false;
        }
        init.type = StoyPassInit::Type::Texture;
        init.texturePath = pathTok.value;
        return true;
    }

    ErrorAt(peek, "expected 'float4(...)' or 'texture \"...\"' for init value");
    return false;
}

bool StoyParser::ParseFloat4(float out[4]) {
    Token lp = NextToken();
    if (lp.type != Token::Type::LParen) {
        ErrorAt(lp, "expected '(' after 'float4'");
        return false;
    }

    for (int i = 0; i < 4; i++) {
        Token num = NextToken();
        if (num.type != Token::Type::NumberLiteral) {
            ErrorAt(num, "expected number in float4 component " + std::to_string(i));
            return false;
        }
        out[i] = (float)std::atof(num.value.c_str());

        if (i < 3) {
            Token comma = NextToken();
            if (comma.type != Token::Type::Comma) {
                ErrorAt(comma, "expected ',' between float4 components");
                return false;
            }
        }
    }

    Token rp = NextToken();
    if (rp.type != Token::Type::RParen) {
        ErrorAt(rp, "expected ')' after float4 components");
        return false;
    }
    return true;
}
