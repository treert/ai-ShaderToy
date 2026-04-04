// ============================================================
// Fake File LSP Manager — fake_file_lsp mode core module
// Manages a second LanguageClient connecting to shader-language-server
// via fake file:// URIs for HLSL intelligence (diagnostics, hover, etc.)
// ============================================================

import * as path from 'path';
import * as fs from 'fs';
import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
    Executable,
} from 'vscode-languageclient/node';
import * as lsp from 'vscode-languageserver-protocol';
import { StoyParser } from './stoyParser';
import { StoyDocument, BlockRange } from './types';
import { generatePassVirtualDoc, generateCommonVirtualDoc, HlslVirtualDoc } from './hlslGenerator';
import { FakeUriManager, FakeUriInfo } from './fakeUriManager';
import { physicalToVirtual, virtualToPhysical, isInBlockRange } from './positionMapper';
import { Logger } from './logger';

/** Per-block tracking info for fake documents */
interface FakeDocState {
    fakeUri: string;
    blockType: 'common' | 'pass';
    blockName: string;
    prefixLineCount: number;
    blockRange: BlockRange;
    version: number;
}

/**
 * Manages the second LanguageClient (shader-language-server) for fake_file_lsp mode.
 *
 * Responsibilities:
 * - Find and start shader-language-server binary
 * - Sync fake HLSL documents via didOpen/didChange/didClose
 * - Intercept diagnostics and map line numbers back to .stoy physical file
 * - Provide position mapping helpers for middleware (hover/completion/definition)
 */
export class FakeFileLspManager {
    private client: LanguageClient | undefined;
    private parser = new StoyParser();
    private uriManager = new FakeUriManager();
    private diagnosticCollection: vscode.DiagnosticCollection;
    private logger: Logger;

    /** Map: .stoy URI → Map<fakeUri, FakeDocState> */
    private docStates = new Map<string, Map<string, FakeDocState>>();

    /** Cached parsed StoyDocument per .stoy URI */
    private stoyDocCache = new Map<string, StoyDocument>();

    /** Whether the second LSP client is ready */
    private isReady = false;

    /** Directory for dumping virtual HLSL files in debug mode */
    private virtualHlslDir: string | undefined;

    constructor(logger: Logger) {
        this.logger = logger;
        this.diagnosticCollection = vscode.languages.createDiagnosticCollection('stoy-hlsl-external');

        // In debug mode, create a directory for virtual HLSL dumps
        if (logger.isDebugEnabled) {
            this.virtualHlslDir = path.join(logger.getLogDir(), 'virtual-hlsl');
            if (!fs.existsSync(this.virtualHlslDir)) {
                fs.mkdirSync(this.virtualHlslDir, { recursive: true });
            }
            this.logger.info(`[Stoy] Virtual HLSL dump directory: ${this.virtualHlslDir}`);
        }
    }

    // ============================================================
    // Lifecycle
    // ============================================================

    /**
     * Find shader-language-server binary and start the second LanguageClient.
     * Returns true if successfully started, false if degraded.
     */
    async activate(context: vscode.ExtensionContext): Promise<boolean> {
        const serverPath = this.findServerBinary(context.extensionPath);
        if (!serverPath) {
            this.logger.info(
                '[Stoy] WARNING: shader-language-server binary not found in bundled bin/ directory. ' +
                'Falling back to builtin HLSL provider only.'
            );
            return false;
        }

        this.logger.info(`[Stoy] Found shader-language-server: ${serverPath}`);

        const serverOptions: ServerOptions = {
            command: serverPath,
            transport: TransportKind.stdio,
        } as Executable;

        const clientOptions: LanguageClientOptions = {
            // Use a document selector that won't match any real files,
            // since we manually send didOpen/didChange/didClose for fake URIs.
            documentSelector: [{ scheme: 'file', language: 'hlsl', pattern: '**/*.stoy-virtual.hlsl' }],
            diagnosticCollectionName: 'stoy-hlsl-external-raw',
            outputChannel: this.logger.getOutputChannel(),
            // Intercept diagnostics from shader-language-server
            middleware: {
                handleDiagnostics: (uri, diagnostics, next) => {
                    this.handleExternalDiagnostics(uri, diagnostics);
                    // Don't call next() — we handle diagnostics ourselves via our own collection
                },
            },
        };

        this.client = new LanguageClient(
            'stoyShaderLanguageServer',
            'Stoy Shader Language Server',
            serverOptions,
            clientOptions,
        );

        try {
            await this.client.start();
            this.isReady = true;
            this.logger.info('[Stoy] shader-language-server started successfully');
            return true;
        } catch (err) {
            this.logger.error(`[Stoy] shader-language-server failed to start: ${err}`);
            this.client = undefined;
            return false;
        }
    }

