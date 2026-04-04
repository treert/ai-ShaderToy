// ============================================================
// DSL Hover Provider — .stoy DSL 层悬停提示
// ============================================================

import { Hover, Position, MarkupContent, MarkupKind } from 'vscode-languageserver';
import { TextDocument } from 'vscode-languageserver-textdocument';
import { StoyDocument, BUILTIN_VARS, STOY_KEYWORDS } from '../types';
import { getWordAtPosition } from './utils';

/** 提供 DSL 层的悬停信息 */
export function provideDslHover(
    doc: StoyDocument,
    textDoc: TextDocument,
    position: Position,
): Hover | null {
    const word = getWordAtPosition(textDoc, position);
    if (!word) return null;

    // 1. 内置变量
    const builtin = BUILTIN_VARS.find(v => v.name === word);
    if (builtin) {
        return {
            contents: {
                kind: MarkupKind.Markdown,
                value: `**${builtin.name}** — \`${builtin.hlslType}\`\n\n${builtin.description}`,
            },
        };
    }

    // 2. Pass 名称
    const pass = doc.passes.find(p => p.name === word);
    if (pass) {
        const isLast = pass.declOrder === doc.passes.length - 1;
        return {
            contents: {
                kind: MarkupKind.Markdown,
                value: `**pass** \`${pass.name}\`${isLast ? ' *(Image output)*' : ' *(Buffer)*'}\n\n` +
                    `声明顺序: #${pass.declOrder}\n\n` +
                    (pass.init.type === 'color' ? `init: \`float4(${pass.init.color.join(', ')})\`` :
                     pass.init.type === 'texture' ? `init: \`texture "${pass.init.texturePath}"\`` :
                     'init: 默认 `float4(0, 0, 0, 0)`') +
                    `\n\n在 HLSL 中使用:\n\`\`\`hlsl\n${pass.name}.Sample(${pass.name}_Sampler, uv)\n${pass.name}_TexelSize // (1/w, 1/h, w, h)\n\`\`\``,
            },
        };
    }

    // 3. Texture 名称
    const tex = doc.textures.find(t => t.name === word);
    if (tex) {
        return {
            contents: {
                kind: MarkupKind.Markdown,
                value: `**texture** \`${tex.name}\`\n\n` +
                    `路径: \`${tex.path}\`\n\n` +
                    `filter: \`${tex.filter}\` | wrap: \`${tex.wrap}\`\n\n` +
                    `在 HLSL 中使用:\n\`\`\`hlsl\n${tex.name}.Sample(${tex.name}_Sampler, uv)\n${tex.name}_TexelSize // (1/w, 1/h, w, h)\n\`\`\``,
            },
        };
    }

    // 4. .stoy 关键字
    const kwDoc = KEYWORD_DOCS[word];
    if (kwDoc) {
        return {
            contents: {
                kind: MarkupKind.Markdown,
                value: kwDoc,
            },
        };
    }

    return null;
}

const KEYWORD_DOCS: Record<string, string> = {
    'global_setting': '**global_setting** `{ ... }`\n\n全局 D3D11 渲染设置。可选块，最多一个。\n\n可用属性: `format`, `msaa`, `vsync`, `resolution_scale`',
    'inner_vars': '**inner_vars** `{ ... }`\n\n声明需要的框架内置变量。只有声明了的变量才会在 HLSL 中可用。\n\n可选块，最多一个。',
    'texture': '**texture** `Name = "path" { ... }`\n\n声明外部纹理资源。自动注入 `Name`, `Name_Sampler`, `Name_TexelSize` 到所有 pass。',
    'common': '**common** `[=[ ... ]=]`\n\n共享 HLSL 代码块，会被注入到每个 pass 的代码之前。\n\n可选，最多一个。',
    'pass': '**pass** `Name { ... }`\n\n渲染通道。至少需要一个 pass。\n\n最后一个 pass 作为最终输出（Image pass）。\n\n必须包含 `code [=[ ... ]=]` 块。',
    'init': '**init** `= float4(r, g, b, a)` | `= texture "path"`\n\n设置 pass 双缓冲纹理的初始值。\n\n默认: `float4(0, 0, 0, 0)` 全黑透明。',
    'code': '**code** `[=[ ... ]=]`\n\nHLSL 代码块。必须实现 `void mainImage(inout float4 fragColor, float2 fragCoord)` 函数。',
};
