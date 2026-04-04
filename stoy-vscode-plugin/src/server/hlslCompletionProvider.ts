// ============================================================
// HLSL Completion Provider — HLSL 区域自动补全（L0 + L1）
// ============================================================

import { CompletionItem, CompletionItemKind, Position } from 'vscode-languageserver';
import { TextDocument } from 'vscode-languageserver-textdocument';
import { StoyDocument, BUILTIN_VARS } from '../types';
import { HLSL_FUNCTIONS, HLSL_TYPES } from '../hlslBuiltins';
import { HlslSymbol, getVisibleSymbols } from '../hlslSymbolScanner';

export function provideHlslCompletions(
    doc: StoyDocument,
    textDoc: TextDocument,
    position: Position,
    symbols: HlslSymbol[],
): CompletionItem[] {
    const items: CompletionItem[] = [];

    // 1. HLSL 内置函数（L0）
    for (const func of HLSL_FUNCTIONS) {
        items.push({
            label: func.name,
            kind: CompletionItemKind.Function,
            detail: func.signature,
            documentation: func.description,
        });
    }

    // 2. HLSL 内置类型（L0）
    for (const type of HLSL_TYPES) {
        items.push({
            label: type.name,
            kind: CompletionItemKind.TypeParameter,
            detail: type.description,
        });
    }

    // 3. HLSL 关键字
    for (const kw of HLSL_KEYWORDS) {
        items.push({
            label: kw,
            kind: CompletionItemKind.Keyword,
        });
    }

    // 4. 框架内置变量
    for (const v of BUILTIN_VARS) {
        items.push({
            label: v.name,
            kind: CompletionItemKind.Variable,
            detail: `${v.hlslType} — ${v.description}`,
        });
    }

    // 5. texture 名称 + 派生变量
    for (const tex of doc.textures) {
        items.push({
            label: tex.name,
            kind: CompletionItemKind.Field,
            detail: `Texture2D — ${tex.path}`,
        });
        items.push({
            label: `${tex.name}_Sampler`,
            kind: CompletionItemKind.Field,
            detail: `SamplerState for ${tex.name}`,
        });
        items.push({
            label: `${tex.name}_TexelSize`,
            kind: CompletionItemKind.Field,
            detail: `float4 (1/w, 1/h, w, h) for ${tex.name}`,
        });
    }

    // 6. pass 名称 + 派生变量
    for (const pass of doc.passes) {
        items.push({
            label: pass.name,
            kind: CompletionItemKind.Field,
            detail: `Texture2D — Pass buffer`,
        });
        items.push({
            label: `${pass.name}_Sampler`,
            kind: CompletionItemKind.Field,
            detail: `SamplerState for ${pass.name}`,
        });
        items.push({
            label: `${pass.name}_TexelSize`,
            kind: CompletionItemKind.Field,
            detail: `float4 (1/w, 1/h, w, h) for ${pass.name}`,
        });
    }

    // 7. 用户定义符号（L1）
    const visible = getVisibleSymbols(symbols, doc, position.line);
    for (const sym of visible) {
        const kind = symbolKindToCompletionKind(sym.kind);
        items.push({
            label: sym.name,
            kind,
            detail: sym.signature,
            documentation: sym.source === 'common' ? '定义于 common 块' : `定义于 pass ${sym.passName}`,
        });

        // struct 成员也加入补全（方便使用）
        if (sym.kind === 'struct' && sym.members) {
            for (const m of sym.members) {
                items.push({
                    label: m.name,
                    kind: CompletionItemKind.Field,
                    detail: `${m.signature}  (${sym.name} member)`,
                });
            }
        }
    }

    return items;
}

function symbolKindToCompletionKind(kind: string): CompletionItemKind {
    switch (kind) {
        case 'function': return CompletionItemKind.Function;
        case 'struct': return CompletionItemKind.Struct;
        case 'variable': return CompletionItemKind.Variable;
        case 'parameter': return CompletionItemKind.Variable;
        case 'macro': return CompletionItemKind.Constant;
        case 'field': return CompletionItemKind.Field;
        default: return CompletionItemKind.Text;
    }
}

const HLSL_KEYWORDS = [
    'if', 'else', 'for', 'while', 'do', 'switch', 'case', 'default',
    'break', 'continue', 'return', 'discard',
    'struct', 'cbuffer', 'static', 'const', 'inline',
    'in', 'out', 'inout',
    'true', 'false',
    'register', 'packoffset',
];
