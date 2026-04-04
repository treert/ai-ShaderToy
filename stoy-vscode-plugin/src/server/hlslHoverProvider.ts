// ============================================================
// HLSL Hover Provider — HLSL 区域悬停提示（L0 + L1）
// 在 Language Server 内处理，不依赖外部扩展。
// ============================================================

import { Hover, MarkupKind, Position } from 'vscode-languageserver';
import { TextDocument } from 'vscode-languageserver-textdocument';
import { StoyDocument, BUILTIN_VARS } from '../types';
import { findHlslFunction, findHlslType, getHlslDocUrl, getHlslTypeDocUrl } from '../hlslBuiltins';
import { HlslSymbol, getVisibleSymbols } from '../hlslSymbolScanner';
import { getWordAtPosition } from './utils';

export function provideHlslHover(
    doc: StoyDocument,
    textDoc: TextDocument,
    position: Position,
    symbols: HlslSymbol[],
): Hover | null {
    const word = getWordAtPosition(textDoc, position);
    if (!word) return null;

    // 1. HLSL 内置函数（L0）
    const func = findHlslFunction(word);
    if (func) {
        let md = `\`\`\`hlsl\n${func.signature}\n\`\`\`\n\n${func.description}`;
        if (func.params) md += `\n\n**Parameters:**\n\n${func.params}`;
        md += `\n\n[📖 HLSL Reference](${getHlslDocUrl(func)})`;
        return { contents: { kind: MarkupKind.Markdown, value: md } };
    }

    // 2. HLSL 内置类型（L0）
    const type = findHlslType(word);
    if (type) {
        let md = `\`\`\`hlsl\n${type.name}\n\`\`\`\n\n${type.description}`;
        const typeUrl = getHlslTypeDocUrl(type);
        if (typeUrl) md += `\n\n[📖 HLSL Reference](${typeUrl})`;
        return {
            contents: {
                kind: MarkupKind.Markdown,
                value: md,
            },
        };
    }

    // 3. 框架内置变量
    const builtin = BUILTIN_VARS.find(v => v.name === word);
    if (builtin) {
        return {
            contents: {
                kind: MarkupKind.Markdown,
                value: `\`\`\`hlsl\n${builtin.hlslType} ${builtin.name}\n\`\`\`\n\n${builtin.description}`,
            },
        };
    }

    // 4. texture 名称 + 派生变量（Name, Name_Sampler, Name_TexelSize）
    for (const tex of doc.textures) {
        if (word === tex.name) {
            return {
                contents: {
                    kind: MarkupKind.Markdown,
                    value: `\`\`\`hlsl\nTexture2D ${tex.name}\n\`\`\`\n\n` +
                        `外部纹理 — \`${tex.path}\`\n\n` +
                        `filter: \`${tex.filter}\` | wrap: \`${tex.wrap}\`\n\n` +
                        `用法:\n\`\`\`hlsl\n${tex.name}.Sample(${tex.name}_Sampler, uv)\n\`\`\``,
                },
            };
        }
        if (word === `${tex.name}_Sampler`) {
            return {
                contents: {
                    kind: MarkupKind.Markdown,
                    value: `\`\`\`hlsl\nSamplerState ${tex.name}_Sampler\n\`\`\`\n\n纹理 \`${tex.name}\` 的采样器 (filter: ${tex.filter}, wrap: ${tex.wrap})`,
                },
            };
        }
        if (word === `${tex.name}_TexelSize`) {
            return {
                contents: {
                    kind: MarkupKind.Markdown,
                    value: `\`\`\`hlsl\nfloat4 ${tex.name}_TexelSize\n\`\`\`\n\n纹理 \`${tex.name}\` 的像素尺寸 — \`(1/width, 1/height, width, height)\``,
                },
            };
        }
    }

    // 5. pass 名称 + 派生变量
    for (const pass of doc.passes) {
        const isLast = pass.declOrder === doc.passes.length - 1;
        if (word === pass.name) {
            return {
                contents: {
                    kind: MarkupKind.Markdown,
                    value: `\`\`\`hlsl\nTexture2D ${pass.name}\n\`\`\`\n\n` +
                        `Pass buffer${isLast ? ' *(Image output)*' : ''}\n\n` +
                        `用法:\n\`\`\`hlsl\n${pass.name}.Sample(${pass.name}_Sampler, uv)\n\`\`\``,
                },
            };
        }
        if (word === `${pass.name}_Sampler`) {
            return {
                contents: {
                    kind: MarkupKind.Markdown,
                    value: `\`\`\`hlsl\nSamplerState ${pass.name}_Sampler\n\`\`\`\n\nPass \`${pass.name}\` 的采样器`,
                },
            };
        }
        if (word === `${pass.name}_TexelSize`) {
            return {
                contents: {
                    kind: MarkupKind.Markdown,
                    value: `\`\`\`hlsl\nfloat4 ${pass.name}_TexelSize\n\`\`\`\n\nPass \`${pass.name}\` 的像素尺寸 — \`(1/width, 1/height, width, height)\``,
                },
            };
        }
    }

    // 6. pass 入口函数 mainImage（特殊标识）
    if (word === 'mainImage') {
        // 判断当前在哪个 pass 中
        let currentPassName: string | undefined;
        let isImagePass = false;
        for (const pass of doc.passes) {
            if (pass.codeRange.startLine > 0 &&
                position.line >= pass.codeRange.startLine && position.line <= pass.codeRange.endLine) {
                currentPassName = pass.name;
                isImagePass = pass.declOrder === doc.passes.length - 1;
                break;
            }
        }
        let md = `\`\`\`hlsl\nvoid mainImage(inout float4 fragColor, float2 fragCoord)\n\`\`\`\n\n`;
        md += `**Pass 入口函数** — 每个 pass 的 code 块中必须实现此函数。\n\n`;
        md += `框架会为每个像素调用此函数，通过 \`fragColor\` 输出颜色，\`fragCoord\` 为当前像素坐标。`;
        if (currentPassName) {
            md += `\n\n*当前所在 pass: \`${currentPassName}\`${isImagePass ? ' (Image output)' : ' (Buffer)'}*`;
        }
        return { contents: { kind: MarkupKind.Markdown, value: md } };
    }

    // 7. 用户定义的符号（L1）
    const visible = getVisibleSymbols(symbols, doc, position.line);
    const userSym = visible.find(s => s.name === word);
    if (userSym) {
        let md = `\`\`\`hlsl\n${userSym.signature}\n\`\`\``;
        if (userSym.kind === 'parameter') {
            md += `\n\n*(parameter)*`;
        } else {
            const loc = userSym.source === 'common' ? 'common 块' : `pass ${userSym.passName}`;
            md += `\n\n*定义于 ${loc} (line ${userSym.line + 1})*`;
        }
        if (userSym.kind === 'struct' && userSym.members && userSym.members.length > 0) {
            md += '\n\n**Members:**\n';
            for (const m of userSym.members) {
                md += `\n- \`${m.signature}\``;
            }
        }
        return { contents: { kind: MarkupKind.Markdown, value: md } };
    }

    return null;
}

