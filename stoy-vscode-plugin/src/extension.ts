// ============================================================
// Extension Entry — .stoy Language 扩展主入口
// 启动 Language Client 连接 Language Server。
// 根据 stoy.hlsl.provider 配置选择 HLSL 智能提示方案：
//   - builtin:        所有智能提示均在 Language Server 内处理
//   - externalLsp:    HLSL 请求通过虚拟文档转发给外部 HLSL 扩展
//   - fake_file_lsp: 通过假 file:// URI 转发给 shader-language-server
// ============================================================

import * as path from 'path';
import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
} from 'vscode-languageclient/node';
import { VirtualDocProvider, VIRTUAL_SCHEME } from './virtualDocProvider';
import { RequestForwarder } from './requestForwarder';
import { StoyParser } from './stoyParser';
import { FakeFileLspManager } from './fakeFileLsp';
import { Logger } from './logger';
import { classifySymbol, shouldPreferBuiltinForHover, shouldPreferBuiltinForDefinition, shouldPreferBuiltinForCompletion } from './symbolClassifier';

let client: LanguageClient;
let fakeFileLspManager: FakeFileLspManager | undefined;
let logger: Logger;

const outputChannel = vscode.window.createOutputChannel('Stoy Language');

export function activate(context: vscode.ExtensionContext) {
    // Initialize logger with storage path for log files
    const storagePath = context.globalStorageUri.fsPath;
    logger = new Logger(outputChannel, storagePath);
    logger.cleanOldLogs(10);
    context.subscriptions.push({ dispose: () => logger.dispose() });

    logger.info('[Stoy] Extension activating...');

    const config = vscode.workspace.getConfiguration('stoy.hlsl');
    const providerMode: string = config.get<string>('provider', 'builtin');
    logger.info(`[Stoy] HLSL provider mode: ${providerMode}`);

    // Language Server 配置
    const serverModule = context.asAbsolutePath(path.join('out', 'server.js'));
    const serverOptions: ServerOptions = {
        run: { module: serverModule, transport: TransportKind.ipc },
        debug: { module: serverModule, transport: TransportKind.ipc, options: { execArgv: ['--nolazy', '--inspect=6009'] } },
    };

    // 根据模式构建 clientOptions
    let clientOptions: LanguageClientOptions;

    if (providerMode === 'externalLsp') {
        // --- externalLsp 模式：注册虚拟文档 + middleware ---
        const virtualDocProvider = new VirtualDocProvider();
        const requestForwarder = new RequestForwarder(virtualDocProvider);
        const stoyParser = new StoyParser();

        // 注册虚拟文档内容提供者
        context.subscriptions.push(
            vscode.workspace.registerTextDocumentContentProvider(VIRTUAL_SCHEME, virtualDocProvider),
        );

        // 监听 .stoy 文件变更，同步虚拟文档缓存
        context.subscriptions.push(
            vscode.workspace.onDidChangeTextDocument(e => {
                if (e.document.languageId === 'stoy') {
                    const uri = e.document.uri.toString();
                    virtualDocProvider.invalidate(uri, e.document.getText());
                    requestForwarder.invalidateOpened(uri);
                }
            }),
        );

        // 打开 .stoy 文件时初始化虚拟文档缓存
        context.subscriptions.push(
            vscode.workspace.onDidOpenTextDocument(doc => {
                if (doc.languageId === 'stoy') {
                    virtualDocProvider.invalidate(doc.uri.toString(), doc.getText());
                }
            }),
        );

        // 初始化已打开的 .stoy 文件
        for (const doc of vscode.workspace.textDocuments) {
            if (doc.languageId === 'stoy') {
                virtualDocProvider.invalidate(doc.uri.toString(), doc.getText());
            }
        }

        clientOptions = {
            documentSelector: [{ scheme: 'file', language: 'stoy' }],
            middleware: {
                provideCompletionItem: async (document, position, context, token, next) => {
                    const result = await tryForwardToExternalLsp(
                        document, position, stoyParser, requestForwarder,
                        async (virtualUri, virtualPos) => {
                            const items = await requestForwarder.forwardCompletion(virtualUri, virtualPos, context.triggerCharacter);
                            if (!items) return null;
                            // CompletionList → 检查是否有实际结果
                            if ('items' in items) {
                                if (items.items.length === 0) return null; // 空列表 → 回退到 server
                                return mapCompletionListRanges(items, virtualUri, requestForwarder);
                            }
                            // CompletionItem[] 的情况
                            if (Array.isArray(items) && items.length === 0) return null;
                            return items;
                        },
                    );
                    if (result !== undefined) return result;
                    return next(document, position, context, token);
                },

                provideHover: async (document, position, token, next) => {
                    const result = await tryForwardToExternalLsp(
                        document, position, stoyParser, requestForwarder,
                        async (virtualUri, virtualPos) => {
                            const hovers = await requestForwarder.forwardHover(virtualUri, virtualPos);
                            if (!hovers || hovers.length === 0) return null;
                            // 映射 hover range 回物理位置
                            return mapHoverRange(hovers[0], virtualUri, requestForwarder);
                        },
                    );
                    if (result !== undefined) return result;
                    return next(document, position, token);
                },

                provideDefinition: async (document, position, token, next) => {
                    const result = await tryForwardToExternalLsp(
                        document, position, stoyParser, requestForwarder,
                        async (virtualUri, virtualPos) => {
                            const locs = await requestForwarder.forwardDefinition(virtualUri, virtualPos);
                            if (!locs || locs.length === 0) return null;
                            // 虚拟文档内的定义位置需要映射回物理位置
                            return mapDefinitionLocations(locs, document.uri, requestForwarder);
                        },
                    );
                    if (result !== undefined) return result;
                    return next(document, position, token);
                },
            },
        };

        logger.info('[Stoy] externalLsp mode: virtual document provider and middleware registered');
    } else if (providerMode === 'fake_file_lsp') {
        // --- fake_file_lsp 模式：通过假 file:// URI 转发给 shader-language-server ---
        fakeFileLspManager = new FakeFileLspManager(logger);

        // Start the second LSP (shader-language-server) asynchronously
        fakeFileLspManager.activate(context).then(success => {
            if (success) {
                logger.info('[Stoy] fake_file_lsp mode: shader-language-server activated');
                // Sync already-opened .stoy files
                for (const doc of vscode.workspace.textDocuments) {
                    if (doc.languageId === 'stoy') {
                        fakeFileLspManager!.syncOpen(doc.uri.toString(), doc.getText());
                    }
                }
            } else {
                logger.info('[Stoy] fake_file_lsp mode: degraded to builtin HLSL only');
            }
        });

        // Listen for .stoy file open
        context.subscriptions.push(
            vscode.workspace.onDidOpenTextDocument(doc => {
                if (doc.languageId === 'stoy' && fakeFileLspManager?.ready) {
                    fakeFileLspManager.syncOpen(doc.uri.toString(), doc.getText());
                }
            }),
        );

        // Listen for .stoy file changes
        context.subscriptions.push(
            vscode.workspace.onDidChangeTextDocument(e => {
                if (e.document.languageId === 'stoy' && fakeFileLspManager?.ready) {
                    fakeFileLspManager.syncChange(e.document.uri.toString(), e.document.getText());
                }
            }),
        );

        // Listen for .stoy file close
        context.subscriptions.push(
            vscode.workspace.onDidCloseTextDocument(doc => {
                if (doc.languageId === 'stoy' && fakeFileLspManager) {
                    fakeFileLspManager.syncClose(doc.uri.toString());
                }
            }),
        );

        // Middleware: forward HLSL requests to shader-language-server via sendRequest
        // with priority mixing — prefer builtin for known symbols, SLS for user-defined
        const mgr = fakeFileLspManager;
        clientOptions = {
            documentSelector: [{ scheme: 'file', language: 'stoy' }],
            middleware: {
                provideCompletionItem: async (document, position, context, token, next) => {
                    // Completion: merge SLS + builtin results
                    const stoyUri = document.uri.toString();
                    const stoyDoc = mgr.getStoyDocument(stoyUri);
                    const block = mgr.ready ? mgr.findHlslBlock(stoyUri, position.line, position.character) : null;

                    // Request both sources in parallel when possible
                    const slsPromise = block
                        ? mgr.sendCompletionRequest(block.fakeUri, block.virtualPosition, context.triggerCharacter)
                            .catch(() => null)
                        : Promise.resolve(null);
                    const builtinPromise = next(document, position, context, token)
                        .then(r => r as vscode.CompletionList | vscode.CompletionItem[] | null | undefined)
                        .catch(() => null);

                    const [slsResult, builtinResult] = await Promise.all([slsPromise, builtinPromise]);

                    // Normalize both to CompletionItem[]
                    const slsItems = slsResult?.items ?? [];
                    const builtinItems = normalizeCompletionResult(builtinResult);

                    if (slsItems.length === 0) {
                        logger.debug(`[Stoy][completion-mix] SLS empty, using builtin (${builtinItems.length} items)`);
                        return builtinItems.length > 0 ? new vscode.CompletionList(builtinItems, false) : builtinResult;
                    }
                    if (builtinItems.length === 0) {
                        logger.debug(`[Stoy][completion-mix] builtin empty, using SLS (${slsItems.length} items)`);
                        return slsResult;
                    }

                    // Merge: for duplicate labels, prefer builtin for known symbols, SLS for user-defined
                    const merged = mergeCompletionItems(slsItems, builtinItems, stoyDoc);
                    logger.debug(`[Stoy][completion-mix] merged: SLS=${slsItems.length}, builtin=${builtinItems.length} → ${merged.length}`);
                    return new vscode.CompletionList(merged, false);
                },

                provideHover: async (document, position, token, next) => {
                    const word = getWordAtCursor(document, position);
                    const stoyUri = document.uri.toString();
                    const stoyDoc = mgr.getStoyDocument(stoyUri);
                    const category = word ? classifySymbol(word, stoyDoc) : undefined;
                    const preferBuiltin = category ? shouldPreferBuiltinForHover(category) : false;

                    logger.debug(`[Stoy][hover-mix] word="${word}", category=${category}, preferBuiltin=${preferBuiltin}`);

                    if (preferBuiltin) {
                        // Try builtin first
                        const builtinResult = await next(document, position, token);
                        if (builtinResult) {
                            logger.debug(`[Stoy][hover-mix] using builtin result for "${word}"`);
                            return builtinResult;
                        }
                        // Fallback to SLS
                        logger.debug(`[Stoy][hover-mix] builtin null for "${word}", falling back to SLS`);
                    }

                    // Try SLS (default path, or fallback from builtin)
                    const slsResult = await tryForwardToFakeFileLsp(
                        document, position, mgr,
                        async (fakeUri, virtualPos) => {
                            const hover = await mgr.sendHoverRequest(fakeUri, virtualPos);
                            if (!hover) return null;
                            return mapFakeHoverRange(hover, fakeUri, mgr);
                        },
                    );
                    if (slsResult !== undefined) {
                        logger.debug(`[Stoy][hover-mix] using SLS result for "${word}"`);
                        return slsResult;
                    }

                    // Final fallback to builtin (for non-preferBuiltin path)
                    if (!preferBuiltin) {
                        logger.debug(`[Stoy][hover-mix] SLS null for "${word}", falling back to builtin`);
                        return next(document, position, token);
                    }
                    return null;
                },

                provideDefinition: async (document, position, token, next) => {
                    const word = getWordAtCursor(document, position);
                    const stoyUri = document.uri.toString();
                    const stoyDoc = mgr.getStoyDocument(stoyUri);
                    const category = word ? classifySymbol(word, stoyDoc) : undefined;
                    const preferBuiltin = category ? shouldPreferBuiltinForDefinition(category) : false;

                    logger.debug(`[Stoy][def-mix] word="${word}", category=${category}, preferBuiltin=${preferBuiltin}`);

                    if (preferBuiltin) {
                        // Try builtin first (jumps to inner_vars / texture / pass declarations)
                        const builtinResult = await next(document, position, token);
                        if (builtinResult) {
                            logger.debug(`[Stoy][def-mix] using builtin result for "${word}"`);
                            return builtinResult;
                        }
                        logger.debug(`[Stoy][def-mix] builtin null for "${word}", falling back to SLS`);
                    }

                    // Try SLS
                    const slsResult = await tryForwardToFakeFileLsp(
                        document, position, mgr,
                        async (fakeUri, virtualPos) => {
                            const locs = await mgr.sendDefinitionRequest(fakeUri, virtualPos);
                            if (!locs || locs.length === 0) return null;
                            return mapFakeDefinitionLocations(locs, document.uri, mgr);
                        },
                    );
                    if (slsResult !== undefined) {
                        logger.debug(`[Stoy][def-mix] using SLS result for "${word}"`);
                        return slsResult;
                    }

                    // Final fallback to builtin (for non-preferBuiltin path)
                    if (!preferBuiltin) {
                        logger.debug(`[Stoy][def-mix] SLS null for "${word}", falling back to builtin`);
                        return next(document, position, token);
                    }
                    return null;
                },
            },
        };

        logger.info('[Stoy] fake_file_lsp mode: middleware registered, starting shader-language-server...');
    } else {
        // --- builtin 模式：无 middleware，全部由 server 处理 ---
        clientOptions = {
            documentSelector: [{ scheme: 'file', language: 'stoy' }],
        };
    }

    // Merge outputChannel into clientOptions so all logs go to "Stoy Language" panel
    clientOptions.outputChannel = logger.getOutputChannel();

    client = new LanguageClient(
        'stoyLanguageServer',
        'Stoy Language Server',
        serverOptions,
        clientOptions,
    );

    client.start().then(() => {
        logger.info('[Stoy] Language Server started successfully');
    }, (err) => {
        logger.error(`[Stoy] Language Server failed to start: ${err}`);
    });

    // 监听配置变更，提示重载窗口
    context.subscriptions.push(
        vscode.workspace.onDidChangeConfiguration(e => {
            if (e.affectsConfiguration('stoy.hlsl.provider')) {
                vscode.window.showInformationMessage(
                    'Stoy: HLSL provider 配置已变更，需要重载窗口才能生效。',
                    '重载窗口',
                ).then(selection => {
                    if (selection === '重载窗口') {
                        vscode.commands.executeCommand('workbench.action.reloadWindow');
                    }
                });
            }
        }),
    );

    logger.info('[Stoy] Extension activated');
}