    /**
     * Stop the second LanguageClient and clean up.
     */
    async deactivate(): Promise<void> {
        this.isReady = false;
        if (this.client) {
            try {
                await this.client.stop();
            } catch {
                // Ignore stop errors
            }
            this.client = undefined;
        }
        this.diagnosticCollection.dispose();
        this.uriManager.dispose();
        this.docStates.clear();
        this.stoyDocCache.clear();
    }

    // ============================================================
    // Server binary discovery
    // ============================================================

    private findServerBinary(extensionPath: string): string | null {
        // Determine platform-specific subdirectory and binary name
        let platformDir: string;
        let binaryName: string;

        switch (process.platform) {
            case 'win32':
                platformDir = 'windows';
                binaryName = 'shader-language-server.exe';
                break;
            case 'linux':
                platformDir = 'linux';
                binaryName = 'shader-language-server';
                break;
            default:
                this.logger.error(
                    `[Stoy] Unsupported platform: ${process.platform}. ` +
                    'shader-language-server is only bundled for windows and linux.'
                );
                return null;
        }

        const serverPath = path.join(extensionPath, 'bin', platformDir, binaryName);
        this.logger.info(`[Stoy] Looking for shader-language-server at: ${serverPath}`);

        if (fs.existsSync(serverPath)) {
            return serverPath;
        }

        this.logger.error(
            `[Stoy] shader-language-server binary not found at: ${serverPath}`
        );
        return null;
    }

    // ============================================================
    // Document sync: didOpen / didChange / didClose
    // ============================================================

    /**
     * Called when a .stoy file is opened. Generates and sends fake HLSL documents.
     */
    syncOpen(stoyUri: string, text: string): void {
        if (!this.isReady || !this.client) return;

        const doc = this.parser.parse(text);
        this.stoyDocCache.set(stoyUri, doc);
        this.logger.debug(`[Stoy][sync] syncOpen: ${stoyUri}, common=${!!doc.common}, passes=${doc.passes.length} (${doc.passes.map(p => p.name).join(',')})`);

        const states = new Map<string, FakeDocState>();
        this.docStates.set(stoyUri, states);

        // Generate fake docs for each HLSL block
        this.syncAllBlocks(stoyUri, doc, states, true);
    }

    /**
     * Called when a .stoy file is changed. Re-generates and sends updated fake HLSL documents.
     */
    syncChange(stoyUri: string, text: string): void {
        if (!this.isReady || !this.client) return;

        const doc = this.parser.parse(text);
        this.stoyDocCache.set(stoyUri, doc);

        let states = this.docStates.get(stoyUri);
        if (!states) {
            // First time seeing this file — treat as open
            states = new Map();
            this.docStates.set(stoyUri, states);
            this.syncAllBlocks(stoyUri, doc, states, true);
            return;
        }

        // Close blocks that no longer exist
        const currentBlockNames = new Set<string>();
        if (doc.common) currentBlockNames.add('_common_');
        for (const pass of doc.passes) currentBlockNames.add(pass.name);

        for (const [fakeUri, state] of states) {
            if (!currentBlockNames.has(state.blockName)) {
                this.sendDidClose(fakeUri);
                states.delete(fakeUri);
                this.uriManager.removeOne(stoyUri, fakeUri);
            }
        }

        // Update existing and add new blocks
        this.syncAllBlocks(stoyUri, doc, states, false);
    }

