// ============================================================
// Semantic Token Provider — highlight .stoy builtin variables
// and pass/texture names with their derived variables.
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

// Token type index
const TOKEN_TYPE_VARIABLE = 0;

// Modifier bitmasks
const BUILTIN_MODIFIER_MASK = (1 << 0) | (1 << 1); // readonly + defaultLibrary
const FRAMEWORK_MODIFIER_MASK = (1 << 0);            // readonly only (pass/texture vars)

// Build regex from builtin variable names
const BUILTIN_REGEX = new RegExp(
    `\\b(${[...BUILTIN_VAR_NAMES].join('|')})\\b`,
    'g'
);

// Suffixes for pass/texture derived variables
const DERIVED_SUFFIXES = ['_Sampler', '_TexelSize'];

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
 * Build a regex that matches pass/texture names and their derived variables.
 * e.g. for names ["Feedback", "NoiseTex"]:
 *   /\b(Feedback|NoiseTex)(_Sampler|_TexelSize)?\b/g
 * Returns null if no names to match.
 */
function buildFrameworkVarRegex(doc: StoyDocument): RegExp | null {
    const names: string[] = [];
    for (const pass of doc.passes) {
        if (pass.name) names.push(pass.name);
    }
    for (const tex of doc.textures) {
        if (tex.name) names.push(tex.name);
    }
    if (names.length === 0) return null;

    // Escape special regex chars in names (unlikely but safe)
    const escaped = names.map(n => n.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'));
    const suffixPart = `(${DERIVED_SUFFIXES.map(s => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('|')})`;
    return new RegExp(`\\b(${escaped.join('|')})${suffixPart}?\\b`, 'g');
}

/**
 * Generate semantic tokens for a .stoy document.
 * Scans:
 *   1. inner_vars block — builtin variable names
 *   2. common block — HLSL code (builtins + framework vars)
 *   3. pass code blocks — HLSL code (builtins + framework vars)
 *   4. DSL pass name declarations — pass name after "pass" keyword
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

    const frameworkRegex = buildFrameworkVarRegex(doc);

    // 1. inner_vars block: highlight declared variable names
    if (doc.innerVars) {
        for (const [varName, range] of doc.innerVars.varRanges) {
            if (BUILTIN_VAR_NAMES.has(varName)) {
                tokens.push({
                    line: range.startLine,
                    col: range.startCol,
                    length: varName.length,
                    tokenType: TOKEN_TYPE_VARIABLE,
                    modifiers: BUILTIN_MODIFIER_MASK,
                });
            }
        }
    }

    // 2. DSL pass name declarations (the name after "pass" keyword)
    for (const pass of doc.passes) {
        if (pass.name && pass.nameRange) {
            tokens.push({
                line: pass.nameRange.startLine,
                col: pass.nameRange.startCol,
                length: pass.name.length,
                tokenType: TOKEN_TYPE_VARIABLE,
                modifiers: FRAMEWORK_MODIFIER_MASK,
            });
        }
    }

    // 3. DSL texture name declarations
    for (const tex of doc.textures) {
        if (tex.name && tex.nameRange) {
            tokens.push({
                line: tex.nameRange.startLine,
                col: tex.nameRange.startCol,
                length: tex.name.length,
                tokenType: TOKEN_TYPE_VARIABLE,
                modifiers: FRAMEWORK_MODIFIER_MASK,
            });
        }
    }

    // 4. common block — builtins + framework vars
    if (doc.common) {
        collectBlockTokens(tokens, lines, doc.common.codeRange.startLine, doc.common.codeRange.endLine, frameworkRegex);
    }

    // 5. pass code blocks — builtins + framework vars
    for (const pass of doc.passes) {
        if (pass.codeRange.startLine > 0) {
            collectBlockTokens(tokens, lines, pass.codeRange.startLine, pass.codeRange.endLine, frameworkRegex);
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
 * and framework variable (pass/texture name + derived) occurrences,
 * then collect them into the tokens array.
 */
function collectBlockTokens(
    tokens: RawToken[],
    lines: string[],
    startLine: number,
    endLine: number,
    frameworkRegex: RegExp | null,
): void {
    for (let i = startLine; i <= endLine && i < lines.length; i++) {
        const line = lines[i];

        // Builtin variables (iTime, iResolution, etc.)
        BUILTIN_REGEX.lastIndex = 0;
        let match: RegExpExecArray | null;
        while ((match = BUILTIN_REGEX.exec(line)) !== null) {
            tokens.push({
                line: i,
                col: match.index,
                length: match[1].length,
                tokenType: TOKEN_TYPE_VARIABLE,
                modifiers: BUILTIN_MODIFIER_MASK,
            });
        }

        // Framework variables (pass/texture names + _Sampler/_TexelSize)
        if (frameworkRegex) {
            frameworkRegex.lastIndex = 0;
            while ((match = frameworkRegex.exec(line)) !== null) {
                tokens.push({
                    line: i,
                    col: match.index,
                    length: match[0].length,
                    tokenType: TOKEN_TYPE_VARIABLE,
                    modifiers: FRAMEWORK_MODIFIER_MASK,
                });
            }
        }
    }
}