export async function deactivate(): Promise<void> {
    const promises: Promise<void>[] = [];
    if (client) {
        promises.push(client.stop());
    }
    if (fakeFileLspManager) {
        promises.push(fakeFileLspManager.deactivate());
        fakeFileLspManager = undefined;
    }
    await Promise.all(promises);
}

// ============================================================
// Helper: 尝试将请求转发给外部 HLSL 扩展
// ============================================================

async function tryForwardToExternalLsp<T>(
    document: vscode.TextDocument,
    position: vscode.Position,
    parser: StoyParser,
    forwarder: RequestForwarder,
    forward: (virtualUri: string, virtualPos: vscode.Position) => Promise<T | null>,
): Promise<T | null | undefined> {
    if (document.languageId !== 'stoy') return undefined;

    const text = document.getText();
    const doc = parser.parse(text);
    const physicalUri = document.uri.toString();

    const hlslBlock = forwarder.findHlslBlock(doc, physicalUri, position.line, position.character);
    if (!hlslBlock) return undefined; // 不在 HLSL 块内，透传给 server

    const result = await forward(hlslBlock.virtualUri, hlslBlock.virtualPosition);
    // 外部扩展有结果 → 使用外部结果；无结果 → 返回 undefined 回退到 server 内置方案
    return result ?? undefined;
}

