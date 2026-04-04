// ============================================================
// HLSL Symbol Scanner — 从用户 HLSL 代码中提取符号（L1）
// 使用正则模式匹配，不做完整的 HLSL 语法分析。
// 扫描范围：common 块 + 各 pass 的 code 块。
// ============================================================

import { StoyDocument, BlockRange } from './types';

/** 符号类型 */
export type HlslSymbolKind = 'function' | 'struct' | 'variable' | 'macro' | 'field';

/** 提取到的用户符号 */
export interface HlslSymbol {
    name: string;
    kind: HlslSymbolKind;
    /** 签名或声明文本（用于 hover 显示） */
    signature: string;
    /** 符号在物理文件中的行号（0-based） */
    line: number;
    /** 符号在物理文件中的列号（0-based） */
    col: number;
    /** 符号所在块类型 */
    source: 'common' | 'pass';
    /** 如果是 pass 中的符号，pass 名称 */
    passName?: string;
    /** struct 的成员列表 */
    members?: HlslSymbol[];
}

/**
 * 扫描文档中所有 HLSL 代码块，提取用户定义的符号。
 * 返回的符号包括 common 块中的（对所有 pass 可见）和各 pass code 块中的。
 */
export function scanHlslSymbols(doc: StoyDocument): HlslSymbol[] {
    const symbols: HlslSymbol[] = [];

    // 扫描 common 块
    if (doc.common && doc.common.code) {
        const blockSymbols = scanCodeBlock(doc.common.code, doc.common.codeRange, 'common');
        symbols.push(...blockSymbols);
    }

    // 扫描各 pass 的 code 块
    for (const pass of doc.passes) {
        if (pass.code) {
            const blockSymbols = scanCodeBlock(pass.code, pass.codeRange, 'pass', pass.name);
            symbols.push(...blockSymbols);
        }
    }

    return symbols;
}

/**
 * 获取在指定位置可见的符号（common 的对所有位置可见，pass 的只对同 pass 可见）。
 */
export function getVisibleSymbols(allSymbols: HlslSymbol[], doc: StoyDocument, line: number): HlslSymbol[] {
    // 确定当前行在哪个块中
    let currentPass: string | undefined;

    if (doc.common && line >= doc.common.codeRange.startLine && line <= doc.common.codeRange.endLine) {
        // 在 common 块中，只能看到 common 自己的符号
        return allSymbols.filter(s => s.source === 'common');
    }

    for (const pass of doc.passes) {
        if (pass.codeRange.startLine > 0 &&
            line >= pass.codeRange.startLine && line <= pass.codeRange.endLine) {
            currentPass = pass.name;
            break;
        }
    }

    if (!currentPass) return [];

    // 在 pass 中：可以看到 common 的 + 当前 pass 的符号
    return allSymbols.filter(s => s.source === 'common' || s.passName === currentPass);
}

// ============================================================
// 内部：代码块扫描
// ============================================================