    /**
     * Called when a .stoy file is closed. Sends didClose for all fake documents.
     */
    syncClose(stoyUri: string): void {
        if (!this.client) return;

        const states = this.docStates.get(stoyUri);
        if (states) {
            for (const [fakeUri] of states) {
                this.sendDidClose(fakeUri);
            }
            states.clear();
        }

        this.docStates.delete(stoyUri);
        this.stoyDocCache.delete(stoyUri);
        this.uriManager.removeAll(stoyUri);

        // Clear diagnostics for this .stoy file
        this.diagnosticCollection.delete(vscode.Uri.parse(stoyUri));
        this.pendingDiagnostics.delete(stoyUri);
    }

    /**
     * Sync all HLSL blocks for a .stoy document.
     */
    private syncAllBlocks(
        stoyUri: string,
        doc: StoyDocument,
        states: Map<string, FakeDocState>,
        isOpen: boolean,
    ): void {
        // Common block
        if (doc.common && doc.common.code.trim()) {
            const fakeUri = this.uriManager.buildFakeUri(stoyUri, 'common', '_common_');
            const vdoc = generateCommonVirtualDoc(doc);
            this.syncOneBlock(stoyUri, fakeUri, 'common', '_common_', vdoc, doc.common.codeRange, states, isOpen);
        }

        // Pass blocks
        for (let i = 0; i < doc.passes.length; i++) {
            const pass = doc.passes[i];
            if (pass.code.trim()) {
                const fakeUri = this.uriManager.buildFakeUri(stoyUri, 'pass', pass.name);
                const vdoc = generatePassVirtualDoc(doc, i);
                this.syncOneBlock(stoyUri, fakeUri, 'pass', pass.name, vdoc, pass.codeRange, states, isOpen);
            }
        }
    }

    private syncOneBlock(
        stoyUri: string,
        fakeUri: string,
        blockType: 'common' | 'pass',
        blockName: string,
        vdoc: HlslVirtualDoc,
        blockRange: BlockRange,
        states: Map<string, FakeDocState>,
        isOpen: boolean,
    ): void {
        const existing = states.get(fakeUri);

        if (existing && !isOpen) {
            // Update existing: send didChange
            existing.version++;
            existing.prefixLineCount = vdoc.prefixLineCount;
            existing.blockRange = blockRange;
            this.sendDidChange(fakeUri, vdoc.content, existing.version);
        } else {
            // New block: send didOpen
            const state: FakeDocState = {
                fakeUri,
                blockType,
                blockName,
                prefixLineCount: vdoc.prefixLineCount,
                blockRange,
                version: 1,
            };
            states.set(fakeUri, state);
            this.uriManager.trackOpen(stoyUri, fakeUri);
            // Debug: log virtual doc details
            const lines = vdoc.content.split('\n');
            this.logger.debug(
                `[Stoy][sync] block "${blockName}" (${blockType}): prefixLineCount=${vdoc.prefixLineCount}, ` +
                `blockRange=[${blockRange.startLine}-${blockRange.endLine}], totalLines=${lines.length}`
            );
            // Show first 5 lines and lines around prefixLineCount
            const preview = lines.slice(0, 5).map((l, i) => `  ${i}: ${l}`).join('\n');
            this.logger.debug(`[Stoy][sync] content preview (first 5 lines):\n${preview}`);
            if (vdoc.prefixLineCount > 0) {
                const around = lines.slice(Math.max(0, vdoc.prefixLineCount - 2), vdoc.prefixLineCount + 3)
                    .map((l, i) => `  ${Math.max(0, vdoc.prefixLineCount - 2) + i}: ${l}`).join('\n');
                this.logger.debug(`[Stoy][sync] content around prefixLineCount=${vdoc.prefixLineCount}:\n${around}`);
            }
            this.sendDidOpen(fakeUri, vdoc.content, state.version);
        }

        // In debug mode, dump virtual HLSL content to file
        this.dumpVirtualHlsl(stoyUri, blockName, blockType, vdoc.content);
    }

