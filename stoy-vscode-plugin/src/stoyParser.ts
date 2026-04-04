// ============================================================
// .stoy File Parser — TypeScript Implementation
// 从 C++ stoy_parser.h/cpp 移植的手写递归下降解析器。
// 输出 StoyDocument，带精确的行号/列号范围。
// ============================================================

import {
    StoyDocument, StoyGlobalSetting, StoyTexture, StoyPass, StoyPassInit,
    StoyCommon, StoyInnerVars, StoyDiagnostic, BlockRange,
    BUILTIN_VAR_NAMES, isReservedName,
} from './types';

// ============================================================
// Token
// ============================================================

enum TokenType {
    // 标点
    LBrace, RBrace, LParen, RParen, Equals, Comma,
    // 字面量
    Identifier, StringLiteral, NumberLiteral, LongString,
    // 关键字
    KW_global_setting, KW_inner_vars, KW_texture, KW_common, KW_pass,
    KW_init, KW_code, KW_float4, KW_true, KW_false,
    // 特殊
    Eof, Error,
}

interface Token {
    type: TokenType;
    value: string;
    line: number;   // 0-based
    col: number;    // 0-based
}

const KEYWORD_MAP: Record<string, TokenType> = {
    'global_setting': TokenType.KW_global_setting,
    'inner_vars': TokenType.KW_inner_vars,
    'texture': TokenType.KW_texture,
    'common': TokenType.KW_common,
    'pass': TokenType.KW_pass,
    'init': TokenType.KW_init,
    'code': TokenType.KW_code,
    'float4': TokenType.KW_float4,
    'true': TokenType.KW_true,
    'false': TokenType.KW_false,
};

// ============================================================
// Lexer
// ============================================================

class Lexer {
    private source: string;
    private pos = 0;
    private line = 0;   // 0-based
    private col = 0;    // 0-based

    constructor(source: string) {
        this.source = source;
    }

    private currentChar(): string {
        return this.pos < this.source.length ? this.source[this.pos] : '\0';
    }

    private peekChar(offset = 1): string {
        const idx = this.pos + offset;
        return idx < this.source.length ? this.source[idx] : '\0';
    }

    private advance(): void {
        if (this.pos < this.source.length) {
            if (this.source[this.pos] === '\n') {
                this.line++;
                this.col = 0;
            } else {
                this.col++;
            }
            this.pos++;
        }
    }

    private isAtEnd(): boolean {
        return this.pos >= this.source.length;
    }

    private skipWhitespaceAndComments(): void {
        while (!this.isAtEnd()) {
            const c = this.currentChar();
            if (c === ' ' || c === '\t' || c === '\r' || c === '\n') {
                this.advance();
                continue;
            }
            // Lua 风格注释: --
            if (c === '-' && this.peekChar() === '-') {
                while (!this.isAtEnd() && this.currentChar() !== '\n') {
                    this.advance();
                }
                continue;
            }
            break;
        }
    }

    private readString(): Token {
        const startLine = this.line;
        const startCol = this.col;
        this.advance(); // skip opening "

        let val = '';
        while (!this.isAtEnd() && this.currentChar() !== '"') {
            if (this.currentChar() === '\\') {
                this.advance();
                if (this.isAtEnd()) break;
                const esc = this.currentChar();
                switch (esc) {
                    case '"': val += '"'; break;
                    case '\\': val += '\\'; break;
                    case 'n': val += '\n'; break;
                    case 't': val += '\t'; break;
                    case 'r': val += '\r'; break;
                    default: val += '\\' + esc; break;
                }
            } else if (this.currentChar() === '\n') {
                return { type: TokenType.Error, value: 'unterminated string literal (newline in string)', line: startLine, col: startCol };
            } else {
                val += this.currentChar();
            }
            this.advance();
        }
        if (this.isAtEnd()) {
            return { type: TokenType.Error, value: 'unterminated string literal', line: startLine, col: startCol };
        }
        this.advance(); // skip closing "
        return { type: TokenType.StringLiteral, value: val, line: startLine, col: startCol };
    }