function scanCodeBlock(
    code: string,
    codeRange: BlockRange,
    source: 'common' | 'pass',
    passName?: string,
): HlslSymbol[] {
    const symbols: HlslSymbol[] = [];
    
    // code 可能以 \n 或 \r\n 开头（[=[ 后紧跟换行），但 codeRange.startLine
    // 已经跳过了这个换行指向内容实际开始行。需要剥离开头的换行以对齐行号。
    let trimmedCode = code;
    if (trimmedCode.startsWith('\r\n')) {
        trimmedCode = trimmedCode.slice(2);
    } else if (trimmedCode.startsWith('\n')) {
        trimmedCode = trimmedCode.slice(1);
    }
    
    const lines = trimmedCode.split('\n');
    const startLine = codeRange.startLine;

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        const physLine = startLine + i;

        // #define MACRO_NAME [value]
        const macroMatch = line.match(/^\s*#define\s+([A-Za-z_]\w*)(\s+.*)?/);
        if (macroMatch) {
            symbols.push({
                name: macroMatch[1],
                kind: 'macro',
                signature: `#define ${macroMatch[1]}${macroMatch[2] || ''}`,
                line: physLine,
                col: line.indexOf(macroMatch[1]),
                source,
                passName,
            });
            continue;
        }

        // struct Name { ... };
        // 简单匹配 struct 开始行
        const structMatch = line.match(/^\s*struct\s+([A-Za-z_]\w*)\s*\{?/);
        if (structMatch) {
            const structName = structMatch[1];
            const members = parseStructMembers(lines, i + 1);
            symbols.push({
                name: structName,
                kind: 'struct',
                signature: `struct ${structName}`,
                line: physLine,
                col: line.indexOf(structName),
                source,
                passName,
                members: members.map(m => ({
                    ...m,
                    line: startLine + m.line,
                    source,
                    passName,
                })),
            });
            continue;
        }

        // 函数定义：returnType functionName(params...) {
        // 匹配模式：类型 名字 ( 参数 ) {  — 排除 if/for/while 等控制流
        const funcMatch = line.match(
            /^\s*((?:static\s+)?(?:inline\s+)?(?:const\s+)?[A-Za-z_]\w*(?:\s*<[^>]*>)?)\s+([A-Za-z_]\w*)\s*\(([^)]*)\)\s*\{?\s*$/
        );
        if (funcMatch) {
            const retType = funcMatch[1].trim();
            const funcName = funcMatch[2];
            const params = funcMatch[3].trim();

            // 排除控制流关键字
            if (isControlKeyword(funcName)) continue;
            // 排除变量声明（无参数列表的不是函数）
            // funcMatch 已经要求有括号了

            symbols.push({
                name: funcName,
                kind: 'function',
                signature: `${retType} ${funcName}(${params})`,
                line: physLine,
                col: line.indexOf(funcName),
                source,
                passName,
            });
            continue;
        }

        // 也匹配多行函数声明（参数在下一行的情况）
        // returnType funcName(
        const funcStartMatch = line.match(
            /^\s*((?:static\s+)?(?:inline\s+)?(?:const\s+)?[A-Za-z_]\w*(?:\s*<[^>]*>)?)\s+([A-Za-z_]\w*)\s*\(\s*$/
        );
        if (funcStartMatch) {
            const retType = funcStartMatch[1].trim();
            const funcName = funcStartMatch[2];
            if (isControlKeyword(funcName)) continue;

            // 收集后续行的参数直到 )
            let params = '';
            for (let j = i + 1; j < lines.length && j < i + 10; j++) {
                const paramLine = lines[j].trim();
                if (paramLine.includes(')')) {
                    params += paramLine.replace(/\)\s*\{?\s*$/, '').trim();
                    break;
                }
                params += paramLine + ' ';
            }

            symbols.push({
                name: funcName,
                kind: 'function',
                signature: `${retType} ${funcName}(${params.trim()})`,
                line: physLine,
                col: line.indexOf(funcName),
                source,
                passName,
            });
            continue;
        }

        // 全局/static 变量声明：[static] [const] type name [= value];
        const varMatch = line.match(
            /^\s*((?:static\s+)?(?:const\s+)?)([A-Za-z_]\w*(?:\d?x\d)?)\s+([A-Za-z_]\w*)\s*(?:=|;)/
        );
        if (varMatch) {
            const qualifiers = varMatch[1].trim();
            const typeName = varMatch[2];
            const varName = varMatch[3];
            // 排除控制流和类型关键字开头（如 struct Foo 已匹配）
            if (isControlKeyword(varName) || typeName === 'struct' || typeName === 'return') continue;

            symbols.push({
                name: varName,
                kind: 'variable',
                signature: `${qualifiers ? qualifiers + ' ' : ''}${typeName} ${varName}`,
                line: physLine,
                col: line.indexOf(varName, line.indexOf(typeName) + typeName.length),
                source,
                passName,
            });
        }
    }

    return symbols;
}

/**
 * 从 struct 开始行之后解析成员（简单模式：type name;）
 */
function parseStructMembers(lines: string[], startIdx: number): HlslSymbol[] {
    const members: HlslSymbol[] = [];

    for (let i = startIdx; i < lines.length; i++) {
        const rawLine = lines[i];
        const trimmed = rawLine.trim();
        if (trimmed.startsWith('}')) break; // struct 结束
        if (trimmed === '' || trimmed.startsWith('//') || trimmed.startsWith('/*')) continue;

        // type name [: SEMANTIC];
        const memberMatch = trimmed.match(/^([A-Za-z_]\w*(?:\d?x\d)?)\s+([A-Za-z_]\w*)\s*(?::\s*\w+)?\s*;/);
        if (memberMatch) {
            const memberName = memberMatch[2];
            const col = rawLine.indexOf(memberName, rawLine.indexOf(memberMatch[1]) + memberMatch[1].length);
            members.push({
                name: memberName,
                kind: 'field',
                signature: `${memberMatch[1]} ${memberName}`,
                line: i, // 相对行号，外部会加 startLine
                col: col >= 0 ? col : 0,
                source: 'common', // 占位，外部覆盖
            });
        }
    }

    return members;
}

const CONTROL_KEYWORDS = new Set([
    'if', 'else', 'for', 'while', 'do', 'switch', 'case', 'return',
    'break', 'continue', 'discard', 'typedef', 'namespace',
]);

function isControlKeyword(name: string): boolean {
    return CONTROL_KEYWORDS.has(name);
}
