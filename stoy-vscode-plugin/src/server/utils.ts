// ============================================================
// Server Utilities — 共享工具函数
// ============================================================

import { Position } from 'vscode-languageserver';
import { TextDocument } from 'vscode-languageserver-textdocument';

/** 获取光标处的标识符单词 */
export function getWordAtPosition(textDoc: TextDocument, position: Position): string | null {
    const line = textDoc.getText({
        start: { line: position.line, character: 0 },
        end: { line: position.line + 1, character: 0 },
    });

    let start = position.character;
    while (start > 0 && isIdentChar(line[start - 1])) start--;
    let end = position.character;
    while (end < line.length && isIdentChar(line[end])) end++;

    if (start === end) return null;
    return line.substring(start, end);
}

function isIdentChar(c: string): boolean {
    return /[a-zA-Z0-9_]/.test(c);
}
