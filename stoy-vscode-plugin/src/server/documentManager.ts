// ============================================================
// Document Manager — .stoy 文档状态缓存管理
// 监听文档变更，立即更新缓存，防抖发送诊断。
// ============================================================

import { TextDocument } from 'vscode-languageserver-textdocument';
import { StoyDocument } from '../types';
import { StoyParser } from '../stoyParser';

export class DocumentManager {
    private parser = new StoyParser();
    /** 最新解析结果（可能含错误/残缺，用于诊断） */
    private cache = new Map<string, StoyDocument>();
    /** 最近一次有效结构的解析结果（用于 Outline/补全/hover 等） */
    private stableCache = new Map<string, StoyDocument>();
    private debounceTimers = new Map<string, ReturnType<typeof setTimeout>>();

    /** 获取稳定的文档解析结果（优先返回有结构的版本，避免编辑中 Outline 闪烁） */
    getDocument(uri: string, text: string): StoyDocument {
        const stable = this.stableCache.get(uri);
        if (stable) return stable;
        let doc = this.cache.get(uri);
        if (!doc) {
            doc = this.parser.parse(text);
            this.cache.set(uri, doc);
            if (isStructurallyValid(doc)) {
                this.stableCache.set(uri, doc);
            }
        }
        return doc;
    }

    /**
     * 文档变更时：防抖发送诊断。
     * 注意：调用前应先调用 parseImmediate 更新缓存。
     */
    scheduleDiagnostics(uri: string, callback: (doc: StoyDocument) => void): void {
        const doc = this.cache.get(uri);
        if (!doc) return;

        // 防抖发送诊断
        const existing = this.debounceTimers.get(uri);
        if (existing) clearTimeout(existing);

        this.debounceTimers.set(uri, setTimeout(() => {
            this.debounceTimers.delete(uri);
            callback(doc);
        }, 300));
    }

    /** 立即解析并缓存；有效结构时同步更新 stableCache */
    parseImmediate(uri: string, text: string): StoyDocument {
        const doc = this.parser.parse(text);
        this.cache.set(uri, doc);
        if (isStructurallyValid(doc)) {
            this.stableCache.set(uri, doc);
        }
        return doc;
    }

    /** 获取稳定版本（用于 Outline 等不希望闪烁的场景） */
    getStable(uri: string): StoyDocument | undefined {
        return this.stableCache.get(uri) ?? this.cache.get(uri);
    }

    /** 文档关闭时清除缓存 */
    removeDocument(uri: string): void {
        this.cache.delete(uri);
        this.stableCache.delete(uri);
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

/** 判断解析结果是否有有效结构（至少有一个有意义的块） */
function isStructurallyValid(doc: StoyDocument): boolean {
    return doc.passes.length > 0
        || doc.common !== undefined
        || doc.innerVars !== undefined
        || doc.textures.length > 0
        || doc.globalSetting !== undefined;
}