// ============================================================
// Helper: 映射外部扩展返回的位置回物理文档
// ============================================================

function mapCompletionListRanges(
    list: vscode.CompletionList,
    _virtualUri: string,
    _forwarder: RequestForwarder,
): vscode.CompletionList {
    // CompletionItem 的 range 通常由 VSCode 自动处理，无需手动映射
    return list;
}

function mapHoverRange(
    hover: vscode.Hover,
    virtualUri: string,
    forwarder: RequestForwarder,
): vscode.Hover {
    if (hover.range) {
        const startMapped = forwarder.mapVirtualPositionToPhysical(
            virtualUri, hover.range.start.line, hover.range.start.character,
        );
        const endMapped = forwarder.mapVirtualPositionToPhysical(
            virtualUri, hover.range.end.line, hover.range.end.character,
        );
        if (startMapped && endMapped) {
            return new vscode.Hover(
                hover.contents,
                new vscode.Range(startMapped.line, startMapped.character, endMapped.line, endMapped.character),
            );
        }
    }
    return hover;
}

function mapDefinitionLocations(
    locs: (vscode.Location | vscode.LocationLink)[],
    physicalDocUri: vscode.Uri,
    forwarder: RequestForwarder,
): vscode.Definition | vscode.DefinitionLink[] {
    return locs.map(loc => {
        if (loc instanceof vscode.Location) {
            // 如果定义在虚拟文档中，映射回物理文件
            if (loc.uri.scheme === VIRTUAL_SCHEME) {
                const mapped = forwarder.mapVirtualPositionToPhysical(
                    loc.uri.toString(), loc.range.start.line, loc.range.start.character,
                );
                if (mapped) {
                    return new vscode.Location(
                        physicalDocUri,
                        new vscode.Range(mapped.line, mapped.character, mapped.line, mapped.character),
                    );
                }
            }
            return loc;
        }
        // LocationLink → 映射 targetUri 为虚拟 scheme 的情况
        if (loc.targetUri.scheme === VIRTUAL_SCHEME) {
            const mapped = forwarder.mapVirtualPositionToPhysical(
                loc.targetUri.toString(), loc.targetRange.start.line, loc.targetRange.start.character,
            );
            if (mapped) {
                return new vscode.Location(
                    physicalDocUri,
                    new vscode.Range(mapped.line, mapped.character, mapped.line, mapped.character),
                );
            }
        }
        // 非虚拟文档的 LocationLink 转为 Location
        return new vscode.Location(loc.targetUri, loc.targetRange);
    });
}

