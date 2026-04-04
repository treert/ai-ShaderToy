// ============================================================
// Folding Range Provider — 代码折叠支持
// DSL 顶层块 + HLSL function/struct 折叠。
// ============================================================

import { FoldingRange, FoldingRangeKind } from 'vscode-languageserver';
import { StoyDocument } from '../types';
import { HlslSymbol } from '../hlslSymbolScanner';

export function provideFoldingRanges(
    doc: StoyDocument,
    symbols: HlslSymbol[],
): FoldingRange[] {
    const ranges: FoldingRange[] = [];

    // ---- DSL 顶层块 ----

    // global_setting { ... }
    if (doc.globalSetting) {
        addRange(ranges, doc.globalSetting.range.startLine, doc.globalSetting.range.endLine);
    }

    // inner_vars { ... }
    if (doc.innerVars) {
        addRange(ranges, doc.innerVars.range.startLine, doc.innerVars.range.endLine);
    }

    // texture 声明（仅多行时折叠）
    for (const tex of doc.textures) {
        addRange(ranges, tex.range.startLine, tex.range.endLine);
    }

    // common [=[ ... ]=]
    if (doc.common) {
        addRange(ranges, doc.common.range.startLine, doc.common.range.endLine);
    }

    // pass name { ... }
    for (const pass of doc.passes) {
        addRange(ranges, pass.range.startLine, pass.range.endLine);
    }

    // ---- HLSL 符号（function / struct） ----

    for (const sym of symbols) {
        if ((sym.kind === 'function' || sym.kind === 'struct') && sym.endLine !== undefined) {
            addRange(ranges, sym.line, sym.endLine);
        }
    }

    return ranges;
}

/** 仅当 startLine < endLine 时添加折叠范围（单行不折叠） */
function addRange(ranges: FoldingRange[], startLine: number, endLine: number): void {
    if (startLine < endLine) {
        ranges.push({
            startLine,
            endLine,
            kind: FoldingRangeKind.Region,
        });
    }
}
