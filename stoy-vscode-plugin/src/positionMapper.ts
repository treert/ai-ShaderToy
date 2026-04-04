// ============================================================
// Position Mapper — 物理文件位置与虚拟文档位置的双向映射
// ============================================================

import { BlockRange } from './types';

/**
 * 将物理文件中的行号映射到虚拟文档中的行号
 * @param physicalLine 物理文件中的行号（0-based）
 * @param blockCodeRange 代码块在物理文件中的范围（code 内容部分）
 * @param prefixLineCount 虚拟文档中注入代码的行数
 * @returns 虚拟文档中的行号（0-based），如果不在代码块范围内返回 -1
 */
export function physicalToVirtual(
    physicalLine: number,
    blockCodeRange: BlockRange,
    prefixLineCount: number,
): number {
    if (physicalLine < blockCodeRange.startLine || physicalLine > blockCodeRange.endLine) {
        return -1;
    }
    return prefixLineCount + (physicalLine - blockCodeRange.startLine);
}

/**
 * 将虚拟文档中的行号映射回物理文件中的行号
 * @param virtualLine 虚拟文档中的行号（0-based）
 * @param blockCodeRange 代码块在物理文件中的范围（code 内容部分）
 * @param prefixLineCount 虚拟文档中注入代码的行数
 * @returns 物理文件中的行号（0-based），如果在注入代码区域返回 -1
 */
export function virtualToPhysical(
    virtualLine: number,
    blockCodeRange: BlockRange,
    prefixLineCount: number,
): number {
    if (virtualLine < prefixLineCount) {
        return -1; // 在注入代码区域
    }
    return blockCodeRange.startLine + (virtualLine - prefixLineCount);
}

/**
 * 判断物理文件中的位置是否在某个代码块范围内
 */
export function isInBlockRange(line: number, col: number, range: BlockRange): boolean {
    if (line < range.startLine || line > range.endLine) return false;
    // 对于多行块，只检查行范围即可（列范围只在单行块有意义）
    return true;
}
