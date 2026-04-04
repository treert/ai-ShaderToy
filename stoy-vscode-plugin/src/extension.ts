// ============================================================
// Extension Entry — .stoy Language 扩展主入口
// 启动 Language Client 连接 Language Server。
// 所有智能提示（DSL + HLSL）均在 Language Server 内处理。
// ============================================================

import * as path from 'path';
import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
} from 'vscode-languageclient/node';

let client: LanguageClient;

const outputChannel = vscode.window.createOutputChannel('Stoy Language');

export function activate(context: vscode.ExtensionContext) {
    outputChannel.appendLine('[Stoy] Extension activating...');

    // Language Server 配置
    const serverModule = context.asAbsolutePath(path.join('out', 'server.js'));
    const serverOptions: ServerOptions = {
        run: { module: serverModule, transport: TransportKind.ipc },
        debug: { module: serverModule, transport: TransportKind.ipc, options: { execArgv: ['--nolazy', '--inspect=6009'] } },
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'stoy' }],
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
