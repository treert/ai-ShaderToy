// ============================================================
// Document Symbol Provider — Outline 视图支持
// DSL 顶层块作为一级节点，HLSL 用户符号嵌套在 common/pass 下。
// ============================================================

import { DocumentSymbol, SymbolKind, Range } from 'vscode-languageserver';
import { StoyDocument, BlockRange } from '../types';
import { HlslSymbol } from '../hlslSymbolScanner';

export function provideDocumentSymbols(
    doc: StoyDocument,
    symbols: HlslSymbol[],
): DocumentSymbol[] {
    const result: DocumentSymbol[] = [];

    // global_setting
    if (doc.globalSetting) {
        result.push({
            name: 'global_setting',
            kind: SymbolKind.Module,
            range: toRange(doc.globalSetting.range),
            selectionRange: toRange(doc.globalSetting.range),
        });
    }

    // inner_vars
    if (doc.innerVars) {
        const children: DocumentSymbol[] = doc.innerVars.vars.map(v => {
            const vr = doc.innerVars!.varRanges.get(v);
            const r = vr ? toRange(vr) : toRange(doc.innerVars!.range);
            return {
                name: v,
                kind: SymbolKind.Variable,
                range: r,
                selectionRange: r,
            };
        });
        result.push({
            name: 'inner_vars',
            kind: SymbolKind.Module,
            range: toRange(doc.innerVars.range),
            selectionRange: toRange(doc.innerVars.range),
            children,
        });
    }

    // textures
    for (const tex of doc.textures) {
        result.push({
            name: `texture ${tex.name}`,
            detail: tex.path,
            kind: SymbolKind.Field,
            range: toRange(tex.range),
            selectionRange: toRange(tex.nameRange),
        });
    }

    // common
    if (doc.common) {
        const commonSymbols = symbols.filter(s => s.source === 'common' && s.kind !== 'parameter' && s.kind !== 'field');
        const children = commonSymbols.map(s => hlslSymbolToDocSymbol(s));
        result.push({
            name: 'common',
            kind: SymbolKind.Module,
            range: toRange(doc.common.range),
            selectionRange: toRange(doc.common.range),
            children,
        });
    }

    // passes
    for (const pass of doc.passes) {
        const passSymbols = symbols.filter(
            s => s.source === 'pass' && s.passName === pass.name && s.kind !== 'parameter' && s.kind !== 'field'
        );
        const children = passSymbols.map(s => hlslSymbolToDocSymbol(s));
        result.push({
            name: `pass ${pass.name}`,
            kind: SymbolKind.Class,
            range: toRange(pass.range),
            selectionRange: toRange(pass.nameRange),
            children,
        });
    }

    return result;
}

function hlslSymbolToDocSymbol(sym: HlslSymbol): DocumentSymbol {
    const nameLen = sym.name ? sym.name.length : 1;
    const selectionRange = Range.create(sym.line, sym.col, sym.line, sym.col + nameLen);
    // range 覆盖符号完整定义范围（function/struct 到 } 结束行）
    const endLine = sym.endLine ?? sym.line;
    const range = Range.create(sym.line, 0, endLine, 999);
    const ds: DocumentSymbol = {
        name: sym.name,
        detail: sym.signature,
        kind: hlslKindToSymbolKind(sym.kind),
        range,
        selectionRange,
    };

    // struct 成员作为子节点
    if (sym.kind === 'struct' && sym.members && sym.members.length > 0) {
        ds.children = sym.members.map(m => {
            const mr = Range.create(m.line, m.col, m.line, m.col + m.name.length);
            return {
                name: m.name,
                detail: m.signature,
                kind: SymbolKind.Field,
                range: mr,
                selectionRange: mr,
            };
        });
    }

    return ds;
}

function hlslKindToSymbolKind(kind: string): SymbolKind {
    switch (kind) {
        case 'function': return SymbolKind.Function;
        case 'struct': return SymbolKind.Struct;
        case 'variable': return SymbolKind.Variable;
        case 'macro': return SymbolKind.Constant;
        default: return SymbolKind.Variable;
    }
}

function toRange(br: BlockRange): Range {
    return Range.create(br.startLine, br.startCol, br.endLine, br.endCol);
}
