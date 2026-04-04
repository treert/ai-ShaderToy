// ============================================================
// Semantic Token Provider — highlight .stoy builtin variables
// Provides precise token coloring that overrides TextMate grammar.
// ============================================================

import {
    SemanticTokensBuilder,
    SemanticTokensLegend,
    SemanticTokens,
} from 'vscode-languageserver/node';

import { TextDocument } from 'vscode-languageserver-textdocument';
import { StoyDocument, BUILTIN_VAR_NAMES } from '../types';

// ---- Token Legend ----

export const TOKEN_TYPES = ['variable'] as const;
export const TOKEN_MODIFIERS = ['readonly', 'defaultLibrary'] as const;

export const semanticTokensLegend: SemanticTokensLegend = {
    tokenTypes: [...TOKEN_TYPES],
    tokenModifiers: [...TOKEN_MODIFIERS],
};

// Pre-computed modifier bitmask: readonly | defaultLibrary = 0b11 = 3
const BUILTIN_MODIFIER_MASK = (1 << 0) | (1 << 1); // readonly + defaultLibrary

// Build regex from builtin variable names
const BUILTIN_REGEX = new RegExp(
    `\\b(${[...BUILTIN_VAR_NAMES].join('|')})\\b`,
    'g'
);

// ---- Provider ----

/** A raw token before encoding */
interface RawToken {
    line: number;
    col: number;
    length: number;
    tokenType: number;
    modifiers: number;
}

/**
 * Generate semantic tokens for a .stoy document.
 * Scans:
 *   1. inner_vars block — variable names
 *   2. common block — HLSL code
 *   3. pass code blocks — HLSL code
 *
 * Tokens are collected, sorted by (line, col), then pushed to the builder
 * because SemanticTokensBuilder requires ascending order.
 */
export function provideSemanticTokens(
    doc: StoyDocument,
    textDoc: TextDocument,
): SemanticTokens {
    const builder = new SemanticTokensBuilder();
    const text = textDoc.getText();
    const lines = text.split('\n');
    const tokens: RawToken[] = [];

    // 1. inner_vars block: highlight declared variable names
    if (doc.innerVars) {
        for (const [varName, range] of doc.innerVars.varRanges) {
            if (BUILTIN_VAR_NAMES.has(varName)) {
                tokens.push({
                    line: range.startLine,
                    col: range.startCol,
                    length: varName.length,
                    tokenType: 0,
                    modifiers: BUILTIN_MODIFIER_MASK,
                });
            }
        }
    }

    // 2. common block
    if (doc.common) {
        collectBlockBuiltins(tokens, lines, doc.common.codeRange.startLine, doc.common.codeRange.endLine);
    }

    // 3. pass code blocks
    for (const pass of doc.passes) {
        if (pass.codeRange.startLine > 0) {
            collectBlockBuiltins(tokens, lines, pass.codeRange.startLine, pass.codeRange.endLine);
        }
    }

    // Sort by (line, col) — required by SemanticTokensBuilder
    tokens.sort((a, b) => a.line - b.line || a.col - b.col);

    for (const t of tokens) {
        builder.push(t.line, t.col, t.length, t.tokenType, t.modifiers);
    }

    return builder.build();
}

/**
 * Scan lines [startLine..endLine] for builtin variable occurrences
 * and collect them into the tokens array.
 */
function collectBlockBuiltins(
    tokens: RawToken[],
    lines: string[],
    startLine: number,
    endLine: number,
): void {
    for (let i = startLine; i <= endLine && i < lines.length; i++) {
        const line = lines[i];
        BUILTIN_REGEX.lastIndex = 0;
        let match: RegExpExecArray | null;
        while ((match = BUILTIN_REGEX.exec(line)) !== null) {
            tokens.push({
                line: i,
                col: match.index,
                length: match[1].length,
                tokenType: 0,
                modifiers: BUILTIN_MODIFIER_MASK,
            });
        }
    }
}