    /**
     * Dump virtual HLSL content to a file in the log directory (debug mode only).
     * File naming: <stoyBaseName>.<blockName>.stoy-virtual.hlsl
     */
    private dumpVirtualHlsl(
        stoyUri: string,
        blockName: string,
        blockType: 'common' | 'pass',
        content: string,
    ): void {
        if (!this.virtualHlslDir) return;

        try {
            // Extract base name from stoy URI: e.g. "feedback_demo" from "file:///path/feedback_demo.stoy"
            const stoyPath = vscode.Uri.parse(stoyUri).fsPath;
            const stoyBaseName = path.basename(stoyPath, '.stoy');

            // Build filename: feedback_demo.Feedback.stoy-virtual.hlsl
            const safeName = blockType === 'common' ? '_common_' : blockName;
            const fileName = `${stoyBaseName}.${safeName}.stoy-virtual.hlsl`;
            const filePath = path.join(this.virtualHlslDir, fileName);

            fs.writeFileSync(filePath, content, 'utf-8');
            this.logger.debug(`[Stoy][dump] Virtual HLSL written: ${filePath}`);
        } catch (err) {
            this.logger.error(`[Stoy][dump] Failed to write virtual HLSL: ${err}`);
        }
    }

    // ============================================================
    // LSP notification senders
    // ============================================================

    private sendDidOpen(fakeUri: string, content: string, version: number): void {
        if (!this.client) return;
        this.logger.debug(`[Stoy][sync] didOpen: ${fakeUri} (version=${version}, contentLen=${content.length})`);
        this.client.sendNotification('textDocument/didOpen', {
            textDocument: {
                uri: fakeUri,
                languageId: 'hlsl',
                version,
                text: content,
            },
        });
    }

    private sendDidChange(fakeUri: string, content: string, version: number): void {
        if (!this.client) return;
        this.client.sendNotification('textDocument/didChange', {
            textDocument: { uri: fakeUri, version },
            contentChanges: [{ text: content }],
        });
    }

    private sendDidClose(fakeUri: string): void {
        if (!this.client) return;
        this.client.sendNotification('textDocument/didClose', {
            textDocument: { uri: fakeUri },
        });
    }

    // ============================================================
    // Diagnostics mapping
    // ============================================================

    /**
     * Handle diagnostics from shader-language-server.
     * Maps fake URI → .stoy physical URI, virtual line → physical line.
     * Filters out diagnostics in injected code region.
     */
    private handleExternalDiagnostics(
        fakeDocUri: vscode.Uri,
        rawDiagnostics: vscode.Diagnostic[],
    ): void {
        const fakeUriStr = fakeDocUri.toString();
        const info = this.uriManager.parseFakeUri(fakeUriStr);
        if (!info) return; // Not a fake URI we manage

        // Find the FakeDocState for line mapping
        const states = this.docStates.get(info.stoyUri);
        if (!states) return;

        const state = states.get(fakeUriStr);
        if (!state) return;

        // Map diagnostics
        const mapped: vscode.Diagnostic[] = [];
        for (const diag of rawDiagnostics) {
            const startLine = virtualToPhysical(
                diag.range.start.line, state.blockRange, state.prefixLineCount,
            );
            const endLine = virtualToPhysical(
                diag.range.end.line, state.blockRange, state.prefixLineCount,
            );

            // Filter out diagnostics in injected code region
            if (startLine < 0 || endLine < 0) continue;

            const mappedRange = new vscode.Range(
                startLine, diag.range.start.character,
                endLine, diag.range.end.character,
            );

            const mappedDiag = new vscode.Diagnostic(mappedRange, diag.message, diag.severity);
            mappedDiag.source = diag.source ?? 'shader-language-server';
            mappedDiag.code = diag.code;
            if (diag.relatedInformation) {
                mappedDiag.relatedInformation = diag.relatedInformation;
            }
            mapped.push(mappedDiag);
        }

        // Merge diagnostics from all blocks of the same .stoy file
        this.mergeDiagnostics(info.stoyUri, fakeUriStr, mapped);
    }

    /**
     * Merge diagnostics from multiple fake docs into a single diagnostic set
     * for the physical .stoy file.
     */
    private pendingDiagnostics = new Map<string, Map<string, vscode.Diagnostic[]>>();

