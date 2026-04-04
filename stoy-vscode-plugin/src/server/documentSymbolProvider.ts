// ============================================================
// Document Symbol Provider — Outline 视图支持
// DSL 顶层块作为一级节点，HLSL 用户符号嵌套在 common/pass 下。
// ============================================================

import { DocumentSymbol, SymbolKind, SymbolTag, Range } from 'vscode-languageserver';
import { StoyDocument, BlockRange, BUILTIN_VARS } from '../types';
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

    // inner_vars — 显示所有可用内置变量（已声明在前，未声明在后）
    if (doc.innerVars) {
        const declaredSet = new Set(doc.innerVars.vars);
        const enabledChildren: DocumentSymbol[] = [];
        const disabledChildren: DocumentSymbol[] = [];

        for (const v of BUILTIN_VARS) {
            if (declaredSet.has(v.name)) {
                const vr = doc.innerVars.varRanges.get(v.name);
                const r = vr ? toRange(vr) : toRange(doc.innerVars.range);
                enabledChildren.push({
                    name: v.name,
                    detail: `${v.hlslType} — ${v.description}`,
                    kind: SymbolKind.Variable,
                    range: r,
                    selectionRange: r,
                });
            } else {
                const r = toRange(doc.innerVars.range);
                disabledChildren.push({
                    name: v.name,
                    detail: `(未启用) ${v.hlslType} — ${v.description}`,
                    kind: SymbolKind.Variable,
                    range: r,
                    selectionRange: r,
                    tags: [SymbolTag.Deprecated],
                });
            }
        }

        const children: DocumentSymbol[] = [...enabledChildren];

        // 未启用的放到单独子树
        if (disabledChildren.length > 0) {
            const r = toRange(doc.innerVars.range);
            children.push({
                name: `可用变量 (未启用 ${disabledChildren.length} 个)`,
                kind: SymbolKind.Enum,
                range: r,
                selectionRange: r,
                children: disabledChildren,
            });
        }

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
        const children = buildNestedSymbols(commonSymbols);
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
        const children = buildNestedSymbols(passSymbols);
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

/**
 * 将符号列表构建为嵌套结构：局部变量放到所属函数下面。
 * 判断依据：variable 的 line 在某个 function 的 [line, endLine] 范围内。
 */
function buildNestedSymbols(syms: HlslSymbol[]): DocumentSymbol[] {
    const functions = syms.filter(s => s.kind === 'function');
    const others = syms.filter(s => s.kind !== 'function');

    // 先构建所有函数的 DocumentSymbol
    const funcDocSymbols = functions.map(f => {
        const ds = hlslSymbolToDocSymbol(f);
        // 找到属于这个函数的局部变量
        if (f.endLine !== undefined) {
            const localVars = others.filter(
                s => s.kind === 'variable' && s.line > f.line && s.line <= f.endLine!
            );
            if (localVars.length > 0) {
                const existingChildren = ds.children ?? [];
                ds.children = [
                    ...existingChildren,
                    ...localVars.map(v => hlslSymbolToDocSymbol(v)),
                ];
            }
        }
        return { sym: f, ds };
    });

    // 收集已被嵌套到函数内的变量
    const nestedVarLines = new Set<number>();
    for (const f of functions) {
        if (f.endLine !== undefined) {
            for (const s of others) {
                if (s.kind === 'variable' && s.line > f.line && s.line <= f.endLine) {
                    nestedVarLines.add(s.line);
                }
            }
        }
    }

    // 顶层符号 = 函数 + 未被嵌套的其他符号（struct/macro/全局变量）
    const topLevel: DocumentSymbol[] = [];
    // 按原始行号排序
    const allEntries: { line: number; ds: DocumentSymbol }[] = [];

    for (const { sym, ds } of funcDocSymbols) {
        allEntries.push({ line: sym.line, ds });
    }
    for (const s of others) {
        if (s.kind === 'variable' && nestedVarLines.has(s.line)) continue; // 已嵌套
        allEntries.push({ line: s.line, ds: hlslSymbolToDocSymbol(s) });
    }

    allEntries.sort((a, b) => a.line - b.line);
    for (const e of allEntries) {
        topLevel.push(e.ds);
    }

    return topLevel;
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
