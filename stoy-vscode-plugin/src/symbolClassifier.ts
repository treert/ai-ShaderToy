// ============================================================
// Symbol Classifier — Classify HLSL symbols for provider priority
// Used by fake_file_lsp middleware to decide whether to prefer
// builtin providers or shader-language-server (SLS) results.
// ============================================================

import { findHlslFunction, findHlslType } from './hlslBuiltins';
import { BUILTIN_VAR_NAMES, StoyDocument } from './types';

/**
 * Symbol category determines which provider should be preferred.
 */
export enum SymbolCategory {
    /** HLSL built-in function (e.g. lerp, sin, frac) — prefer builtin for hover/completion */
    HlslBuiltinFunc = 'HlslBuiltinFunc',
    /** HLSL built-in type (e.g. float4, Texture2D) — prefer builtin for hover/completion */
    HlslBuiltinType = 'HlslBuiltinType',
    /** Framework built-in variable (e.g. iResolution, iTime) — prefer builtin */
    FrameworkVar = 'FrameworkVar',
    /** Texture-derived variable (e.g. MyTex, MyTex_Sampler, MyTex_TexelSize) — prefer builtin */
    TextureDerived = 'TextureDerived',
    /** Pass-derived variable (e.g. Feedback, Feedback_Sampler) — prefer builtin */
    PassDerived = 'PassDerived',
    /** mainImage entry function — prefer builtin */
    MainImageEntry = 'MainImageEntry',
    /** User-defined symbol (function/struct/variable/macro) — prefer SLS */
    UserDefined = 'UserDefined',
}

/**
 * Classify a symbol (word under cursor) into a category.
 *
 * Check order:
 * 1. HLSL built-in function
 * 2. HLSL built-in type
 * 3. Framework built-in variable (iResolution, iTime, etc.)
 * 4. Texture-derived variable (name, name_Sampler, name_TexelSize)
 * 5. Pass-derived variable (name, name_Sampler, name_TexelSize)
 * 6. mainImage entry function
 * 7. Default → UserDefined (prefer SLS)
 */
export function classifySymbol(word: string, stoyDoc: StoyDocument | undefined): SymbolCategory {
    // 1. HLSL built-in function
    if (findHlslFunction(word)) {
        return SymbolCategory.HlslBuiltinFunc;
    }

    // 2. HLSL built-in type
    if (findHlslType(word)) {
        return SymbolCategory.HlslBuiltinType;
    }

    // 3. Framework built-in variable
    if (BUILTIN_VAR_NAMES.has(word)) {
        return SymbolCategory.FrameworkVar;
    }

    // 4 & 5 require StoyDocument
    if (stoyDoc) {
        // 4. Texture-derived variable
        for (const tex of stoyDoc.textures) {
            if (word === tex.name || word === `${tex.name}_Sampler` || word === `${tex.name}_TexelSize`) {
                return SymbolCategory.TextureDerived;
            }
        }

        // 5. Pass-derived variable
        for (const pass of stoyDoc.passes) {
            if (word === pass.name || word === `${pass.name}_Sampler` || word === `${pass.name}_TexelSize`) {
                return SymbolCategory.PassDerived;
            }
        }
    }

    // 6. mainImage entry function
    if (word === 'mainImage') {
        return SymbolCategory.MainImageEntry;
    }

    // 7. Default: user-defined symbol
    return SymbolCategory.UserDefined;
}

/**
 * Whether builtin provider should be preferred for hover requests.
 * Returns true for symbols where builtin provides richer information
 * (detailed docs, framework context, DSL metadata).
 */
export function shouldPreferBuiltinForHover(category: SymbolCategory): boolean {
    switch (category) {
        case SymbolCategory.HlslBuiltinFunc:
        case SymbolCategory.HlslBuiltinType:
        case SymbolCategory.FrameworkVar:
        case SymbolCategory.TextureDerived:
        case SymbolCategory.PassDerived:
        case SymbolCategory.MainImageEntry:
            return true;
        case SymbolCategory.UserDefined:
            return false;
    }
}

/**
 * Whether builtin provider should be preferred for go-to-definition requests.
 * Similar to hover, but excludes HlslBuiltinFunc and HlslBuiltinType
 * (builtin doesn't support jumping to built-in function/type definitions).
 */
export function shouldPreferBuiltinForDefinition(category: SymbolCategory): boolean {
    switch (category) {
        case SymbolCategory.FrameworkVar:
        case SymbolCategory.TextureDerived:
        case SymbolCategory.PassDerived:
            return true;
        case SymbolCategory.HlslBuiltinFunc:
        case SymbolCategory.HlslBuiltinType:
        case SymbolCategory.MainImageEntry:
        case SymbolCategory.UserDefined:
            return false;
    }
}

/**
 * Whether a completion item label should prefer builtin version.
 * Used during completion list merging to decide which source wins for duplicates.
 */
export function shouldPreferBuiltinForCompletion(label: string, stoyDoc: StoyDocument | undefined): boolean {
    const category = classifySymbol(label, stoyDoc);
    return shouldPreferBuiltinForHover(category);
}
