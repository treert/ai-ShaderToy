// ============================================================
// Extension Entry — .stoy Language 扩展主入口
// 注册 Language Client、虚拟文档提供者、middleware 路由。
// ============================================================

import * as path from 'path';
import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
    Middleware,
} from 'vscode-languageclient/node';

import { VirtualDocProvider, VIRTUAL_SCHEME } from './virtualDocProvider';
import { RequestForwarder } from './requestForwarder';

let client: LanguageClient;
let virtualDocProvider: VirtualDocProvider;
let requestForwarder: RequestForwarder;

const outputChannel = vscode.window.createOutputChannel('Stoy Language');

export function activate(context: vscode.ExtensionContext) {
    outputChannel.appendLine('[Stoy] Extension activating...');

    // 1. 注册虚拟文档提供者
    virtualDocProvider = new VirtualDocProvider();
    context.subscriptions.push(
        vscode.workspace.registerTextDocumentContentProvider(VIRTUAL_SCHEME, virtualDocProvider)
    );

    // 2. 创建请求转发器
    requestForwarder = new RequestForwarder(virtualDocProvider);

    // 3. 监听 .stoy 文档变更，刷新虚拟文档
    context.subscriptions.push(
        vscode.workspace.onDidChangeTextDocument(e => {
            if (e.document.languageId === 'stoy') {
                virtualDocProvider.invalidate(e.document.uri.toString(), e.document.getText());
            }
        })
    );
    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(doc => {
            if (doc.languageId === 'stoy') {
                virtualDocProvider.invalidate(doc.uri.toString(), doc.getText());
            }
        })
    );

    // 4. Language Server 配置
    const serverModule = context.asAbsolutePath(path.join('out', 'server.js'));
    const serverOptions: ServerOptions = {
        run: { module: serverModule, transport: TransportKind.ipc },
        debug: { module: serverModule, transport: TransportKind.ipc, options: { execArgv: ['--nolazy', '--inspect=6009'] } },
    };

    // 5. Middleware — 拦截 HLSL 区域请求并转发
    const middleware: Middleware = {
        provideCompletionItem: async (document, position, context, token, next) => {
            const hlslResult = tryForwardToHlsl(document, position);
            if (hlslResult) {
                const result = await requestForwarder.forwardCompletion(
                    hlslResult.virtualUri,
                    hlslResult.virtualPosition,
                    context.triggerCharacter,
                );
                if (result) {
                    // B5: 映射补全结果中的 textEdit range
                    return mapCompletionResult(result, hlslResult.virtualUri);
                }
            }
            return next(document, position, context, token);
        },
        provideHover: async (document, position, token, next) => {
            outputChannel.appendLine(`[Stoy] Hover request at ${document.uri.toString()} line=${position.line} char=${position.character}`);
            const hlslResult = tryForwardToHlsl(document, position);
            if (hlslResult) {
                outputChannel.appendLine(`[Stoy] -> In HLSL block, forwarding to ${hlslResult.virtualUri} vLine=${hlslResult.virtualPosition.line}`);
                const hovers = await requestForwarder.forwardHover(
                    hlslResult.virtualUri,
                    hlslResult.virtualPosition,
                );
                outputChannel.appendLine(`[Stoy] -> Forward result: ${hovers ? hovers.length + ' hovers' : 'null/undefined'}`);
                if (hovers && hovers.length > 0) {
                    const hover = hovers[0];
                    // B5: 映射悬停结果中的 range
                    if (hover.range) {
                        const mappedStart = requestForwarder.mapVirtualPositionToPhysical(
                            hlslResult.virtualUri, hover.range.start.line, hover.range.start.character,
                        );
                        const mappedEnd = requestForwarder.mapVirtualPositionToPhysical(
                            hlslResult.virtualUri, hover.range.end.line, hover.range.end.character,
                        );
                        if (mappedStart && mappedEnd) {
                            hover.range = new vscode.Range(
                                new vscode.Position(mappedStart.line, mappedStart.character),
                                new vscode.Position(mappedEnd.line, mappedEnd.character),
                            );
                        }
                    }
                    return hover;
                }
            }
            outputChannel.appendLine(`[Stoy] -> Not in HLSL block, delegating to Language Server`);
            return next(document, position, token);
        },
        provideDefinition: async (document, position, token, next) => {
            const hlslResult = tryForwardToHlsl(document, position);
            if (hlslResult) {
                const defs = await requestForwarder.forwardDefinition(
                    hlslResult.virtualUri,
                    hlslResult.virtualPosition,
                );
                if (defs && defs.length > 0) {
                    return mapDefinitionResults(defs, document.uri, hlslResult.virtualUri);
                }
            }
            return next(document, position, token);
        },
    };

    // 6. 创建 Language Client
    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'stoy' }],
        middleware,
    };

    client = new LanguageClient(
        'stoyLanguageServer',
        'Stoy Language Server',
        serverOptions,
        clientOptions,
    );

    client.start().then(() => {
        outputChannel.appendLine('[Stoy] Language Server started successfully');
    }, (err) => {
        outputChannel.appendLine(`[Stoy] Language Server failed to start: ${err}`);
    });

    outputChannel.appendLine('[Stoy] Extension activated');
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) return undefined;
    return client.stop();
}

