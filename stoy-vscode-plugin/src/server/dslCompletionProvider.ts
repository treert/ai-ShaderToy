// ============================================================
// DSL Completion Provider — .stoy DSL 层自动补全
// ============================================================

import {
    CompletionItem, CompletionItemKind, Position,
} from 'vscode-languageserver';
import { TextDocument as TD } from 'vscode-languageserver-textdocument';
import { StoyDocument, BUILTIN_VARS, FILTER_VALUES, WRAP_VALUES, GLOBAL_SETTING_KEYS, TEXTURE_CONFIG_KEYS } from '../types';

/** 判断光标上下文，提供对应的补全项 */
export function provideDslCompletions(
    doc: StoyDocument,
    textDoc: TD,
    position: Position,
): CompletionItem[] {
    const line = textDoc.getText({
        start: { line: position.line, character: 0 },
        end: { line: position.line, character: position.character },
    });
    const fullLine = textDoc.getText({
        start: { line: position.line, character: 0 },
        end: { line: position.line + 1, character: 0 },
    }).trimEnd();

    // 判断当前上下文
    const ctx = getContext(doc, position.line, line, fullLine);

    switch (ctx) {
        case 'top-level':
            return topLevelCompletions(doc);
        case 'inner-vars':
            return innerVarsCompletions(doc);
        case 'global-setting-key':
            return globalSettingKeyCompletions();
        case 'global-setting-vsync-value':
            return booleanCompletions();
        case 'global-setting-format-value':
            return formatCompletions();
        case 'texture-config-key':
            return textureConfigKeyCompletions();
        case 'filter-value':
            return stringValueCompletions(FILTER_VALUES);
        case 'wrap-value':
            return stringValueCompletions(WRAP_VALUES);
        case 'pass-body':
            return passBodyCompletions();
        default:
            return [];
    }
}

function getContext(
    doc: StoyDocument,
    line: number,
    lineTextBeforeCursor: string,
    fullLine: string,
): string {
    const trimmed = lineTextBeforeCursor.trim();

    // 在 inner_vars 块内
    if (doc.innerVars && line > doc.innerVars.range.startLine && line < doc.innerVars.range.endLine) {
        return 'inner-vars';
    }

    // 在 global_setting 块内
    if (doc.globalSetting && line > doc.globalSetting.range.startLine && line < doc.globalSetting.range.endLine) {
        if (trimmed.includes('=')) {
            const key = trimmed.split('=')[0].trim();
            if (key === 'vsync') return 'global-setting-vsync-value';
            if (key === 'format') return 'global-setting-format-value';
        }
        return 'global-setting-key';
    }

    // 在 texture 配置块内
    for (const tex of doc.textures) {
        if (line > tex.range.startLine && line < tex.range.endLine) {
            if (trimmed.includes('=')) {
                const key = trimmed.split('=')[0].trim();
                if (key === 'filter') return 'filter-value';
                if (key === 'wrap') return 'wrap-value';
            }
            return 'texture-config-key';
        }
    }

    // 在 pass 块内（但不在 code 块内）
    for (const pass of doc.passes) {
        if (line > pass.range.startLine && line < pass.range.endLine) {
            // 排除 code 块区域
            if (pass.codeRange.startLine > 0 &&
                line >= pass.codeRange.startLine && line <= pass.codeRange.endLine) {
                return 'hlsl'; // HLSL 区域，不由 DSL 补全
            }
            return 'pass-body';
        }
    }

    // 在 common 代码块内
    if (doc.common && line >= doc.common.codeRange.startLine && line <= doc.common.codeRange.endLine) {
        return 'hlsl';
    }

    // 顶层
    return 'top-level';
}

