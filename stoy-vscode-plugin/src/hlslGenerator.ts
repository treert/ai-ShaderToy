// ============================================================
// HLSL Virtual Document Generator
// 从 StoyDocument 为每个 code/common 块生成完整的虚拟 HLSL 文档。
// 移植自 C++ stoy_hlsl_generator.cpp 的核心逻辑。
// ============================================================

import { StoyDocument, BUILTIN_VARS, BUILTIN_VAR_NAMES } from './types';

/** 虚拟文档生成结果 */
export interface HlslVirtualDoc {
    /** 虚拟文档完整内容 */
    content: string;
    /** 注入代码的行数（content 中用户代码之前的行数） */
    prefixLineCount: number;
}

// ============================================================
// cbuffer ShaderToyUniforms : register(b0)
// 固定 208 字节布局，与 C++ ShaderToyConstants 结构体对齐
// ============================================================

const CBUFFER_B0 = `cbuffer ShaderToyUniforms : register(b0) {
    float4 _iResolution4;       // xyz=resolution, w=padding
    float  _iTime;
    float  _iTimeDelta;
    int    _iFrame;
    float  _iFrameRate;
    float4 _iMouse;
    float4 _iDate;
    float  _iSampleRate;
    float  _iClickTime;
    float2 _pad0;
    float4 _iChannelTime;
    float4 _iChannelResolution[4];
    float4 _cubeFaceRight;
    float4 _cubeFaceUp;
    float4 _cubeFaceDir;
};
`;

// ============================================================
// inner_vars 别名映射表
// ============================================================

const INNER_VAR_ALIASES: Record<string, string> = {
    'iResolution': 'static float3 iResolution = _iResolution4.xyz;',
    'iTime': 'static float iTime = _iTime;',
    'iTimeDelta': 'static float iTimeDelta = _iTimeDelta;',
    'iFrame': 'static int iFrame = _iFrame;',
    'iFrameRate': 'static float iFrameRate = _iFrameRate;',
    'iMouse': 'static float4 iMouse = _iMouse;',
    'iDate': 'static float4 iDate = _iDate;',
    'iSampleRate': 'static float iSampleRate = _iSampleRate;',
};

/**
 * 为指定 pass 生成完整的虚拟 HLSL 文档
 */
export function generatePassVirtualDoc(doc: StoyDocument, passIndex: number): HlslVirtualDoc {
    const lines: string[] = [];

    // 1. cbuffer B0
    lines.push(...CBUFFER_B0.split('\n'));

    // 2. inner_vars 别名
    if (doc.innerVars && doc.innerVars.vars.length > 0) {
        lines.push('// --- inner_vars aliases ---');
        for (const v of doc.innerVars.vars) {
            const alias = INNER_VAR_ALIASES[v];
            if (alias) lines.push(alias);
        }
        lines.push('');
    }

    // 3. cbuffer TextureParams : register(b1)
    const hasTexelSize = doc.textures.length > 0 || doc.passes.length > 1;
    if (hasTexelSize) {
        lines.push('cbuffer TextureParams : register(b1) {');
        for (const tex of doc.textures) {
            lines.push(`    float4 ${tex.name}_TexelSize;`);
        }
        for (const pass of doc.passes) {
            lines.push(`    float4 ${pass.name}_TexelSize;`);
        }
        lines.push('};');
        lines.push('');
    }

    // 4. 外部纹理声明
    let slot = 0;
    if (doc.textures.length > 0) {
        lines.push('// --- external textures ---');
        for (const tex of doc.textures) {
            lines.push(`Texture2D<float4> ${tex.name} : register(t${slot});`);
            lines.push(`SamplerState ${tex.name}_Sampler : register(s${slot});`);
            slot++;
        }
        lines.push('');
    }

    // 5. Pass 输出纹理声明
    if (doc.passes.length > 0) {
        lines.push('// --- pass output textures ---');
        for (const pass of doc.passes) {
            lines.push(`Texture2D<float4> ${pass.name} : register(t${slot});`);
            lines.push(`SamplerState ${pass.name}_Sampler : register(s${slot});`);
            slot++;
        }
        lines.push('');
    }

    // 6. common 代码
    if (doc.common && doc.common.code.trim()) {
        lines.push('// --- common code ---');
        lines.push(...doc.common.code.split('\n'));
        lines.push('');
    }

    const prefixLineCount = lines.length;

    // 7. 用户 pass 代码
    if (passIndex >= 0 && passIndex < doc.passes.length) {
        lines.push(`// --- pass code: ${doc.passes[passIndex].name} ---`);
        // prefixLineCount 需要包含这个注释行
        const actualPrefixLineCount = lines.length;
        lines.push(...doc.passes[passIndex].code.split('\n'));
        lines.push('');

        // 8. main 入口包装
        const isLast = passIndex === doc.passes.length - 1;
        lines.push('// --- entry point ---');
        lines.push('struct PS_INPUT {');
        lines.push('    float4 pos : SV_Position;');
        lines.push('};');
        lines.push('');
        lines.push('float4 main(PS_INPUT input) : SV_Target0 {');
        if (isLast) {
            lines.push('    float2 fragCoord = float2(input.pos.x, _iResolution4.y - input.pos.y);');
        } else {
            lines.push('    float2 fragCoord = float2(input.pos.x, input.pos.y);');
        }
        lines.push('    float4 fragColor = float4(0, 0, 0, 1);');
        lines.push('    mainImage(fragColor, fragCoord);');
        lines.push('    return fragColor;');
        lines.push('}');

        return { content: lines.join('\n'), prefixLineCount: actualPrefixLineCount };
    }

    return { content: lines.join('\n'), prefixLineCount };
}

