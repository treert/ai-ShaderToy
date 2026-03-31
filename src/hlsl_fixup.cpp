#include "hlsl_fixup.h"
#include <regex>
#include <sstream>

// ============================================================
// HlslFixupPipeline
// ============================================================

void HlslFixupPipeline::addPass(std::unique_ptr<HlslFixupPass> pass) {
    passes_.push_back(std::move(pass));
}

std::string HlslFixupPipeline::run(std::string hlsl, const HlslFixupContext& ctx) const {
    for (auto& pass : passes_) {
        if (pass->shouldApply(hlsl, ctx)) {
            hlsl = pass->apply(std::move(hlsl), ctx);
        }
    }
    return hlsl;
}

// ============================================================
// 1. UboPrefixFixup — 删除 "_stU_" 前缀
// ============================================================

bool UboPrefixFixup::shouldApply(const std::string& hlsl, const HlslFixupContext&) const {
    return hlsl.find("_stU_") != std::string::npos;
}

std::string UboPrefixFixup::apply(std::string hlsl, const HlslFixupContext&) const {
    const std::string prefix = "_stU_";
    size_t pos = 0;
    while ((pos = hlsl.find(prefix, pos)) != std::string::npos) {
        hlsl.erase(pos, prefix.size());
    }
    return hlsl;
}

// ============================================================
// 2. FragCoordYFlipFixup — Image pass Y 翻转
// ============================================================

bool FragCoordYFlipFixup::shouldApply(const std::string& hlsl, const HlslFixupContext& ctx) const {
    return ctx.flipFragCoordY && hlsl.find("gl_FragCoord.w = 1.0 / gl_FragCoord.w;") != std::string::npos;
}

std::string FragCoordYFlipFixup::apply(std::string hlsl, const HlslFixupContext&) const {
    // D3D11 SV_Position.y 是 top-down，OpenGL gl_FragCoord.y 是 bottom-up
    // 在 gl_FragCoord.w 赋值之后插入 Y 翻转
    // 注意：全局 iResolution 在 frag_main() 中才赋值，此处必须用 cbuffer 成员 iResolution4
    const std::string wLine = "gl_FragCoord.w = 1.0 / gl_FragCoord.w;";
    size_t wPos = hlsl.find(wLine);
    if (wPos == std::string::npos) return hlsl;

    // 查找 cbuffer 中实际的 iResolution4 成员名（前缀可能已被 UboPrefixFixup 清理）
    std::string resName = "iResolution4";
    std::regex resRe(R"((\w*iResolution4)\s*:\s*packoffset)");
    std::smatch resMatch;
    if (std::regex_search(hlsl, resMatch, resRe)) {
        resName = resMatch[1].str();
    }

    size_t insertPos = wPos + wLine.size();
    hlsl.insert(insertPos, "\n    gl_FragCoord.y = " + resName + ".y - gl_FragCoord.y;");
    return hlsl;
}

// ============================================================
// 3. FallbackReturnFixup — 非 void 函数兜底 return
// ============================================================

bool FallbackReturnFixup::shouldApply(const std::string&, const HlslFixupContext&) const {
    return true;  // 总是应用（开销很低，逐行扫描）
}

std::string FallbackReturnFixup::apply(std::string hlsl, const HlslFixupContext&) const {
    // SPIRV-Cross 生成的函数格式规律：顶格 "type name(params)\n{"，末尾顶格 "}"
    static const std::regex funcDefRe(
        R"(^(float[2-4]?|int[2-4]?|uint[2-4]?|bool|half[2-4]?)\s+(\w+)\s*\([^)]*\)\s*$)");

    std::string result;
    result.reserve(hlsl.size() + 256);
    std::istringstream stream(hlsl);
    std::string line;
    std::string pendingReturnType;
    int braceDepth = 0;
    bool inNonVoidFunc = false;

    while (std::getline(stream, line)) {
        std::smatch m;
        if (std::regex_match(line, m, funcDefRe)) {
            pendingReturnType = m[1].str();
            inNonVoidFunc = false;
            braceDepth = 0;
        }

        for (char c : line) {
            if (c == '{') {
                if (!pendingReturnType.empty() && braceDepth == 0) {
                    inNonVoidFunc = true;
                }
                braceDepth++;
            } else if (c == '}') {
                braceDepth--;
                if (inNonVoidFunc && braceDepth == 0) {
                    std::string defaultVal = "0.0f";
                    if (pendingReturnType.find("2") != std::string::npos) defaultVal = "0.0f.xx";
                    else if (pendingReturnType.find("3") != std::string::npos) defaultVal = "0.0f.xxx";
                    else if (pendingReturnType.find("4") != std::string::npos) defaultVal = "0.0f.xxxx";
                    result += "    return " + defaultVal + ";\n";
                    inNonVoidFunc = false;
                    pendingReturnType.clear();
                }
            }
        }

        result += line + "\n";
    }
    return result;
}

// ============================================================
// 4. LoopAttributeFixup — [loop] 属性
// ============================================================

bool LoopAttributeFixup::shouldApply(const std::string& hlsl, const HlslFixupContext&) const {
    return hlsl.find("for (") != std::string::npos ||
           hlsl.find("for(")  != std::string::npos ||
           hlsl.find("while (") != std::string::npos ||
           hlsl.find("while(")  != std::string::npos;
}

std::string LoopAttributeFixup::apply(std::string hlsl, const HlslFixupContext&) const {
    static const std::regex loopRe(R"(^(\s+)(for|while)\s*\()");
    std::string result;
    result.reserve(hlsl.size() + 512);
    std::istringstream stream(hlsl);
    std::string line;
    while (std::getline(stream, line)) {
        std::smatch m;
        if (std::regex_search(line, m, loopRe)) {
            result += m[1].str() + "[loop]\n";
        }
        result += line + "\n";
    }
    return result;
}

// ============================================================
// 默认管线构建
// ============================================================

HlslFixupPipeline CreateDefaultFixupPipeline() {
    HlslFixupPipeline pipeline;
    // 顺序重要：UBO 前缀清理必须在 Y 翻转之前（Y 翻转需要找到干净的成员名）
    pipeline.addPass(std::make_unique<UboPrefixFixup>());
    pipeline.addPass(std::make_unique<FragCoordYFlipFixup>());
    pipeline.addPass(std::make_unique<FallbackReturnFixup>());
    pipeline.addPass(std::make_unique<LoopAttributeFixup>());
    return pipeline;
}
