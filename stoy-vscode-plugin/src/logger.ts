// ============================================================
// Logger — Unified logging with debug toggle and file output
// Supports two levels:
//   - info:  Always logged (startup, config, errors)
//   - debug: Only logged when stoy.debug.log is enabled (middleware, requests, etc.)
// All logs are written to a local file regardless of debug toggle.
// When debug is off, only info-level logs go to the file.
// ============================================================

import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';

export class Logger {
    private outputChannel: vscode.OutputChannel;
    private logFilePath: string;
    private logDir: string;
    private logStream: fs.WriteStream | undefined;
    private debugEnabled: boolean;

    constructor(outputChannel: vscode.OutputChannel, storagePath: string) {
        this.outputChannel = outputChannel;

        // Read debug toggle from config
        const config = vscode.workspace.getConfiguration('stoy.debug');
        this.debugEnabled = config.get<boolean>('log', false);

        // Ensure log directory exists
        this.logDir = path.join(storagePath, 'logs');
        const logDir = this.logDir;
        if (!fs.existsSync(logDir)) {
            fs.mkdirSync(logDir, { recursive: true });
        }

        // Create log file with timestamp in name
        const now = new Date();
        const timestamp = now.toISOString().replace(/[:.]/g, '-').replace('T', '_').slice(0, 19);
        this.logFilePath = path.join(logDir, `stoy-${timestamp}.log`);

        try {
            this.logStream = fs.createWriteStream(this.logFilePath, { flags: 'a', encoding: 'utf-8' });
        } catch {
            // If we can't create the log file, just continue without file logging
        }

        // Write header to log file
        this.writeToFile(`=== Stoy Language Extension Log ===`);
        this.writeToFile(`Started: ${now.toISOString()}`);
        this.writeToFile(`Debug logging: ${this.debugEnabled ? 'ENABLED' : 'DISABLED'}`);
        this.writeToFile(`Log file: ${this.logFilePath}`);
        this.writeToFile('');

        // Show log file path in output channel
        this.outputChannel.appendLine(`[Stoy] Log file: ${this.logFilePath}`);
        this.outputChannel.appendLine(`[Stoy] Debug logging: ${this.debugEnabled ? 'ENABLED' : 'DISABLED'}`);
    }

    /**
     * Info-level log — always output to outputChannel and file.
     * Use for: startup, config, lifecycle events, errors.
     */
    info(message: string): void {
        this.outputChannel.appendLine(message);
        this.writeToFile(`[INFO]  ${message}`);
    }

    /**
     * Debug-level log — only output when debug toggle is enabled.
     * Use for: middleware forwarding, LSP requests/responses, sync details.
     */
    debug(message: string): void {
        if (this.debugEnabled) {
            this.outputChannel.appendLine(message);
        }
        // Always write debug to file when debug is enabled
        if (this.debugEnabled) {
            this.writeToFile(`[DEBUG] ${message}`);
        }
    }

    /**
     * Error-level log — always output to outputChannel and file.
     */
    error(message: string): void {
        this.outputChannel.appendLine(message);
        this.writeToFile(`[ERROR] ${message}`);
    }

    /**
     * Get the log file path.
     */
    getLogFilePath(): string {
        return this.logFilePath;
    }

    /**
     * Get the log directory path.
     */
    getLogDir(): string {
        return this.logDir;
    }

    /**
     * Check if debug logging is enabled.
     */
    get isDebugEnabled(): boolean {
        return this.debugEnabled;
    }

    /**
     * Get the underlying output channel (for LanguageClient outputChannel config).
     */
    getOutputChannel(): vscode.OutputChannel {
        return this.outputChannel;
    }

    /**
     * Clean up old log files, keeping only the most recent N files.
     */
    cleanOldLogs(maxFiles: number = 10): void {
        try {
            const logDir = path.dirname(this.logFilePath);
            const files = fs.readdirSync(logDir)
                .filter(f => f.startsWith('stoy-') && f.endsWith('.log'))
                .sort()
                .reverse();

            // Delete files beyond maxFiles
            for (let i = maxFiles; i < files.length; i++) {
                try {
                    fs.unlinkSync(path.join(logDir, files[i]));
                } catch {
                    // Ignore deletion errors
                }
            }
        } catch {
            // Ignore cleanup errors
        }
    }

    /**
     * Dispose the logger and close the file stream.
     */
    dispose(): void {
        if (this.logStream) {
            this.writeToFile('');
            this.writeToFile(`=== Log ended: ${new Date().toISOString()} ===`);
            this.logStream.end();
            this.logStream = undefined;
        }
    }

    private writeToFile(message: string): void {
        if (!this.logStream) return;
        const timestamp = new Date().toISOString().slice(11, 23); // HH:mm:ss.SSS
        this.logStream.write(`${timestamp} ${message}\n`);
    }
}