    private mergeDiagnostics(stoyUri: string, fakeUri: string, diagnostics: vscode.Diagnostic[]): void {
        let perFile = this.pendingDiagnostics.get(stoyUri);
        if (!perFile) {
            perFile = new Map();
            this.pendingDiagnostics.set(stoyUri, perFile);
        }
        perFile.set(fakeUri, diagnostics);

        // Merge all blocks' diagnostics
        const allDiags: vscode.Diagnostic[] = [];
        for (const diags of perFile.values()) {
            allDiags.push(...diags);
        }

        this.diagnosticCollection.set(vscode.Uri.parse(stoyUri), allDiags);
    }

    // ============================================================
    // Position mapping helpers (for middleware in extension.ts)
    // ============================================================

    /**
     * Find which HLSL block the cursor is in and return the fake URI + mapped position.
     * Returns null if cursor is not in an HLSL block.
     */
    findHlslBlock(stoyUri: string, line: number, character: number): {
        fakeUri: string;
        virtualPosition: vscode.Position;
        state: FakeDocState;
    } | null {
        const doc = this.stoyDocCache.get(stoyUri);
        if (!doc) return null;

        const states = this.docStates.get(stoyUri);
        if (!states) return null;

        // Check common block
        if (doc.common && isInBlockRange(line, character, doc.common.codeRange)) {
            const fakeUri = this.uriManager.buildFakeUri(stoyUri, 'common', '_common_');
            const state = states.get(fakeUri);
            if (!state) return null;
            const virtualLine = physicalToVirtual(line, state.blockRange, state.prefixLineCount);
            if (virtualLine < 0) return null;
            return { fakeUri, virtualPosition: new vscode.Position(virtualLine, character), state };
        }

        // Check pass blocks
        for (const pass of doc.passes) {
            if (pass.codeRange.startLine > 0 && isInBlockRange(line, character, pass.codeRange)) {
                const fakeUri = this.uriManager.buildFakeUri(stoyUri, 'pass', pass.name);
                const state = states.get(fakeUri);
                if (!state) return null;
                const virtualLine = physicalToVirtual(line, state.blockRange, state.prefixLineCount);
                if (virtualLine < 0) return null;
                return { fakeUri, virtualPosition: new vscode.Position(virtualLine, character), state };
            }
        }

        return null;
    }

    /**
     * Map a virtual document position back to physical .stoy file position.
     */
    mapVirtualToPhysical(fakeUri: string, virtualLine: number, virtualCharacter: number): {
        line: number;
        character: number;
    } | null {
        const info = this.uriManager.parseFakeUri(fakeUri);
        if (!info) return null;

        const states = this.docStates.get(info.stoyUri);
        if (!states) return null;

        const state = states.get(fakeUri);
        if (!state) return null;

        const physicalLine = virtualToPhysical(virtualLine, state.blockRange, state.prefixLineCount);
        if (physicalLine < 0) return null;

        return { line: physicalLine, character: virtualCharacter };
    }

    // ============================================================
    // LSP request forwarding (hover / completion / definition)
    // ============================================================

    /**
     * Send textDocument/hover request to shader-language-server.
     */
    async sendHoverRequest(fakeUri: string, position: vscode.Position): Promise<vscode.Hover | null> {
        if (!this.client) return null;
        this.logger.debug(`[Stoy][hover] sendRequest: uri=${fakeUri}, pos=(${position.line},${position.character})`);
        try {
            const result = await this.client.sendRequest(lsp.HoverRequest.type, {
                textDocument: { uri: fakeUri },
                position: { line: position.line, character: position.character },
            });
            this.logger.debug(`[Stoy][hover] raw result: ${result ? JSON.stringify(result).substring(0, 300) : 'null'}`);
            if (!result) return null;
            // Convert LSP Hover → vscode.Hover
            const contents = Array.isArray(result.contents)
                ? result.contents.map(c => typeof c === 'string' ? new vscode.MarkdownString(c) : new vscode.MarkdownString(c.value))
                : typeof result.contents === 'string'
                    ? [new vscode.MarkdownString(result.contents)]
                    : [new vscode.MarkdownString((result.contents as { kind?: string; value: string }).value)];
            const range = result.range
                ? new vscode.Range(result.range.start.line, result.range.start.character, result.range.end.line, result.range.end.character)
                : undefined;
            return new vscode.Hover(contents, range);
        } catch (err) {
            this.logger.error(`[Stoy] hover request failed: ${err}`);
            return null;
        }
    }

