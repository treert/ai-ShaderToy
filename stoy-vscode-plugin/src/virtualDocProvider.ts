// ============================================================
// Virtual Document Provider — 虚拟 HLSL 文档内容提供者
// 为每个 .stoy 的 HLSL 代码块生成虚拟 .hlsl 文档。
// ============================================================

import * as vscode from 'vscode';
import { StoyParser } from './stoyParser';
import { StoyDocument, VirtualDocInfo } from './types';
import { generatePassVirtualDoc, generateCommonVirtualDoc, HlslVirtualDoc } from './hlslGenerator';

export const VIRTUAL_SCHEME = 'stoy-hlsl';

/**
 * URI 格式:
 *   stoy-hlsl://hlsl/<blockType>/<blockName>/<encodedPhysicalUri>.hlsl
 *
 * 例如:
 *   stoy-hlsl://hlsl/pass/Feedback/file%3A%2F%2F%2Fc%3A%2Ftest.stoy.hlsl
 *   stoy-hlsl://hlsl/common/_common_/file%3A%2F%2F%2Fc%3A%2Ftest.stoy.hlsl
 */

export class VirtualDocProvider implements vscode.TextDocumentContentProvider {
    private parser = new StoyParser();
    private docCache = new Map<string, StoyDocument>();
    private virtualDocCache = new Map<string, HlslVirtualDoc>();
    private virtualDocInfoCache = new Map<string, VirtualDocInfo>();

    private _onDidChange = new vscode.EventEmitter<vscode.Uri>();
    readonly onDidChange = this._onDidChange.event;

    /** 文档变更时刷新虚拟文档缓存 */
    invalidate(physicalUri: string, text: string): void {
        const doc = this.parser.parse(text);
        this.docCache.set(physicalUri, doc);

        // 清除该物理文件的所有虚拟文档缓存
        for (const [key] of this.virtualDocCache) {
            if (key.includes(encodeURIComponent(physicalUri))) {
                this.virtualDocCache.delete(key);
                this.virtualDocInfoCache.delete(key);
            }
        }

        // 通知虚拟文档更新
        if (doc.common) {
            const uri = buildVirtualUri(physicalUri, 'common', '_common_');
            this._onDidChange.fire(vscode.Uri.parse(uri));
        }
        for (const pass of doc.passes) {
            const uri = buildVirtualUri(physicalUri, 'pass', pass.name);
            this._onDidChange.fire(vscode.Uri.parse(uri));
        }
    }

    /** 获取解析后的文档 */
    getStoyDocument(physicalUri: string): StoyDocument | undefined {
        return this.docCache.get(physicalUri);
    }

    /** 获取虚拟文档信息（用于请求转发） */
    getVirtualDocInfo(virtualUri: string): VirtualDocInfo | undefined {
        // 先尝试缓存
        let info = this.virtualDocInfoCache.get(virtualUri);
        if (info) return info;

        // 解析 URI 生成信息
        const parsed = parseVirtualUri(virtualUri);
        if (!parsed) return undefined;

        const doc = this.docCache.get(parsed.physicalUri);
        if (!doc) return undefined;

        if (parsed.blockType === 'common' && doc.common) {
            const vdoc = generateCommonVirtualDoc(doc);
            info = {
                uri: virtualUri,
                prefixLineCount: vdoc.prefixLineCount,
                blockRange: doc.common.codeRange,
                blockType: 'common',
            };
        } else if (parsed.blockType === 'pass') {
            const passIndex = doc.passes.findIndex(p => p.name === parsed.blockName);
            if (passIndex >= 0) {
                const vdoc = generatePassVirtualDoc(doc, passIndex);
                info = {
                    uri: virtualUri,
                    prefixLineCount: vdoc.prefixLineCount,
                    blockRange: doc.passes[passIndex].codeRange,
                    blockType: 'pass',
                    passName: parsed.blockName,
                };
            }
        }

        if (info) {
            this.virtualDocInfoCache.set(virtualUri, info);
        }
        return info;
    }

    // VS Code TextDocumentContentProvider 接口
    provideTextDocumentContent(uri: vscode.Uri): string {
        const uriStr = uri.toString();

        // 检查缓存
        const cached = this.virtualDocCache.get(uriStr);
        if (cached) return cached.content;

        // 解析 URI
        const parsed = parseVirtualUri(uriStr);
        if (!parsed) return '// Error: invalid virtual document URI';

        const doc = this.docCache.get(parsed.physicalUri);
        if (!doc) return '// Error: source .stoy document not found';

        let vdoc: HlslVirtualDoc;
        if (parsed.blockType === 'common') {
            vdoc = generateCommonVirtualDoc(doc);
        } else {
            const passIndex = doc.passes.findIndex(p => p.name === parsed.blockName);
            if (passIndex < 0) return `// Error: pass '${parsed.blockName}' not found`;
            vdoc = generatePassVirtualDoc(doc, passIndex);
        }

        this.virtualDocCache.set(uriStr, vdoc);
        return vdoc.content;
    }

    dispose(): void {
        this._onDidChange.dispose();
    }
}

// ============================================================
// URI 工具函数
// ============================================================

export function buildVirtualUri(physicalUri: string, blockType: string, blockName: string): string {
    return `${VIRTUAL_SCHEME}://hlsl/${blockType}/${blockName}/${encodeURIComponent(physicalUri)}.hlsl`;
}

export function parseVirtualUri(uri: string): { physicalUri: string; blockType: string; blockName: string } | null {
    // stoy-hlsl://hlsl/<blockType>/<blockName>/<encodedPhysicalUri>.hlsl
    const prefix = `${VIRTUAL_SCHEME}://hlsl/`;
    if (!uri.startsWith(prefix)) return null;

    const rest = uri.slice(prefix.length);
    const parts = rest.split('/');
    if (parts.length < 3) return null;

    const blockType = parts[0];
    const blockName = parts[1];
    let encodedUri = parts.slice(2).join('/');
    if (encodedUri.endsWith('.hlsl')) {
        encodedUri = encodedUri.slice(0, -5); // remove .hlsl suffix
    }

    return {
        physicalUri: decodeURIComponent(encodedUri),
        blockType,
        blockName,
    };
}
