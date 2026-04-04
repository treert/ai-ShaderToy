// ============================================================
// HLSL Definition Provider — HLSL 区域跳转定义（L1）
// ============================================================

import { Definition, Location, Position, Range } from 'vscode-languageserver';
import { TextDocument } from 'vscode-languageserver-textdocument';
import { StoyDocument, BlockRange } from '../types';
import { HlslSymbol, getVisibleSymbols } from '../hlslSymbolScanner';
import { getWordAtPosition } from './utils';

export function provideHlslDefinition(
    doc: StoyDocument,
    textDoc: TextDocument,
    position: Position,
    symbols: HlslSymbol[],
): Definition | null {
    const word = getWordAtPosition(textDoc, position);
    if (!word) return null;
    const uri = textDoc.uri;

    // 1. texture 名称 → 跳到 texture 声明
    const tex = doc.textures.find(t => t.name === word);
    if (tex) {
        return Location.create(uri, toRange(tex.nameRange));
    }
    // texture 派生变量 → 跳到 texture 声明
    for (const t of doc.textures) {
        if (word === `${t.name}_Sampler` || word === `${t.name}_TexelSize`) {
            return Location.create(uri, toRange(t.nameRange));
        }
    }

    // 2. pass 名称 → 跳到 pass 声明
    const pass = doc.passes.find(p => p.name === word);
    if (pass) {
        return Location.create(uri, toRange(pass.nameRange));
    }
    // pass 派生变量 → 跳到 pass 声明
    for (const p of doc.passes) {
        if (word === `${p.name}_Sampler` || word === `${p.name}_TexelSize`) {
            return Location.create(uri, toRange(p.nameRange));
        }
    }

    // 3. inner_vars 中的内置变量 → 跳到 inner_vars 声明
    if (doc.innerVars) {
        const varRange = doc.innerVars.varRanges.get(word);
        if (varRange) {
            return Location.create(uri, toRange(varRange));
        }
    }

    // 4. 用户定义符号 → 跳到定义位置（L1）
    const visible = getVisibleSymbols(symbols, doc, position.line);
    const sym = visible.find(s => s.name === word);
    if (sym) {
        return Location.create(uri, Range.create(sym.line, sym.col, sym.line, sym.col + sym.name.length));
    }

    return null;
}

function toRange(br: BlockRange): Range {
    return Range.create(br.startLine, br.startCol, br.endLine, br.endCol);
}

