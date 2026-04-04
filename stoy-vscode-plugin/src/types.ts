// ============================================================
// .stoy Language Support — Shared Type Definitions
// ============================================================

/** 代码块在物理文件中的位置范围（行号 0-based） */
export interface BlockRange {
    startLine: number;
    endLine: number;
    startCol: number;
    endCol: number;
}

/** 全局渲染设置 */
export interface StoyGlobalSetting {
    format: string;
    msaa: number;
    vsync: boolean;
    resolutionScale: number;
    range: BlockRange;
}

/** 外部纹理声明 */
export interface StoyTexture {
    name: string;
    path: string;
    filter: string;   // "linear" | "point"
    wrap: string;      // "repeat" | "clamp" | "mirror"
    declOrder: number;
    nameRange: BlockRange;
    range: BlockRange;
}

/** Pass 初始值 */
export interface StoyPassInit {
    type: 'none' | 'color' | 'texture';
    color: [number, number, number, number];
    texturePath: string;
}

/** Pass 数据 */
export interface StoyPass {
    name: string;
    code: string;
    init: StoyPassInit;
    declOrder: number;
    nameRange: BlockRange;
    codeRange: BlockRange;   // code [=[ 之后第一行到 ]=] 之前最后一行
    range: BlockRange;       // 整个 pass 块
}

/** Common 块 */
export interface StoyCommon {
    code: string;
    codeRange: BlockRange;   // [=[ 之后第一行到 ]=] 之前最后一行
    range: BlockRange;       // 整个 common 块
}

/** Inner vars 块 */
export interface StoyInnerVars {
    vars: string[];
    range: BlockRange;
    /** 每个变量名的位置（用于跳转和悬停） */
    varRanges: Map<string, BlockRange>;
}

/** 诊断信息 */
export interface StoyDiagnostic {
    range: BlockRange;
    message: string;
    severity: 'error' | 'warning' | 'info';
}

/** 解析后的 .stoy 文档 */
export interface StoyDocument {
    globalSetting?: StoyGlobalSetting;
    innerVars?: StoyInnerVars;
    textures: StoyTexture[];
    common?: StoyCommon;
    passes: StoyPass[];
    diagnostics: StoyDiagnostic[];
}

/** 虚拟文档映射信息 */
export interface VirtualDocInfo {
    /** 虚拟文档 URI */
    uri: string;
    /** 注入代码的行数（cbuffer + aliases + textures 等） */
    prefixLineCount: number;
    /** 对应的物理文件代码块范围 */
    blockRange: BlockRange;
    /** 块类型 */
    blockType: 'common' | 'pass';
    /** pass 名称（blockType='pass' 时有效） */
    passName?: string;
}

// ============================================================
// 内置变量信息表
// ============================================================

export interface BuiltinVarInfo {
    name: string;
    hlslType: string;
    description: string;
}

export const BUILTIN_VARS: BuiltinVarInfo[] = [
    { name: 'iResolution', hlslType: 'float3', description: '视口分辨率 (width, height, 1.0)' },
    { name: 'iTime', hlslType: 'float', description: '运行时间（秒）' },
    { name: 'iTimeDelta', hlslType: 'float', description: '帧间隔时间（秒）' },
    { name: 'iFrame', hlslType: 'int', description: '当前帧号' },
    { name: 'iFrameRate', hlslType: 'float', description: '当前帧率' },
    { name: 'iMouse', hlslType: 'float4', description: '鼠标状态 (xy=当前位置, zw=点击位置)' },
    { name: 'iDate', hlslType: 'float4', description: '日期 (year, month, day, seconds)' },
    { name: 'iSampleRate', hlslType: 'float', description: '音频采样率（固定 44100）' },
];

export const BUILTIN_VAR_NAMES = new Set(BUILTIN_VARS.map(v => v.name));

// ============================================================
// .stoy 关键字和保留字
// ============================================================

export const STOY_KEYWORDS = new Set([
    'global_setting', 'inner_vars', 'texture', 'common', 'pass',
    'init', 'code', 'float4', 'true', 'false',
]);

export const HLSL_RESERVED = new Set([
    'float', 'float2', 'float3', 'float4', 'int', 'int2', 'int3', 'int4',
    'uint', 'uint2', 'uint3', 'uint4', 'bool', 'half', 'double', 'void',
    'return', 'if', 'else', 'for', 'while', 'do', 'switch', 'case',
    'break', 'continue', 'discard', 'struct', 'cbuffer', 'static', 'const',
    'inout', 'in', 'out', 'Texture2D', 'Texture3D', 'TextureCube',
    'SamplerState', 'SV_Position', 'SV_Target0', 'register',
    'main', 'mainImage',
]);

/** 所有保留字 = stoy 关键字 + 内置变量名 + HLSL 保留字 */
export function isReservedName(name: string): boolean {
    return STOY_KEYWORDS.has(name) || BUILTIN_VAR_NAMES.has(name) || HLSL_RESERVED.has(name);
}

// ============================================================
// DSL 属性值补全数据
// ============================================================

export const FILTER_VALUES = ['linear', 'point'];
export const WRAP_VALUES = ['repeat', 'clamp', 'mirror'];
export const GLOBAL_SETTING_KEYS = ['format', 'msaa', 'vsync', 'resolution_scale'];
export const TEXTURE_CONFIG_KEYS = ['filter', 'wrap'];