// ============================================================
// Helper: fake_file_lsp 模式 — 尝试将请求转发给 shader-language-server
// ============================================================

async function tryForwardToFakeFileLsp<T>(
    document: vscode.TextDocument,
    position: vscode.Position,
    mgr: FakeFileLspManager,
    forward: (fakeUri: string, virtualPos: vscode.Position) => Promise<T | null>,
): Promise<T | null | undefined> {
    if (document.languageId !== 'stoy') {
        logger.debug(`[Stoy][middleware] skip: not stoy (lang=${document.languageId})`);
        return undefined;
    }
    if (!mgr.ready) {
        logger.debug('[Stoy][middleware] skip: mgr not ready');
        return undefined;
    }

    const stoyUri = document.uri.toString();
    const block = mgr.findHlslBlock(stoyUri, position.line, position.character);
    if (!block) {
        logger.debug(`[Stoy][middleware] skip: not in HLSL block (line=${position.line}, char=${position.character})`);
        return undefined; // Not in HLSL block → pass through to our server
    }

    logger.debug(`[Stoy][middleware] forwarding to fakeUri=${block.fakeUri}, virtualPos=(${block.virtualPosition.line},${block.virtualPosition.character})`);

    try {
        const result = await forward(block.fakeUri, block.virtualPosition);
        logger.debug(`[Stoy][middleware] forward result: ${result !== null && result !== undefined ? 'GOT RESULT' : 'null/undefined → fallback'}`);
        return result ?? undefined;
    } catch (err) {
        logger.error(`[Stoy][middleware] forward error: ${err}`);
        return undefined; // Silent fallback to builtin
    }
}

