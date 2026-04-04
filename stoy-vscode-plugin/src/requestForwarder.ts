// ============================================================
// Request Forwarder — HLSL 请求转发器
// 将 .stoy HLSL 代码块中的 LSP 请求转发给外部 HLSL 扩展。
// ============================================================

import * as vscode from 'vscode';
import { StoyDocument, BlockRange } from './types';
import { VirtualDocProvider, buildVirtualUri, parseVirtualUri } from './virtualDocProvider';
import { physicalToVirtual, virtualToPhysical, isInBlockRange } from './positionMapper';

export class RequestForwarder {
    private openedDocs = new Set<string>();

    constructor(private virtualDocProvider: VirtualDocProvider) {}

    /**
     * 确保虚拟文档已被 VS Code 打开并识别为 HLSL 语言。
     * 外部 HLSL 扩展只有在文档被加载后才能响应 executeCommand 请求。
     */
    private async ensureVirtualDocOpened(virtualUri: string): Promise<void> {
        if (this.openedDocs.has(virtualUri)) return;
        try {
            const uri = vscode.Uri.parse(virtualUri);
            const doc = await vscode.workspace.openTextDocument(uri);
            // 确保文档被识别为 HLSL
            if (doc.languageId !== 'hlsl') {
                await vscode.languages.setTextDocumentLanguage(doc, 'hlsl');
            }
            this.openedDocs.add(virtualUri);
        } catch (err) {
            // 如果设置语言失败也不影响
            console.error('[Stoy] ensureVirtualDocOpened error:', err);
        }
    }

    /** 虚拟文档缓存失效时清除 opened 标记 */
    invalidateOpened(physicalUri: string): void {
        for (const key of this.openedDocs) {
            if (key.includes(physicalUri)) {
                this.openedDocs.delete(key);
            }
        }
    }

    /**
     * 判断光标是否在 HLSL 代码块内，如果是则返回虚拟文档信息
     */
    findHlslBlock(doc: StoyDocument, physicalUri: string, line: number, character: number): {
        virtualUri: string;
        virtualPosition: vscode.Position;
    } | null {
        // 检查 common 块
        if (doc.common && isInBlockRange(line, character, doc.common.codeRange)) {
            const virtualUri = buildVirtualUri(physicalUri, 'common', '_common_');
            const info = this.virtualDocProvider.getVirtualDocInfo(virtualUri);
            if (!info) return null;
            const virtualLine = physicalToVirtual(line, info.blockRange, info.prefixLineCount);
            if (virtualLine < 0) return null;
            return {
                virtualUri,
                virtualPosition: new vscode.Position(virtualLine, character),
            };
        }

        // 检查各 pass 的 code 块
        for (const pass of doc.passes) {
            if (pass.codeRange.startLine > 0 && isInBlockRange(line, character, pass.codeRange)) {
                const virtualUri = buildVirtualUri(physicalUri, 'pass', pass.name);
                const info = this.virtualDocProvider.getVirtualDocInfo(virtualUri);
                if (!info) return null;
                const virtualLine = physicalToVirtual(line, info.blockRange, info.prefixLineCount);
                if (virtualLine < 0) return null;
                return {
                    virtualUri,
                    virtualPosition: new vscode.Position(virtualLine, character),
                };
            }
        }

        return null;
    }

    /**
     * 将虚拟文档中的位置映射回物理文件位置
     */
    mapVirtualPositionToPhysical(
        virtualUri: string,
        virtualLine: number,
        virtualCharacter: number,
    ): { line: number; character: number } | null {
        const info = this.virtualDocProvider.getVirtualDocInfo(virtualUri);
        if (!info) return null;
        const physicalLine = virtualToPhysical(virtualLine, info.blockRange, info.prefixLineCount);
        if (physicalLine < 0) return null;
        return { line: physicalLine, character: virtualCharacter };
    }