    private readLongString(): Token {
        const startLine = this.line;
        const startCol = this.col;
        this.advance(); // skip first [

        // 计算 = 的数量
        let eqCount = 0;
        while (!this.isAtEnd() && this.currentChar() === '=') {
            eqCount++;
            this.advance();
        }
        if (this.isAtEnd() || this.currentChar() !== '[') {
            return { type: TokenType.Error, value: "malformed long string (expected '[' after '=')", line: startLine, col: startCol };
        }
        this.advance(); // skip second [

        // 构造结束标记
        let endMark = ']';
        for (let i = 0; i < eqCount; i++) endMark += '=';
        endMark += ']';

        let val = '';
        while (!this.isAtEnd()) {
            if (this.currentChar() === ']') {
                let match = true;
                for (let i = 0; i < endMark.length; i++) {
                    const idx = this.pos + i;
                    if (idx >= this.source.length || this.source[idx] !== endMark[i]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    for (let i = 0; i < endMark.length; i++) {
                        this.advance();
                    }
                    return { type: TokenType.LongString, value: val, line: startLine, col: startCol };
                }
            }
            val += this.currentChar();
            this.advance();
        }

        return { type: TokenType.Error, value: `unterminated long string (expected '${endMark}')`, line: startLine, col: startCol };
    }

    private readNumber(): Token {
        const startLine = this.line;
        const startCol = this.col;
        let val = '';
        if (this.currentChar() === '-') {
            val += '-';
            this.advance();
        }
        while (!this.isAtEnd() && isDigit(this.currentChar())) {
            val += this.currentChar();
            this.advance();
        }
        if (!this.isAtEnd() && this.currentChar() === '.') {
            val += '.';
            this.advance();
            while (!this.isAtEnd() && isDigit(this.currentChar())) {
                val += this.currentChar();
                this.advance();
            }
        }
        return { type: TokenType.NumberLiteral, value: val, line: startLine, col: startCol };
    }

    private readIdentifierOrKeyword(): Token {
        const startLine = this.line;
        const startCol = this.col;
        let val = '';
        while (!this.isAtEnd() && isIdentChar(this.currentChar())) {
            val += this.currentChar();
            this.advance();
        }
        const kwType = KEYWORD_MAP[val];
        return { type: kwType !== undefined ? kwType : TokenType.Identifier, value: val, line: startLine, col: startCol };
    }

    nextToken(): Token {
        this.skipWhitespaceAndComments();
        if (this.isAtEnd()) {
            return { type: TokenType.Eof, value: '', line: this.line, col: this.col };
        }

        const c = this.currentChar();
        const tokLine = this.line;
        const tokCol = this.col;

        // 标点
        switch (c) {
            case '{': this.advance(); return { type: TokenType.LBrace, value: '{', line: tokLine, col: tokCol };
            case '}': this.advance(); return { type: TokenType.RBrace, value: '}', line: tokLine, col: tokCol };
            case '(': this.advance(); return { type: TokenType.LParen, value: '(', line: tokLine, col: tokCol };
            case ')': this.advance(); return { type: TokenType.RParen, value: ')', line: tokLine, col: tokCol };
            case '=': this.advance(); return { type: TokenType.Equals, value: '=', line: tokLine, col: tokCol };
            case ',': this.advance(); return { type: TokenType.Comma, value: ',', line: tokLine, col: tokCol };
        }

        // 字符串
        if (c === '"') return this.readString();

        // 长字符串 [=[ ... ]=]
        if (c === '[' && (this.peekChar() === '=' || this.peekChar() === '[')) {
            return this.readLongString();
        }

        // 数字（含负数）
        if (isDigit(c) || (c === '-' && isDigit(this.peekChar()))) {
            return this.readNumber();
        }

        // 标识符 / 关键字
        if (isAlpha(c) || c === '_') {
            return this.readIdentifierOrKeyword();
        }

        // 未知字符
        this.advance();
        return { type: TokenType.Error, value: `unexpected character '${c}'`, line: tokLine, col: tokCol };
    }

    getLine(): number { return this.line; }
    getCol(): number { return this.col; }
}

function isDigit(c: string): boolean {
    return c >= '0' && c <= '9';
}

function isAlpha(c: string): boolean {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

function isIdentChar(c: string): boolean {
    return isAlpha(c) || isDigit(c) || c === '_';
}

// ============================================================
// Parser
// ============================================================

/**
 * 从 LongString token 的 value 和行号反推 contentRange。
 * 假设 [=[ 之后紧跟换行（标准 .stoy 写法），内容从下一行开始。
 * contentStartLine = token.line + 1（[=[ 行的下一行）
 * contentEndLine 通过内容中的换行符数量推算。
 * 
 * 行号约定：所有行号均为 0-based。
 */
function computeLongStringContentRange(token: Token): { contentStartLine: number; contentEndLine: number } {
    // [=[ 所在行是 token.line
    // 内容从 [=[ 之后开始。如果 value 以 \n 开头，说明 [=[ 后紧跟换行
    const value = token.value;
    
    // [=[ 之后的第一个字符如果是 \n，内容实际从下一行开始
    let contentStartLine = token.line; // [=[ 所在行
    if (value.length > 0 && value[0] === '\n') {
        contentStartLine = token.line + 1;
    } else if (value.length > 0 && value[0] === '\r' && value.length > 1 && value[1] === '\n') {
        contentStartLine = token.line + 1;
    } else {
        // [=[ 和内容在同一行 — 不常见但需要处理
        contentStartLine = token.line;
    }
    
    // 计算内容的行数
    let newlineCount = 0;
    for (let i = 0; i < value.length; i++) {
        if (value[i] === '\n') newlineCount++;
    }
    
    // contentEndLine: ]=] 所在行的前一行
    // 如果 value 以 \n 结尾（]=] 单独占一行），最后一行内容在 ]=] 的前一行
    const contentEndLine = contentStartLine + newlineCount;
    // 注意：如果 value 末尾有 \n，那 newlineCount 包含了那个换行，
    // contentEndLine 实际指向 ]=] 所在行，但 ]=] 行不含用户内容。
    // 位置映射时用 < endLine 即可。
    
    return { contentStartLine, contentEndLine };
}

export class StoyParser {
    private lexer!: Lexer;
    private peeked: Token | null = null;
    private diagnostics: StoyDiagnostic[] = [];
    private usedNames: Set<string> = new Set();

    // 解析结果
    private globalSetting?: StoyGlobalSetting;
    private innerVars?: StoyInnerVars;
    private textures: StoyTexture[] = [];
    private common?: StoyCommon;
    private passes: StoyPass[] = [];

    /**
     * 解析 .stoy 文件内容
     * @param source 文件内容字符串
     * @returns 解析结果
     */
    parse(source: string): StoyDocument {
        this.lexer = new Lexer(source);
        this.peeked = null;
        this.diagnostics = [];
        this.usedNames = new Set();
        this.globalSetting = undefined;
        this.innerVars = undefined;
        this.textures = [];
        this.common = undefined;
        this.passes = [];

        this.parseFile();

        return {
            globalSetting: this.globalSetting,
            innerVars: this.innerVars,
            textures: this.textures,
            common: this.common,
            passes: this.passes,
            diagnostics: this.diagnostics,
        };
    }

    // ---- Token 操作 ----

    private peekToken(): Token {
        if (!this.peeked) {
            this.peeked = this.lexer.nextToken();
        }
        return this.peeked;
    }

    private nextToken(): Token {
        if (this.peeked) {
            const tok = this.peeked;
            this.peeked = null;
            return tok;
        }
        return this.lexer.nextToken();
    }

    private expect(type: TokenType, context: string): Token | null {
        const tok = this.nextToken();
        if (tok.type !== type) {
            this.addError(tok, `expected ${tokenTypeName(type)} ${context}, got '${tok.value}'`);
            return null;
        }
        return tok;
    }

    // ---- 错误报告 ----

    private addError(tok: Token, msg: string): void {
        this.diagnostics.push({
            range: { startLine: tok.line, endLine: tok.line, startCol: tok.col, endCol: tok.col + tok.value.length },
            message: msg,
            severity: 'error',
        });
    }

    private addWarning(range: BlockRange, msg: string): void {
        this.diagnostics.push({ range, message: msg, severity: 'warning' });
    }

    // ---- 错误恢复 ----

    /** 判断 token 是否为顶层关键字 */
    private isTopLevelKeyword(type: TokenType): boolean {
        return type === TokenType.KW_global_setting
            || type === TokenType.KW_inner_vars
            || type === TokenType.KW_texture
            || type === TokenType.KW_common
            || type === TokenType.KW_pass;
    }

    /** 跳到下一个顶层关键字或 EOF（panic mode recovery） */
    private skipToNextTopLevel(): void {
        while (true) {
            const tok = this.peekToken();
            if (tok.type === TokenType.Eof || this.isTopLevelKeyword(tok.type)) return;
            this.nextToken(); // 丢弃
        }
    }

    /** 跳过 token 直到下一行的内容（用于块内键值对错误恢复） */
    private skipPastCurrentLine(refTok: Token): void {
        while (true) {
            const tok = this.peekToken();
            if (tok.type === TokenType.Eof || tok.type === TokenType.RBrace) return;
            if (this.isTopLevelKeyword(tok.type)) return;
            if (tok.line > refTok.line) return; // 已经到下一行了
            this.nextToken(); // 丢弃同一行的 token
        }
    }

    /** 跳到当前块的 } 结束符（跟踪嵌套深度），或遇到顶层关键字时停止 */
    private skipToBlockEnd(): void {
        let depth = 1; // 假设已消耗过 {
        while (true) {
            const tok = this.peekToken();
            if (tok.type === TokenType.Eof) return;
            if (this.isTopLevelKeyword(tok.type)) return; // 安全同步点
            this.nextToken(); // 消耗
            if (tok.type === TokenType.LBrace) depth++;
            if (tok.type === TokenType.RBrace) {
                depth--;
                if (depth <= 0) return;
            }
        }
    }

    // ---- 主循环 ----

    private parseFile(): void {
        while (true) {
            const tok = this.peekToken();
            if (tok.type === TokenType.Eof) break;
            if (tok.type === TokenType.Error) {
                this.addError(tok, tok.value);
                this.nextToken(); // 消耗错误 token
                continue; // 继续尝试解析
            }
            this.parseTopLevel();
        }

        // 验证：至少一个 pass
        if (this.passes.length === 0) {
            const tok = this.peekToken();
            this.addError(tok, "at least one 'pass' block is required");
        }
    }

    private parseTopLevel(): void {
        const tok = this.peekToken();
        switch (tok.type) {
            case TokenType.KW_global_setting: this.parseGlobalSetting(); break;
            case TokenType.KW_inner_vars: this.parseInnerVars(); break;
            case TokenType.KW_texture: this.parseTextureDecl(); break;
            case TokenType.KW_common: this.parseCommonBlock(); break;
            case TokenType.KW_pass: this.parsePassBlock(); break;
            default:
                this.addError(tok, `expected top-level keyword (global_setting, inner_vars, texture, common, pass), got '${tok.value}'`);
                this.nextToken(); // 消耗意外 token，避免死循环
                this.skipToNextTopLevel(); // 恢复到下一个顶层关键字
                break;
        }
    }

    // ---- global_setting ----

    private parseGlobalSetting(): void {
        const kw = this.nextToken();
        if (this.globalSetting) {
            this.addError(kw, "duplicate 'global_setting' block (at most one allowed)");
            this.skipToNextTopLevel();
            return;
        }

        const lb = this.expect(TokenType.LBrace, "after 'global_setting'");
        if (!lb) { this.skipToNextTopLevel(); return; }

        const setting: StoyGlobalSetting = {
            format: 'R8G8B8A8_UNORM', msaa: 1, vsync: true, resolutionScale: 1.0,
            range: { startLine: kw.line, endLine: kw.line, startCol: kw.col, endCol: kw.col },
        };

        while (true) {
            const peek = this.peekToken();
            if (peek.type === TokenType.RBrace) {
                const rb = this.nextToken();
                setting.range.endLine = rb.line;
                setting.range.endCol = rb.col + 1;
                break;
            }
            if (peek.type === TokenType.Eof) { this.addError(peek, 'unexpected end of file in global_setting block'); break; }
            // 遇到顶层关键字 → 块可能缺少 }，结束当前块解析
            if (this.isTopLevelKeyword(peek.type)) { this.addError(peek, "missing '}' for global_setting block"); break; }

            const key = this.expect(TokenType.Identifier, 'setting key');
            if (!key) { this.skipToBlockEnd(); break; }

            if (!this.expect(TokenType.Equals, `after setting key '${key.value}'`)) {
                // 跳过到下一行继续（尝试找到下一个 key 或 }）
                this.skipPastCurrentLine(key);
                continue;
            }
            const val = this.nextToken();

            if (key.value === 'format') {
                if (val.type !== TokenType.StringLiteral) { this.addError(val, "'format' expects a string value"); continue; }
                setting.format = val.value;
            } else if (key.value === 'msaa') {
                if (val.type !== TokenType.NumberLiteral) { this.addError(val, "'msaa' expects a number value"); continue; }
                setting.msaa = parseInt(val.value, 10);
            } else if (key.value === 'vsync') {
                if (val.type === TokenType.KW_true) setting.vsync = true;
                else if (val.type === TokenType.KW_false) setting.vsync = false;
                else { this.addError(val, "'vsync' expects true or false"); continue; }
            } else if (key.value === 'resolution_scale') {
                if (val.type !== TokenType.NumberLiteral) { this.addError(val, "'resolution_scale' expects a number value"); continue; }
                setting.resolutionScale = parseFloat(val.value);
            } else {
                this.addError(key, `unknown global_setting key '${key.value}'`);
                continue; // 跳过未知 key，继续解析下一个
            }
        }

        this.globalSetting = setting;
    }

    // ---- inner_vars ----

    private parseInnerVars(): void {
        const kw = this.nextToken();
        if (this.innerVars) {
            this.addError(kw, "duplicate 'inner_vars' block (at most one allowed)");
            this.skipToNextTopLevel();
            return;
        }

        const lb = this.expect(TokenType.LBrace, "after 'inner_vars'");
        if (!lb) { this.skipToNextTopLevel(); return; }

        const vars: string[] = [];
        const varRanges = new Map<string, BlockRange>();
        const range: BlockRange = { startLine: kw.line, endLine: kw.line, startCol: kw.col, endCol: kw.col };

        while (true) {
            const peek = this.peekToken();
            if (peek.type === TokenType.RBrace) {
                const rb = this.nextToken();
                range.endLine = rb.line;
                range.endCol = rb.col + 1;
                break;
            }
            if (peek.type === TokenType.Eof) { this.addError(peek, 'unexpected end of file in inner_vars block'); break; }
            if (this.isTopLevelKeyword(peek.type)) { this.addError(peek, "missing '}' for inner_vars block"); break; }

            const varTok = this.nextToken();
            if (varTok.type !== TokenType.Identifier) {
                this.addError(varTok, `expected built-in variable name, got '${varTok.value}'`);
                continue; // 跳过非标识符 token，继续
            }

            if (!BUILTIN_VAR_NAMES.has(varTok.value)) {
                this.addError(varTok, `'${varTok.value}' is not a valid built-in variable name`);
                continue; // 记录 error，跳过继续
            }
            if (vars.includes(varTok.value)) {
                this.addError(varTok, `duplicate inner_var '${varTok.value}'`);
                continue; // 记录 error，跳过继续
            }

            vars.push(varTok.value);
            varRanges.set(varTok.value, {
                startLine: varTok.line, endLine: varTok.line,
                startCol: varTok.col, endCol: varTok.col + varTok.value.length,
            });
        }

        this.innerVars = { vars, range, varRanges };
    }

    // ---- texture ----

    private parseTextureDecl(): void {
        const kw = this.nextToken();
        const nameTok = this.expect(TokenType.Identifier, 'texture name');
        if (!nameTok) { this.skipToNextTopLevel(); return; }

        if (isReservedName(nameTok.value)) {
            this.addError(nameTok, `'${nameTok.value}' is a reserved name and cannot be used as a texture name`);
            this.skipToNextTopLevel();
            return;
        }
        if (this.usedNames.has(nameTok.value)) {
            this.addError(nameTok, `name '${nameTok.value}' is already used by another texture or pass`);
            this.skipToNextTopLevel();
            return;
        }

        if (!this.expect(TokenType.Equals, 'after texture name')) { this.skipToNextTopLevel(); return; }

        const pathTok = this.expect(TokenType.StringLiteral, 'texture path');
        if (!pathTok) { this.skipToNextTopLevel(); return; }

        const tex: StoyTexture = {
            name: nameTok.value,
            path: pathTok.value,
            filter: 'linear',
            wrap: 'clamp',
            declOrder: this.textures.length,
            nameRange: {
                startLine: nameTok.line, endLine: nameTok.line,
                startCol: nameTok.col, endCol: nameTok.col + nameTok.value.length,
            },
            range: { startLine: kw.line, endLine: pathTok.line, startCol: kw.col, endCol: pathTok.col + pathTok.value.length + 1 },
        };

        // 可选 { filter = ..., wrap = ... }
        const peek = this.peekToken();
        if (peek.type === TokenType.LBrace) {
            this.nextToken(); // consume {
            while (true) {
                const p = this.peekToken();
                if (p.type === TokenType.RBrace) {
                    const rb = this.nextToken();
                    tex.range.endLine = rb.line;
                    tex.range.endCol = rb.col + 1;
                    break;
                }
                if (p.type === TokenType.Eof) { this.addError(p, 'unexpected end of file in texture config'); break; }
                if (this.isTopLevelKeyword(p.type)) { this.addError(p, "missing '}' for texture config block"); break; }

                const key = this.expect(TokenType.Identifier, 'texture config key');
                if (!key) { this.skipToBlockEnd(); break; }
                if (!this.expect(TokenType.Equals, `after texture config key '${key.value}'`)) {
                    this.skipPastCurrentLine(key);
                    continue;
                }
                const val = this.expect(TokenType.StringLiteral, `value for texture config key '${key.value}'`);
                if (!val) {
                    this.skipPastCurrentLine(key);
                    continue;
                }

                if (key.value === 'filter') {
                    if (val.value !== 'linear' && val.value !== 'point') {
                        this.addError(val, 'filter must be "linear" or "point"');
                        continue;
                    }
                    tex.filter = val.value;
                } else if (key.value === 'wrap') {
                    if (val.value !== 'repeat' && val.value !== 'clamp' && val.value !== 'mirror') {
                        this.addError(val, 'wrap must be "repeat", "clamp", or "mirror"');
                        continue;
                    }
                    tex.wrap = val.value;
                } else {
                    this.addError(key, `unknown texture config key '${key.value}'`);
                    continue;
                }
            }
        }

        this.usedNames.add(tex.name);
        this.textures.push(tex);
    }

    // ---- common ----

    private parseCommonBlock(): void {
        const kw = this.nextToken();
        if (this.common) {
            this.addError(kw, "duplicate 'common' block (at most one allowed)");
            this.skipToNextTopLevel();
            return;
        }

        const ls = this.nextToken();
        if (ls.type !== TokenType.LongString) {
            this.addError(ls, "expected long string [=[ ... ]=] after 'common'");
            // 把 token 推回去，让 skipToNextTopLevel 正确处理
            this.peeked = ls;
            this.skipToNextTopLevel();
            return;
        }

        const { contentStartLine, contentEndLine } = computeLongStringContentRange(ls);

        this.common = {
            code: ls.value,
            codeRange: {
                startLine: contentStartLine,
                endLine: contentEndLine,
                startCol: 0,
                endCol: 0,
            },
            range: {
                startLine: kw.line,
                endLine: contentEndLine,
                startCol: kw.col,
                endCol: 0,
            },
        };
    }

    // ---- pass ----

    private parsePassBlock(): void {
        const kw = this.nextToken();
        const nameTok = this.expect(TokenType.Identifier, 'pass name');
        if (!nameTok) { this.skipToNextTopLevel(); return; }

        if (isReservedName(nameTok.value)) {
            this.addError(nameTok, `'${nameTok.value}' is a reserved name and cannot be used as a pass name`);
            this.skipToNextTopLevel();
            return;
        }
        if (this.usedNames.has(nameTok.value)) {
            this.addError(nameTok, `name '${nameTok.value}' is already used by another texture or pass`);
            this.skipToNextTopLevel();
            return;
        }

        const lb = this.expect(TokenType.LBrace, 'after pass name');
        if (!lb) { this.skipToNextTopLevel(); return; }

        const pass: StoyPass = {
            name: nameTok.value,
            code: '',
            init: { type: 'none', color: [0, 0, 0, 0], texturePath: '' },
            declOrder: this.passes.length,
            nameRange: {
                startLine: nameTok.line, endLine: nameTok.line,
                startCol: nameTok.col, endCol: nameTok.col + nameTok.value.length,
            },
            codeRange: { startLine: 0, endLine: 0, startCol: 0, endCol: 0 },
            range: { startLine: kw.line, endLine: kw.line, startCol: kw.col, endCol: kw.col },
        };

        let hasCode = false;

        while (true) {
            const peek = this.peekToken();
            if (peek.type === TokenType.RBrace) {
                const rb = this.nextToken();
                pass.range.endLine = rb.line;
                pass.range.endCol = rb.col + 1;
                break;
            }
            if (peek.type === TokenType.Eof) { this.addError(peek, 'unexpected end of file in pass block'); break; }
            if (this.isTopLevelKeyword(peek.type)) { this.addError(peek, `missing '}' for pass '${pass.name}' block`); break; }

            if (peek.type === TokenType.KW_init) {
                this.nextToken(); // consume 'init'
                if (!this.expect(TokenType.Equals, "after 'init'")) {
                    this.skipToBlockEnd();
                    break;
                }
                this.parsePassInit(pass.init);
            } else if (peek.type === TokenType.KW_code) {
                this.nextToken(); // consume 'code'
                const ls = this.nextToken();
                if (ls.type !== TokenType.LongString) {
                    this.addError(ls, "expected long string [=[ ... ]=] after 'code'");
                    // 推回 token 让块循环正确处理
                    this.peeked = ls;
                    continue;
                }
                pass.code = ls.value;
                const { contentStartLine, contentEndLine } = computeLongStringContentRange(ls);
                pass.codeRange = {
                    startLine: contentStartLine,
                    endLine: contentEndLine,
                    startCol: 0,
                    endCol: 0,
                };
                hasCode = true;
            } else {
                this.addError(peek, `expected 'init' or 'code' in pass block, got '${peek.value}'`);
                this.nextToken(); // 消耗意外 token
                continue; // 跳过继续
            }
        }

        if (!hasCode) {
            this.addError(
                { type: TokenType.Eof, value: '', line: pass.range.endLine, col: pass.range.endCol },
                `pass '${pass.name}' is missing required 'code' block`
            );
        }

        this.usedNames.add(pass.name);
        this.passes.push(pass);
    }

    // ---- init 值 ----

    private parsePassInit(init: StoyPassInit): void {
        const peek = this.peekToken();

        if (peek.type === TokenType.KW_float4) {
            this.nextToken(); // consume 'float4'
            init.type = 'color';
            this.parseFloat4(init.color);
            return;
        }

        if (peek.type === TokenType.KW_texture) {
            this.nextToken(); // consume 'texture'
            const pathTok = this.expect(TokenType.StringLiteral, 'init texture path');
            if (!pathTok) return; // 错误已记录，回到 pass 循环继续
            init.type = 'texture';
            init.texturePath = pathTok.value;
            return;
        }

        this.addError(peek, "expected 'float4(...)' or 'texture \"...\"' for init value");
        this.nextToken(); // 消耗意外 token
    }

    private parseFloat4(out: [number, number, number, number]): void {
        if (!this.expect(TokenType.LParen, "after 'float4'")) return;

        for (let i = 0; i < 4; i++) {
            const num = this.expect(TokenType.NumberLiteral, `float4 component ${i}`);
            if (!num) return; // 错误已记录，回到调用者
            out[i] = parseFloat(num.value);
            if (i < 3) {
                if (!this.expect(TokenType.Comma, 'between float4 components')) return;
            }
        }

        this.expect(TokenType.RParen, 'after float4 components');
    }
}

function tokenTypeName(type: TokenType): string {
    switch (type) {
        case TokenType.LBrace: return "'{'";
        case TokenType.RBrace: return "'}'";
        case TokenType.LParen: return "'('";
        case TokenType.RParen: return "')'";
        case TokenType.Equals: return "'='";
        case TokenType.Comma: return "','";
        case TokenType.Identifier: return 'identifier';
        case TokenType.StringLiteral: return 'string literal';
        case TokenType.NumberLiteral: return 'number';
        case TokenType.LongString: return 'long string [=[ ... ]=]';
        default: return TokenType[type];
    }
}
