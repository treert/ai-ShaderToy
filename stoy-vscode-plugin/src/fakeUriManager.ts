// ============================================================
// Fake URI Manager — fake_file_lsp mode
// Generates and manages fake file:// URIs for virtual HLSL documents.
// Fake URIs are placed in the same directory as the .stoy file
// so that shader-language-server can resolve #include relative paths.
//
// URI format:
//   file:///<stoyDir>/<stoyBaseName>.<blockName>.stoy-virtual.hlsl
//
// Example:
//   .stoy file:  file:///C:/Projects/demo.stoy
//   Image pass:   file:///C:/Projects/demo.Image.stoy-virtual.hlsl
//   common block: file:///C:/Projects/demo._common_.stoy-virtual.hlsl
// ============================================================

import * as path from 'path';
import * as vscode from 'vscode';

/** Suffix added to fake URIs to avoid collision with real .hlsl files */
const FAKE_SUFFIX = '.stoy-virtual.hlsl';

/**
 * Information extracted from a fake URI
 */
export interface FakeUriInfo {
    /** The original .stoy file URI (file:// scheme) */
    stoyUri: string;
    /** Block name: pass name or '_common_' */
    blockName: string;
    /** Block type */
    blockType: 'common' | 'pass';
}

/**
 * Manages fake file:// URIs for the fake_file_lsp mode.
 * Each .stoy file can have multiple fake URIs (one per HLSL code block).
 */
export class FakeUriManager {
    /**
     * Map from .stoy file URI → Set of active fake URIs.
     * Used to track which fake documents are currently open.
     */
    private activeUris = new Map<string, Set<string>>();

    /**
     * Reverse map: fake URI → FakeUriInfo.
     * Populated when fake URIs are built, used for fast reverse lookup.
     */
    private reverseMap = new Map<string, FakeUriInfo>();

    /**
     * Build a fake file:// URI for a given .stoy file and block.
     *
     * @param stoyFileUri The .stoy file URI (file:// scheme string)
     * @param blockType 'common' or 'pass'
     * @param blockName Pass name or '_common_'
     * @returns A fake file:// URI string
     */
    buildFakeUri(stoyFileUri: string, blockType: 'common' | 'pass', blockName: string): string {
        const parsed = vscode.Uri.parse(stoyFileUri);
        const fsPath = parsed.fsPath; // e.g. C:\Projects\demo.stoy
        const dir = path.dirname(fsPath);
        const baseName = path.basename(fsPath, '.stoy'); // e.g. demo

        // Format: <dir>/<baseName>.<blockName>.stoy-virtual.hlsl
        const fakePath = path.join(dir, `${baseName}.${blockName}${FAKE_SUFFIX}`);
        const fakeUri = vscode.Uri.file(fakePath).toString();

        // Register in reverse map
        this.reverseMap.set(fakeUri, { stoyUri: stoyFileUri, blockName, blockType });

        return fakeUri;
    }

    /**
     * Parse a fake URI back to its source .stoy file and block info.
     *
     * @param fakeUri The fake file:// URI string
     * @returns FakeUriInfo or null if not a recognized fake URI
     */
    parseFakeUri(fakeUri: string): FakeUriInfo | null {
        // Fast path: check reverse map
        const cached = this.reverseMap.get(fakeUri);
        if (cached) return cached;

        // Slow path: parse from URI structure
        const parsed = vscode.Uri.parse(fakeUri);
        const fsPath = parsed.fsPath;
        const fileName = path.basename(fsPath);

        // Must end with .stoy-virtual.hlsl
        if (!fileName.endsWith(FAKE_SUFFIX)) return null;

        // Strip suffix: "demo.Image.stoy-virtual.hlsl" → "demo.Image"
        const withoutSuffix = fileName.slice(0, -FAKE_SUFFIX.length);

        // Find the first dot to split baseName and blockName
        const firstDot = withoutSuffix.indexOf('.');
        if (firstDot < 0) return null;

        const baseName = withoutSuffix.slice(0, firstDot);
        const blockName = withoutSuffix.slice(firstDot + 1);
        if (!blockName) return null;

        // Reconstruct .stoy file URI
        const dir = path.dirname(fsPath);
        const stoyPath = path.join(dir, `${baseName}.stoy`);
        const stoyUri = vscode.Uri.file(stoyPath).toString();

        const blockType: 'common' | 'pass' = blockName === '_common_' ? 'common' : 'pass';

        const info: FakeUriInfo = { stoyUri, blockName, blockType };
        this.reverseMap.set(fakeUri, info);
        return info;
    }

    /**
     * Register a fake URI as active for a .stoy file.
     */
    trackOpen(stoyUri: string, fakeUri: string): void {
        let set = this.activeUris.get(stoyUri);
        if (!set) {
            set = new Set();
            this.activeUris.set(stoyUri, set);
        }
        set.add(fakeUri);
    }

    /**
     * Get all active fake URIs for a .stoy file.
     */
    getActiveUris(stoyUri: string): Set<string> {
        return this.activeUris.get(stoyUri) ?? new Set();
    }

    /**
     * Remove a single fake URI from tracking for a .stoy file.
     */
    removeOne(stoyUri: string, fakeUri: string): void {
        const set = this.activeUris.get(stoyUri);
        if (set) {
            set.delete(fakeUri);
            if (set.size === 0) {
                this.activeUris.delete(stoyUri);
            }
        }
        this.reverseMap.delete(fakeUri);
    }

    /**
     * Remove all active fake URIs for a .stoy file and clean up reverse map.
     * @returns The set of fake URIs that were removed (for sending didClose).
     */
    removeAll(stoyUri: string): Set<string> {
        const set = this.activeUris.get(stoyUri);
        if (!set) return new Set();

        // Clean reverse map
        for (const fakeUri of set) {
            this.reverseMap.delete(fakeUri);
        }

        this.activeUris.delete(stoyUri);
        return set;
    }

    /**
     * Check if a given URI is a known fake URI.
     */
    isFakeUri(uri: string): boolean {
        return this.reverseMap.has(uri);
    }

    /**
     * Get all tracked .stoy URIs.
     */
    getTrackedStoyUris(): string[] {
        return Array.from(this.activeUris.keys());
    }

    /**
     * Clear all state.
     */
    dispose(): void {
        this.activeUris.clear();
        this.reverseMap.clear();
    }
}
