// ============================================================
// Document Manager — .stoy 文档状态缓存管理
// 监听文档变更，立即更新缓存，防抖发送诊断。
// ============================================================

import { TextDocument } from 'vscode-languageserver-textdocument';
import { StoyDocument } from '../types';
import { StoyParser } from '../stoyParser';

export class DocumentManager {
    private parser = new StoyParser();
    private cache = new Map<string, StoyDocument>();
    private debounceTimers = new Map<string, ReturnType<typeof setTimeout>>();

    /** 获取文档的解析结果（如果缓存中有，直接返回；否则立即解析） */
    getDocument(uri: string, text: string): StoyDocument {
        let doc = this.cache.get(uri);
        if (!doc) {
            doc = this.parser.parse(text);
            this.cache.set(uri, doc);
        }
        return doc;
    }

    /**
     * 文档变更时：立即解析更新缓存（保证补全/悬停使用最新数据），
     * 但诊断回调使用 300ms 防抖（避免高频编辑时反复推送诊断）。
     */
    onDocumentChanged(uri: string, text: string, callback?: (doc: StoyDocument) => void): void {
        // 立即解析并更新缓存
        const doc = this.parser.parse(text);
        this.cache.set(uri, doc);

        // 防抖发送诊断
        const existing = this.debounceTimers.get(uri);
        if (existing) clearTimeout(existing);

        this.debounceTimers.set(uri, setTimeout(() => {
            this.debounceTimers.delete(uri);
            callback?.(doc);
        }, 300));
    }

    /** 立即解析并缓存 */
    parseImmediate(uri: string, text: string): StoyDocument {
        const doc = this.parser.parse(text);
        this.cache.set(uri, doc);
        return doc;
    }

    /** 文档关闭时清除缓存 */
    removeDocument(uri: string): void {
        this.cache.delete(uri);
        const timer = this.debounceTimers.get(uri);
        if (timer) {
            clearTimeout(timer);
            this.debounceTimers.delete(uri);
        }
    }

    /** 获取缓存的解析结果（可能不存在） */
    getCached(uri: string): StoyDocument | undefined {
        return this.cache.get(uri);
    }
}
