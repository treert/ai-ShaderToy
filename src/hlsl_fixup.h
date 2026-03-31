#pragma once

#include <string>
#include <vector>
#include <memory>

// ============================================================
// HLSL Fixup Pipeline
// 将 SPIRV-Cross 输出的 HLSL 按需修复为可在 D3D11 上编译运行的代码。
// 每个修复封装为独立的 HlslFixupPass，由 HlslFixupPipeline 按顺序执行。
// ============================================================

/// 翻译上下文（传递给每个 fixup pass 的参数）
struct HlslFixupContext {
    bool flipFragCoordY = true;   // Image pass = true, Buffer/CubeMap pass = false
};

/// Fixup pass 基类
struct HlslFixupPass {
    virtual ~HlslFixupPass() = default;
    virtual const char* name() const = 0;
    virtual bool shouldApply(const std::string& hlsl, const HlslFixupContext& ctx) const = 0;
    virtual std::string apply(std::string hlsl, const HlslFixupContext& ctx) const = 0;
};

/// Fixup 管线：按注册顺序依次执行各 pass
class HlslFixupPipeline {
public:
    void addPass(std::unique_ptr<HlslFixupPass> pass);
    std::string run(std::string hlsl, const HlslFixupContext& ctx) const;

private:
    std::vector<std::unique_ptr<HlslFixupPass>> passes_;
};

// ============================================================
// 内置 Fixup Pass 声明
// ============================================================

/// 1. 删除 SPIRV-Cross 生成的 "_stU_" UBO 成员前缀
struct UboPrefixFixup : HlslFixupPass {
    const char* name() const override { return "UboPrefixFixup"; }
    bool shouldApply(const std::string& hlsl, const HlslFixupContext& ctx) const override;
    std::string apply(std::string hlsl, const HlslFixupContext& ctx) const override;
};

/// 2. 为 Image pass 插入 gl_FragCoord.y 翻转
struct FragCoordYFlipFixup : HlslFixupPass {
    const char* name() const override { return "FragCoordYFlipFixup"; }
    bool shouldApply(const std::string& hlsl, const HlslFixupContext& ctx) const override;
    std::string apply(std::string hlsl, const HlslFixupContext& ctx) const override;
};

/// 3. 为非 void 函数插入兜底 return 语句（修复 X3507）
struct FallbackReturnFixup : HlslFixupPass {
    const char* name() const override { return "FallbackReturnFixup"; }
    bool shouldApply(const std::string& hlsl, const HlslFixupContext& ctx) const override;
    std::string apply(std::string hlsl, const HlslFixupContext& ctx) const override;
};

/// 4. 为 for/while 循环添加 [loop] 属性（修复 X3511）
struct LoopAttributeFixup : HlslFixupPass {
    const char* name() const override { return "LoopAttributeFixup"; }
    bool shouldApply(const std::string& hlsl, const HlslFixupContext& ctx) const override;
    std::string apply(std::string hlsl, const HlslFixupContext& ctx) const override;
};

/// 创建包含所有内置 fixup pass 的默认管线
HlslFixupPipeline CreateDefaultFixupPipeline();