function topLevelCompletions(doc: StoyDocument): CompletionItem[] {
    const items: CompletionItem[] = [];

    if (!doc.globalSetting) {
        items.push({
            label: 'global_setting',
            kind: CompletionItemKind.Keyword,
            insertText: 'global_setting {\n    $0\n}',
            insertTextFormat: 2, // Snippet
            detail: '全局渲染设置',
        });
    }
    if (!doc.innerVars) {
        items.push({
            label: 'inner_vars',
            kind: CompletionItemKind.Keyword,
            insertText: 'inner_vars {\n    $0\n}',
            insertTextFormat: 2,
            detail: '声明需要的框架内置变量',
        });
    }
    items.push({
        label: 'texture',
        kind: CompletionItemKind.Keyword,
        insertText: 'texture ${1:Name} = "${2:path}" {\n    filter = "${3|linear,point|}"\n    wrap = "${4|clamp,repeat,mirror|}"\n}',
        insertTextFormat: 2,
        detail: '声明外部纹理资源',
    });
    if (!doc.common) {
        items.push({
            label: 'common',
            kind: CompletionItemKind.Keyword,
            insertText: 'common [=[\n    $0\n]=]',
            insertTextFormat: 2,
            detail: '共享 HLSL 代码块',
        });
    }
    items.push({
        label: 'pass',
        kind: CompletionItemKind.Keyword,
        insertText: 'pass ${1:Name} {\n    code [=[\n        void mainImage(inout float4 fragColor, float2 fragCoord) {\n            $0\n        }\n    ]=]\n}',
        insertTextFormat: 2,
        detail: '渲染通道',
    });

    return items;
}

function innerVarsCompletions(doc: StoyDocument): CompletionItem[] {
    const declared = new Set(doc.innerVars?.vars ?? []);
    return BUILTIN_VARS
        .filter(v => !declared.has(v.name))
        .map(v => ({
            label: v.name,
            kind: CompletionItemKind.Variable,
            detail: `${v.hlslType} — ${v.description}`,
        }));
}

function globalSettingKeyCompletions(): CompletionItem[] {
    return GLOBAL_SETTING_KEYS.map(k => ({
        label: k,
        kind: CompletionItemKind.Property,
        insertText: `${k} = `,
    }));
}

function booleanCompletions(): CompletionItem[] {
    return [
        { label: 'true', kind: CompletionItemKind.Value },
        { label: 'false', kind: CompletionItemKind.Value },
    ];
}

function formatCompletions(): CompletionItem[] {
    return [
        { label: '"R8G8B8A8_UNORM"', kind: CompletionItemKind.Value, insertText: '"R8G8B8A8_UNORM"' },
        { label: '"R16G16B16A16_FLOAT"', kind: CompletionItemKind.Value, insertText: '"R16G16B16A16_FLOAT"' },
        { label: '"R32G32B32A32_FLOAT"', kind: CompletionItemKind.Value, insertText: '"R32G32B32A32_FLOAT"' },
    ];
}

function textureConfigKeyCompletions(): CompletionItem[] {
    return TEXTURE_CONFIG_KEYS.map(k => ({
        label: k,
        kind: CompletionItemKind.Property,
        insertText: `${k} = `,
    }));
}

function stringValueCompletions(values: string[]): CompletionItem[] {
    return values.map(v => ({
        label: `"${v}"`,
        kind: CompletionItemKind.Value,
        insertText: `"${v}"`,
    }));
}

function passBodyCompletions(): CompletionItem[] {
    return [
        {
            label: 'init',
            kind: CompletionItemKind.Keyword,
            insertText: 'init = ${1|float4(0\\, 0\\, 0\\, 1),texture "path"|}',
            insertTextFormat: 2,
            detail: '设置 pass 初始值',
        },
        {
            label: 'code',
            kind: CompletionItemKind.Keyword,
            insertText: 'code [=[\n        void mainImage(inout float4 fragColor, float2 fragCoord) {\n            $0\n        }\n    ]=]',
            insertTextFormat: 2,
            detail: 'HLSL 代码块',
        },
    ];
}