// ============================================================
// Helper: 检查是否在 HLSL 区域并返回虚拟文档信息
// ============================================================

function tryForwardToHlsl(document: vscode.TextDocument, position: vscode.Position): {
    virtualUri: string;
    virtualPosition: vscode.Position;
} | null {
    const physicalUri = document.uri.toString();
    const doc = virtualDocProvider.getStoyDocument(physicalUri);
    if (!doc) {
        virtualDocProvider.invalidate(physicalUri, document.getText());
        const freshDoc = virtualDocProvider.getStoyDocument(physicalUri);
        if (!freshDoc) return null;
        return requestForwarder.findHlslBlock(freshDoc, physicalUri, position.line, position.character);
    }
    return requestForwarder.findHlslBlock(doc, physicalUri, position.line, position.character);
}

// ============================================================
// B5: 映射补全结果中的 textEdit.range
// ============================================================

function mapCompletionResult(
    result: vscode.CompletionItem[] | vscode.CompletionList,
    virtualUri: string,
): vscode.CompletionItem[] | vscode.CompletionList {
    // CompletionList
    if ('items' in result) {
        for (const item of result.items) {
            mapCompletionItemRanges(item, virtualUri);
        }
        return result;
    }
    // CompletionItem[]
    for (const item of result) {
        mapCompletionItemRanges(item, virtualUri);
    }
    return result;
}

function mapCompletionItemRanges(item: vscode.CompletionItem, virtualUri: string): void {
    if (item.textEdit && 'range' in item.textEdit) {
        const mapped = mapRange(item.textEdit.range, virtualUri);
        if (mapped) {
            (item.textEdit as vscode.TextEdit).range = mapped;
        }
    }
    if (item.additionalTextEdits) {
        for (const edit of item.additionalTextEdits) {
            const mapped = mapRange(edit.range, virtualUri);
            if (mapped) {
                edit.range = mapped;
            }
        }
    }
}

// ============================================================
// B6: 映射 definition 结果位置（完整 start + end）
// ============================================================

function mapDefinitionResults(
    defs: (vscode.Location | vscode.LocationLink)[],
    physicalUri: vscode.Uri,
    virtualUri: string,
): vscode.Location[] {
    const results: vscode.Location[] = [];
    for (const def of defs) {
        if ('targetUri' in def) {
            const link = def as vscode.LocationLink;
            const targetUriStr = link.targetUri.toString();
            if (targetUriStr.startsWith(VIRTUAL_SCHEME)) {
                const mappedRange = mapRange(link.targetRange, targetUriStr);
                if (mappedRange) {
                    results.push(new vscode.Location(physicalUri, mappedRange));
                }
            } else {
                results.push(new vscode.Location(link.targetUri, link.targetRange));
            }
        } else {
            const loc = def as vscode.Location;
            const locUriStr = loc.uri.toString();
            if (locUriStr.startsWith(VIRTUAL_SCHEME)) {
                const mappedRange = mapRange(loc.range, locUriStr);
                if (mappedRange) {
                    results.push(new vscode.Location(physicalUri, mappedRange));
                }
            } else {
                results.push(loc);
            }
        }
    }
    return results;
}

// ============================================================
// Helper: 映射虚拟文档 Range 到物理文件 Range
// ============================================================

function mapRange(range: vscode.Range, virtualUri: string): vscode.Range | null {
    const mappedStart = requestForwarder.mapVirtualPositionToPhysical(
        virtualUri, range.start.line, range.start.character,
    );
    const mappedEnd = requestForwarder.mapVirtualPositionToPhysical(
        virtualUri, range.end.line, range.end.character,
    );
    if (!mappedStart || !mappedEnd) return null;
    return new vscode.Range(
        new vscode.Position(mappedStart.line, mappedStart.character),
        new vscode.Position(mappedEnd.line, mappedEnd.character),
    );
}
