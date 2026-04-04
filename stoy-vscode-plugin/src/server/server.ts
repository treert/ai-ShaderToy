// ============================================================
// Language Server — .stoy Language Server 主入口
// 创建 LSP Connection，注册各类处理器。
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

// 创建 LSP 连接
const connection = createConnection(ProposedFeatures.all);

// 文档管理器（LSP 标准）
const documents = new TextDocuments(TextDocument);

// .stoy 文档解析缓存
const docManager = new DocumentManager();

// ---- 初始化 ----

connection.onInitialize((params: InitializeParams): InitializeResult => {
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

// ---- 文档变更 → 诊断 ----

documents.onDidChangeContent(change => {
    const uri = change.document.uri;
    const text = change.document.getText();

    docManager.onDocumentChanged(uri, text, (doc) => {
        const diagnostics = provideDslDiagnostics(doc);
        connection.sendDiagnostics({ uri, diagnostics });
    });
});

documents.onDidClose(e => {
    docManager.removeDocument(e.document.uri);
    connection.sendDiagnostics({ uri: e.document.uri, diagnostics: [] });
});

// ---- 补全 ----

connection.onCompletion((params: CompletionParams) => {
    const textDoc = documents.get(params.textDocument.uri);
    if (!textDoc) return [];

    const doc = docManager.getDocument(params.textDocument.uri, textDoc.getText());
    return provideDslCompletions(doc, textDoc, params.position);
});

// ---- 悬停 ----

connection.onHover((params: HoverParams) => {
    const textDoc = documents.get(params.textDocument.uri);
    if (!textDoc) return null;

    const doc = docManager.getDocument(params.textDocument.uri, textDoc.getText());
    return provideDslHover(doc, textDoc, params.position);
});

// ---- 跳转定义 ----

connection.onDefinition((params: DefinitionParams) => {
    const textDoc = documents.get(params.textDocument.uri);
    if (!textDoc) return null;

    const doc = docManager.getDocument(params.textDocument.uri, textDoc.getText());
    return provideDslDefinition(doc, textDoc, params.position);
});

// ---- 启动 ----

documents.listen(connection);
connection.listen();
