// ============================================================
// Language Server — .stoy Language Server 主入口
// DSL 区域 → DSL providers，HLSL 区域 → HLSL providers
// ============================================================

import {
    createConnection,
    ProposedFeatures,
    TextDocuments,
    TextDocumentSyncKind,
    InitializeParams,
    InitializeResult,
    CompletionParams,
    HoverParams,
    DefinitionParams,
} from 'vscode-languageserver/node';

import { TextDocument } from 'vscode-languageserver-textdocument';
import { DocumentManager } from './documentManager';
import { provideDslCompletions } from './dslCompletionProvider';
import { provideDslHover } from './dslHoverProvider';
import { provideDslDefinition } from './dslDefinitionProvider';
import { provideDslDiagnostics } from './dslDiagnosticsProvider';
import { provideHlslHover } from './hlslHoverProvider';
import { provideHlslCompletions } from './hlslCompletionProvider';
import { provideHlslDefinition } from './hlslDefinitionProvider';
import { StoyDocument } from '../types';
import { scanHlslSymbols, HlslSymbol } from '../hlslSymbolScanner';

// 创建 LSP 连接
const connection = createConnection(ProposedFeatures.all);

// 文档管理器（LSP 标准）
const documents = new TextDocuments(TextDocument);

// .stoy 文档解析缓存
const docManager = new DocumentManager();

// HLSL 符号缓存（每次文档变更时重新扫描）
const symbolCache = new Map<string, HlslSymbol[]>();

// HLSL provider 模式（通过 workspace/configuration 获取）
let hlslProviderMode: string = 'builtin';

async function refreshHlslProviderMode(): Promise<void> {
    try {
        const config = await connection.workspace.getConfiguration('stoy.hlsl');
        if (config && config.provider) {
            hlslProviderMode = config.provider;
            connection.console.log(`[Stoy Server] HLSL provider mode: ${hlslProviderMode}`);
        }
    } catch {
        // 配置获取失败时保持当前值
    }
}

// ---- 初始化 ----

connection.onInitialize((_params: InitializeParams): InitializeResult => {
    connection.console.log('[Stoy Server] Initializing...');
    return {
        capabilities: {
            textDocumentSync: TextDocumentSyncKind.Full,
            completionProvider: {
                triggerCharacters: ['.', '"', '=', '{'],
                resolveProvider: false,
            },
            hoverProvider: true,
            definitionProvider: true,
        },
    };
});

connection.onInitialized(async () => {
    await refreshHlslProviderMode();
});

// 监听配置变更
connection.onDidChangeConfiguration(async () => {
    await refreshHlslProviderMode();
});

// ---- 文档变更 → 解析 + 符号扫描 + 诊断 ----

documents.onDidChangeContent(change => {
    const uri = change.document.uri;
    const text = change.document.getText();

    // 立即解析 + 更新符号缓存
    const doc = docManager.parseImmediate(uri, text);
    symbolCache.set(uri, scanHlslSymbols(doc));

    // 防抖推送诊断（只做诊断，不重复扫描符号）
    docManager.onDocumentChanged(uri, text, (d) => {
        const diagnostics = provideDslDiagnostics(d);
        connection.sendDiagnostics({ uri, diagnostics });
    });
});

documents.onDidClose(e => {
    docManager.removeDocument(e.document.uri);
    symbolCache.delete(e.document.uri);
    connection.sendDiagnostics({ uri: e.document.uri, diagnostics: [] });
});

// ---- 补全 ----

connection.onCompletion((params: CompletionParams) => {
    const textDoc = documents.get(params.textDocument.uri);
    if (!textDoc) return [];

    const doc = docManager.getDocument(params.textDocument.uri, textDoc.getText());

    if (isInHlslBlock(doc, params.position.line)) {
        // externalLsp 模式下不由 server 处理 HLSL，由客户端 middleware 转发
        if (hlslProviderMode === 'externalLsp') return [];
        const symbols = symbolCache.get(params.textDocument.uri) ?? [];
        return provideHlslCompletions(doc, textDoc, params.position, symbols);
    }

    return provideDslCompletions(doc, textDoc, params.position);
});

// ---- 悬停 ----

connection.onHover((params: HoverParams) => {
    const textDoc = documents.get(params.textDocument.uri);
    if (!textDoc) return null;

    const doc = docManager.getDocument(params.textDocument.uri, textDoc.getText());

    if (isInHlslBlock(doc, params.position.line)) {
        if (hlslProviderMode === 'externalLsp') return null;
        const symbols = symbolCache.get(params.textDocument.uri) ?? [];
        return provideHlslHover(doc, textDoc, params.position, symbols);
    }

    return provideDslHover(doc, textDoc, params.position);
});

// ---- 跳转定义 ----

connection.onDefinition((params: DefinitionParams) => {
    const textDoc = documents.get(params.textDocument.uri);
    if (!textDoc) return null;

    const doc = docManager.getDocument(params.textDocument.uri, textDoc.getText());

    if (isInHlslBlock(doc, params.position.line)) {
        if (hlslProviderMode === 'externalLsp') return null;
        const symbols = symbolCache.get(params.textDocument.uri) ?? [];
        return provideHlslDefinition(doc, textDoc, params.position, symbols);
    }

    return provideDslDefinition(doc, textDoc, params.position);
});

// ---- 启动 ----

documents.listen(connection);
connection.listen();

// ============================================================
// Helper: 判断行号是否在 HLSL 代码块内
// ============================================================

function isInHlslBlock(doc: StoyDocument, line: number): boolean {
    // common 块
    if (doc.common) {
        const cr = doc.common.codeRange;
        if (line >= cr.startLine && line <= cr.endLine) return true;
    }

    // pass code 块
    for (const pass of doc.passes) {
        if (pass.codeRange.startLine > 0) {
            const cr = pass.codeRange;
            if (line >= cr.startLine && line <= cr.endLine) return true;
        }
    }

    return false;
}
