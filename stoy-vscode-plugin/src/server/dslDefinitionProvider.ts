// ============================================================
// DSL Definition Provider — .stoy DSL 层跳转定义
// ============================================================

import { Definition, Location, Position, Range } from 'vscode-languageserver';
import { TextDocument } from 'vscode-languageserver-textdocument';
import { StoyDocument, BlockRange } from '../types';
import { getWordAtPosition } from './utils';

/** 提供 DSL 层的跳转定义 */
export function provideDslDefinition(
    doc: StoyDocument,
    textDoc: TextDocument,
    position: Position,
): Definition | null {
    const word = getWordAtPosition(textDoc, position);
    if (!word) return null;
    const uri = textDoc.uri;

    // 1. Pass 名称 → 跳转到 pass 声明
    const pass = doc.passes.find(p => p.name === word);
    if (pass) {
        return Location.create(uri, blockRangeToRange(pass.nameRange));
    }

    // 2. Texture 名称 → 跳转到 texture 声明
    const tex = doc.textures.find(t => t.name === word);
    if (tex) {
        return Location.create(uri, blockRangeToRange(tex.nameRange));
    }

    // 3. 内置变量名 → 跳转到 inner_vars 中的声明
    if (doc.innerVars) {
        const varRange = doc.innerVars.varRanges.get(word);
        if (varRange) {
            return Location.create(uri, blockRangeToRange(varRange));
        }
    }

    return null;
}

function blockRangeToRange(br: BlockRange): Range {
    return Range.create(br.startLine, br.startCol, br.endLine, br.endCol);
}