    /**
     * Send textDocument/completion request to shader-language-server.
     */
    async sendCompletionRequest(
        fakeUri: string,
        position: vscode.Position,
        triggerCharacter?: string,
    ): Promise<vscode.CompletionList | null> {
        if (!this.client) return null;
        this.logger.debug(`[Stoy][completion] sendRequest: uri=${fakeUri}, pos=(${position.line},${position.character}), trigger=${triggerCharacter ?? 'none'}`);
        try {
            const result = await this.client.sendRequest(lsp.CompletionRequest.type, {
                textDocument: { uri: fakeUri },
                position: { line: position.line, character: position.character },
                context: {
                    triggerKind: triggerCharacter
                        ? lsp.CompletionTriggerKind.TriggerCharacter
                        : lsp.CompletionTriggerKind.Invoked,
                    triggerCharacter,
                },
            });
            if (!result) return null;
            // Convert LSP CompletionItem[] or CompletionList → vscode.CompletionList
            const items = Array.isArray(result) ? result : result.items;
            const isIncomplete = Array.isArray(result) ? false : result.isIncomplete;
            const vsItems = items.map(item => {
                const ci = new vscode.CompletionItem(item.label, item.kind as number | undefined);
                ci.detail = item.detail;
                ci.documentation = item.documentation
                    ? (typeof item.documentation === 'string'
                        ? item.documentation
                        : new vscode.MarkdownString(item.documentation.value))
                    : undefined;
                ci.sortText = item.sortText;
                ci.filterText = item.filterText;
                ci.insertText = item.insertText;
                if (item.textEdit && 'range' in item.textEdit) {
                    const r = item.textEdit.range;
                    ci.range = new vscode.Range(r.start.line, r.start.character, r.end.line, r.end.character);
                    ci.insertText = item.textEdit.newText;
                }
                return ci;
            });
            return new vscode.CompletionList(vsItems, isIncomplete);
        } catch (err) {
            this.logger.error(`[Stoy] completion request failed: ${err}`);
            return null;
        }
    }

    /**
     * Send textDocument/definition request to shader-language-server.
     */
    async sendDefinitionRequest(
        fakeUri: string,
        position: vscode.Position,
    ): Promise<vscode.Location[] | null> {
        if (!this.client) return null;
        try {
            const result = await this.client.sendRequest(lsp.DefinitionRequest.type, {
                textDocument: { uri: fakeUri },
                position: { line: position.line, character: position.character },
            });
            if (!result) return null;
            // Convert LSP Location(s) → vscode.Location[]
            const locs = Array.isArray(result) ? result : [result];
            return locs.map(loc => {
                if ('targetUri' in loc) {
                    // LocationLink
                    const r = loc.targetRange;
                    return new vscode.Location(
                        vscode.Uri.parse(loc.targetUri),
                        new vscode.Range(r.start.line, r.start.character, r.end.line, r.end.character),
                    );
                }
                // Location
                const r = loc.range;
                return new vscode.Location(
                    vscode.Uri.parse(loc.uri),
                    new vscode.Range(r.start.line, r.start.character, r.end.line, r.end.character),
                );
            });
        } catch (err) {
            this.logger.error(`[Stoy] definition request failed: ${err}`);
            return null;
        }
    }

    /**
     * Check if the manager is ready (second LSP client is running).
     */
    get ready(): boolean {
        return this.isReady;
    }

    /**
     * Get the FakeUriManager instance (for external use).
     */
    get fakeUris(): FakeUriManager {
        return this.uriManager;
    }

    /**
     * Get the StoyParser instance (for external use).
     */
    get stoyParser(): StoyParser {
        return this.parser;
    }

    /**
     * Get cached StoyDocument for a .stoy URI.
     */
    getStoyDocument(stoyUri: string): StoyDocument | undefined {
        return this.stoyDocCache.get(stoyUri);
    }
}