/**
 * 为 common 块生成虚拟 HLSL 文档
 * common 块不包含 pass 代码和 main 入口，只包含 cbuffer + aliases + textures + common 代码
 */
export function generateCommonVirtualDoc(doc: StoyDocument): HlslVirtualDoc {
    const lines: string[] = [];

    // 1. cbuffer B0
    lines.push(...CBUFFER_B0.split('\n'));

    // 2. inner_vars 别名
    if (doc.innerVars && doc.innerVars.vars.length > 0) {
        lines.push('// --- inner_vars aliases ---');
        for (const v of doc.innerVars.vars) {
            const alias = INNER_VAR_ALIASES[v];
            if (alias) lines.push(alias);
        }
        lines.push('');
    }

    // 3. cbuffer TextureParams
    const hasTexelSize = doc.textures.length > 0 || doc.passes.length > 1;
    if (hasTexelSize) {
        lines.push('cbuffer TextureParams : register(b1) {');
        for (const tex of doc.textures) {
            lines.push(`    float4 ${tex.name}_TexelSize;`);
        }
        for (const pass of doc.passes) {
            lines.push(`    float4 ${pass.name}_TexelSize;`);
        }
        lines.push('};');
        lines.push('');
    }

    // 4-5. 纹理声明
    let slot = 0;
    if (doc.textures.length > 0) {
        lines.push('// --- external textures ---');
        for (const tex of doc.textures) {
            lines.push(`Texture2D<float4> ${tex.name} : register(t${slot});`);
            lines.push(`SamplerState ${tex.name}_Sampler : register(s${slot});`);
            slot++;
        }
        lines.push('');
    }
    if (doc.passes.length > 0) {
        lines.push('// --- pass output textures ---');
        for (const pass of doc.passes) {
            lines.push(`Texture2D<float4> ${pass.name} : register(t${slot});`);
            lines.push(`SamplerState ${pass.name}_Sampler : register(s${slot});`);
            slot++;
        }
        lines.push('');
    }

    const prefixLineCount = lines.length;

    // 6. common 代码
    if (doc.common && doc.common.code.trim()) {
        lines.push('// --- common code ---');
        const actualPrefixLineCount = lines.length;
        lines.push(...doc.common.code.split('\n'));
        return { content: lines.join('\n'), prefixLineCount: actualPrefixLineCount };
    }

    return { content: lines.join('\n'), prefixLineCount };
}