// ============================================================
// Helper: get word under cursor from a TextDocument
// ============================================================

function getWordAtCursor(document: vscode.TextDocument, position: vscode.Position): string | undefined {
    const range = document.getWordRangeAtPosition(position);
    return range ? document.getText(range) : undefined;
}

// ============================================================
// Helper: normalize completion result to CompletionItem[]
// ============================================================

function normalizeCompletionResult(
    result: vscode.CompletionList | vscode.CompletionItem[] | null | undefined,
): vscode.CompletionItem[] {
    if (!result) return [];
    if (Array.isArray(result)) return result;
    if ('items' in result) return result.items;
    return [];
}

// ============================================================
// Helper: merge SLS and builtin completion items
// For duplicate labels, prefer builtin for known symbols, SLS for user-defined
// ============================================================

function mergeCompletionItems(
    slsItems: vscode.CompletionItem[],
    builtinItems: vscode.CompletionItem[],
    stoyDoc: import('./types').StoyDocument | undefined,
): vscode.CompletionItem[] {
    // Build a map of builtin items by label
    const builtinMap = new Map<string, vscode.CompletionItem>();
    for (const item of builtinItems) {
        const label = typeof item.label === 'string' ? item.label : item.label.label;
        builtinMap.set(label, item);
    }

    // Build a map of SLS items by label
    const slsMap = new Map<string, vscode.CompletionItem>();
    for (const item of slsItems) {
        const label = typeof item.label === 'string' ? item.label : item.label.label;
        slsMap.set(label, item);
    }

    const merged = new Map<string, vscode.CompletionItem>();

    // Add all SLS items first
    for (const [label, item] of slsMap) {
        merged.set(label, item);
    }

    // Override with builtin items for known symbols, add new builtin-only items
    for (const [label, item] of builtinMap) {
        if (shouldPreferBuiltinForCompletion(label, stoyDoc) || !slsMap.has(label)) {
            merged.set(label, item);
        }
    }

    return Array.from(merged.values());
}

