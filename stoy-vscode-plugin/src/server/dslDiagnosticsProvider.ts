// ============================================================
// DSL Diagnostics Provider — .stoy DSL 层诊断
// ============================================================

import { Diagnostic, DiagnosticSeverity, Range } from 'vscode-languageserver';
import { StoyDocument, BlockRange } from '../types';

/** 将解析器的诊断转换为 LSP 诊断 */
export function provideDslDiagnostics(doc: StoyDocument): Diagnostic[] {
    return doc.diagnostics.map(d => ({
        range: blockRangeToRange(d.range),
        message: d.message,
        severity: severityMap[d.severity],
        source: 'stoy',
    }));
}

const severityMap: Record<string, DiagnosticSeverity> = {
    'error': DiagnosticSeverity.Error,
    'warning': DiagnosticSeverity.Warning,
    'info': DiagnosticSeverity.Information,
};

function blockRangeToRange(br: BlockRange): Range {
    return Range.create(br.startLine, br.startCol, br.endLine, br.endCol);
}