    /**
     * 转发自动补全请求
     */
    async forwardCompletion(
        virtualUri: string,
        virtualPosition: vscode.Position,
        triggerCharacter?: string,
    ): Promise<vscode.CompletionItem[] | vscode.CompletionList | undefined> {
        try {
            await this.ensureVirtualDocOpened(virtualUri);
            const uri = vscode.Uri.parse(virtualUri);
            const result = await vscode.commands.executeCommand<vscode.CompletionList>(
                'vscode.executeCompletionItemProvider',
                uri,
                virtualPosition,
                triggerCharacter,
            );
            return result;
        } catch {
            return undefined; // 静默降级
        }
    }

    /**
     * 转发悬停请求
     */
    async forwardHover(
        virtualUri: string,
        virtualPosition: vscode.Position,
    ): Promise<vscode.Hover[] | undefined> {
        try {
            await this.ensureVirtualDocOpened(virtualUri);
            const uri = vscode.Uri.parse(virtualUri);
            console.log(`[Stoy] forwardHover: uri=${uri.toString()} scheme=${uri.scheme} line=${virtualPosition.line} char=${virtualPosition.character}`);
            
            // 检查虚拟文档内容
            const openDoc = vscode.workspace.textDocuments.find(d => d.uri.toString() === uri.toString());
            if (openDoc) {
                console.log(`[Stoy] Virtual doc found: languageId=${openDoc.languageId} lines=${openDoc.lineCount}`);
                const targetLine = openDoc.lineAt(virtualPosition.line).text;
                console.log(`[Stoy] Target line: "${targetLine}"`);
            } else {
                console.log(`[Stoy] Virtual doc NOT found in workspace.textDocuments`);
            }
            
            const result = await vscode.commands.executeCommand<vscode.Hover[]>(
                'vscode.executeHoverProvider',
                uri,
                virtualPosition,
            );
            console.log(`[Stoy] forwardHover result: ${result ? `${result.length} hovers` : 'null/undefined'}`);
            return result;
        } catch (err) {
            console.error(`[Stoy] forwardHover error:`, err);
            return undefined;
        }
    }

    /**
     * 转发跳转定义请求
     */
    async forwardDefinition(
        virtualUri: string,
        virtualPosition: vscode.Position,
    ): Promise<(vscode.Location | vscode.LocationLink)[] | undefined> {
        try {
            await this.ensureVirtualDocOpened(virtualUri);
            const uri = vscode.Uri.parse(virtualUri);
            const result = await vscode.commands.executeCommand<(vscode.Location | vscode.LocationLink)[]>(
                'vscode.executeDefinitionProvider',
                uri,
                virtualPosition,
            );
            return result;
        } catch {
            return undefined;
        }
    }

    /**
     * 转发查找引用请求
     */
    async forwardReferences(
        virtualUri: string,
        virtualPosition: vscode.Position,
    ): Promise<vscode.Location[] | undefined> {
        try {
            await this.ensureVirtualDocOpened(virtualUri);
            const uri = vscode.Uri.parse(virtualUri);
            const result = await vscode.commands.executeCommand<vscode.Location[]>(
                'vscode.executeReferenceProvider',
                uri,
                virtualPosition,
            );
            return result;
        } catch {
            return undefined;
        }
    }

    /**
     * 转发签名帮助请求
     */
    async forwardSignatureHelp(
        virtualUri: string,
        virtualPosition: vscode.Position,
        triggerCharacter?: string,
    ): Promise<vscode.SignatureHelp | undefined> {
        try {
            await this.ensureVirtualDocOpened(virtualUri);
            const uri = vscode.Uri.parse(virtualUri);
            const result = await vscode.commands.executeCommand<vscode.SignatureHelp>(
                'vscode.executeSignatureHelpProvider',
                uri,
                virtualPosition,
                triggerCharacter,
            );
            return result;
        } catch {
            return undefined;
        }
    }
}