// ============================================================
// Helper: fake_file_lsp 模式 — 映射 hover range 回物理文档
// ============================================================

function mapFakeHoverRange(
    hover: vscode.Hover,
    fakeUri: string,
    mgr: FakeFileLspManager,
): vscode.Hover {
    if (hover.range) {
        const startMapped = mgr.mapVirtualToPhysical(
            fakeUri, hover.range.start.line, hover.range.start.character,
        );
        const endMapped = mgr.mapVirtualToPhysical(
            fakeUri, hover.range.end.line, hover.range.end.character,
        );
        if (startMapped && endMapped) {
            return new vscode.Hover(
                hover.contents,
                new vscode.Range(startMapped.line, startMapped.character, endMapped.line, endMapped.character),
            );
        }
    }
    return hover;
}

// ============================================================
// Helper: fake_file_lsp 模式 — 映射 definition locations 回物理文档
// ============================================================

function mapFakeDefinitionLocations(
    locs: (vscode.Location | vscode.LocationLink)[],
    physicalDocUri: vscode.Uri,
    mgr: FakeFileLspManager,
): vscode.Definition | vscode.DefinitionLink[] {
    return locs.map(loc => {
        if (loc instanceof vscode.Location) {
            // If definition is in a fake URI, map back to physical .stoy file
            if (mgr.fakeUris.isFakeUri(loc.uri.toString())) {
                const mapped = mgr.mapVirtualToPhysical(
                    loc.uri.toString(), loc.range.start.line, loc.range.start.character,
                );
                if (mapped) {
                    return new vscode.Location(
                        physicalDocUri,
                        new vscode.Range(mapped.line, mapped.character, mapped.line, mapped.character),
                    );
                }
                // Definition in injected code region → drop it
                return null;
            }
            return loc;
        }
        // LocationLink
        if (mgr.fakeUris.isFakeUri(loc.targetUri.toString())) {
            const mapped = mgr.mapVirtualToPhysical(
                loc.targetUri.toString(), loc.targetRange.start.line, loc.targetRange.start.character,
            );
            if (mapped) {
                return new vscode.Location(
                    physicalDocUri,
                    new vscode.Range(mapped.line, mapped.character, mapped.line, mapped.character),
                );
            }
            return null;
        }
        return new vscode.Location(loc.targetUri, loc.targetRange);
    }).filter((loc): loc is vscode.Location => loc !== null);
}
