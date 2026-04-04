// ============================================================
// HLSL Builtins — 内置 HLSL 函数/类型签名和文档
// ============================================================

export interface HlslBuiltinFunc {
    name: string;
    signature: string;
    description: string;
    params?: string;
    /** 覆盖默认 URL 规则的自定义文档链接（不含 base URL） */
    docSlug?: string;
}

export interface HlslBuiltinType {
    name: string;
    description: string;
    /** 文档 URL slug（不含 base URL） */
    docSlug?: string;
}

export const HLSL_TYPES: HlslBuiltinType[] = [
    // Scalar
    { name: 'bool', description: 'Boolean (true/false)', docSlug: 'dx-graphics-hlsl-scalar' },
    { name: 'int', description: '32-bit signed integer', docSlug: 'dx-graphics-hlsl-scalar' },
    { name: 'uint', description: '32-bit unsigned integer', docSlug: 'dx-graphics-hlsl-scalar' },
    { name: 'float', description: '32-bit floating point', docSlug: 'dx-graphics-hlsl-scalar' },
    { name: 'half', description: '16-bit floating point', docSlug: 'dx-graphics-hlsl-scalar' },
    { name: 'double', description: '64-bit floating point', docSlug: 'dx-graphics-hlsl-scalar' },
    { name: 'void', description: 'No return value' },
    // Vector
    { name: 'float2', description: '2-component float vector', docSlug: 'dx-graphics-hlsl-vector' },
    { name: 'float3', description: '3-component float vector', docSlug: 'dx-graphics-hlsl-vector' },
    { name: 'float4', description: '4-component float vector', docSlug: 'dx-graphics-hlsl-vector' },
    { name: 'int2', description: '2-component int vector', docSlug: 'dx-graphics-hlsl-vector' },
    { name: 'int3', description: '3-component int vector', docSlug: 'dx-graphics-hlsl-vector' },
    { name: 'int4', description: '4-component int vector', docSlug: 'dx-graphics-hlsl-vector' },
    { name: 'uint2', description: '2-component uint vector', docSlug: 'dx-graphics-hlsl-vector' },
    { name: 'uint3', description: '3-component uint vector', docSlug: 'dx-graphics-hlsl-vector' },
    { name: 'uint4', description: '4-component uint vector', docSlug: 'dx-graphics-hlsl-vector' },
    { name: 'bool2', description: '2-component bool vector', docSlug: 'dx-graphics-hlsl-vector' },
    { name: 'bool3', description: '3-component bool vector', docSlug: 'dx-graphics-hlsl-vector' },
    { name: 'bool4', description: '4-component bool vector', docSlug: 'dx-graphics-hlsl-vector' },
    { name: 'half2', description: '2-component half vector', docSlug: 'dx-graphics-hlsl-vector' },
    { name: 'half3', description: '3-component half vector', docSlug: 'dx-graphics-hlsl-vector' },
    { name: 'half4', description: '4-component half vector', docSlug: 'dx-graphics-hlsl-vector' },
    // Matrix
    { name: 'float2x2', description: '2×2 float matrix', docSlug: 'dx-graphics-hlsl-matrix' },
    { name: 'float3x3', description: '3×3 float matrix', docSlug: 'dx-graphics-hlsl-matrix' },
    { name: 'float4x4', description: '4×4 float matrix', docSlug: 'dx-graphics-hlsl-matrix' },
    { name: 'float3x4', description: '3×4 float matrix', docSlug: 'dx-graphics-hlsl-matrix' },
    { name: 'float4x3', description: '4×3 float matrix', docSlug: 'dx-graphics-hlsl-matrix' },
    // Texture / Sampler
    { name: 'Texture2D', description: '2D texture resource', docSlug: 'sm5-object-texture2d' },
    { name: 'Texture3D', description: '3D texture resource', docSlug: 'sm5-object-texture3d' },
    { name: 'TextureCube', description: 'Cube texture resource', docSlug: 'texturecube' },
    { name: 'Texture2DArray', description: '2D texture array resource', docSlug: 'sm5-object-texture2darray' },
    { name: 'SamplerState', description: 'Texture sampler state', docSlug: 'dx-graphics-hlsl-sampler' },
    { name: 'SamplerComparisonState', description: 'Comparison sampler state', docSlug: 'dx-graphics-hlsl-sampler' },
];

export const HLSL_FUNCTIONS: HlslBuiltinFunc[] = [
    // Math — trigonometric
    { name: 'abs', signature: 'T abs(T x)', description: 'Returns the absolute value of x.' },
    { name: 'acos', signature: 'T acos(T x)', description: 'Returns the arccosine of x (in radians).' },
    { name: 'asin', signature: 'T asin(T x)', description: 'Returns the arcsine of x (in radians).' },
    { name: 'atan', signature: 'T atan(T y_over_x)', description: 'Returns the arctangent.' },
    { name: 'atan2', signature: 'T atan2(T y, T x)', description: 'Returns the arctangent of y/x, using signs to determine quadrant.' },
    { name: 'cos', signature: 'T cos(T x)', description: 'Returns the cosine of x (in radians).' },
    { name: 'cosh', signature: 'T cosh(T x)', description: 'Returns the hyperbolic cosine of x.' },
    { name: 'sin', signature: 'T sin(T x)', description: 'Returns the sine of x (in radians).' },
    { name: 'sincos', signature: 'void sincos(T x, out T s, out T c)', description: 'Simultaneously computes sin(x) and cos(x).' },
    { name: 'sinh', signature: 'T sinh(T x)', description: 'Returns the hyperbolic sine of x.' },
    { name: 'tan', signature: 'T tan(T x)', description: 'Returns the tangent of x (in radians).' },
    { name: 'tanh', signature: 'T tanh(T x)', description: 'Returns the hyperbolic tangent of x.' },
    // Math — exponential
    { name: 'exp', signature: 'T exp(T x)', description: 'Returns e^x.' },
    { name: 'exp2', signature: 'T exp2(T x)', description: 'Returns 2^x.' },
    { name: 'log', signature: 'T log(T x)', description: 'Returns the natural logarithm of x.' },
    { name: 'log2', signature: 'T log2(T x)', description: 'Returns the base-2 logarithm of x.' },
    { name: 'log10', signature: 'T log10(T x)', description: 'Returns the base-10 logarithm of x.' },
    { name: 'pow', signature: 'T pow(T base, T exp)', description: 'Returns base^exp.', params: '`base` — base value\n\n`exp` — exponent' },
    { name: 'sqrt', signature: 'T sqrt(T x)', description: 'Returns the square root of x.' },
    { name: 'rsqrt', signature: 'T rsqrt(T x)', description: 'Returns 1/sqrt(x).' },
    { name: 'ldexp', signature: 'T ldexp(T x, T exp)', description: 'Returns x × 2^exp.' },
    // Math — rounding
    { name: 'ceil', signature: 'T ceil(T x)', description: 'Returns the smallest integer ≥ x.' },
    { name: 'floor', signature: 'T floor(T x)', description: 'Returns the largest integer ≤ x.' },
    { name: 'round', signature: 'T round(T x)', description: 'Rounds x to the nearest integer.' },
    { name: 'trunc', signature: 'T trunc(T x)', description: 'Truncates x to the integer part (toward zero).' },
    { name: 'frac', signature: 'T frac(T x)', description: 'Returns the fractional part of x (x - floor(x)).' },
    { name: 'fmod', signature: 'T fmod(T x, T y)', description: 'Returns the floating-point remainder of x/y.' },
    { name: 'modf', signature: 'T modf(T x, out T intPart)', description: 'Splits x into integer and fractional parts.' },
    // Math — clamping
    { name: 'clamp', signature: 'T clamp(T x, T minVal, T maxVal)', description: 'Clamps x to [minVal, maxVal].', params: '`x` — value\n\n`minVal` — lower bound\n\n`maxVal` — upper bound' },
    { name: 'saturate', signature: 'T saturate(T x)', description: 'Clamps x to [0, 1].' },
    { name: 'max', signature: 'T max(T a, T b)', description: 'Returns the larger of a and b.' },
    { name: 'min', signature: 'T min(T a, T b)', description: 'Returns the smaller of a and b.' },
    { name: 'sign', signature: 'T sign(T x)', description: 'Returns -1, 0, or 1 based on the sign of x.' },
    // Math — interpolation
    { name: 'lerp', signature: 'T lerp(T a, T b, T t)', description: 'Linear interpolation: a + t × (b - a).', params: '`a` — start\n\n`b` — end\n\n`t` — factor [0, 1]' },
    { name: 'smoothstep', signature: 'T smoothstep(T edge0, T edge1, T x)', description: 'Smooth Hermite interpolation when edge0 < x < edge1.', params: '`edge0` — lower edge\n\n`edge1` — upper edge\n\n`x` — source value' },
    { name: 'step', signature: 'T step(T edge, T x)', description: 'Returns 0 if x < edge, else 1.' },
    // Vector
    { name: 'cross', signature: 'float3 cross(float3 a, float3 b)', description: 'Returns the cross product of two 3D vectors.' },
    { name: 'dot', signature: 'float dot(T a, T b)', description: 'Returns the dot product of two vectors.' },
    { name: 'distance', signature: 'float distance(T a, T b)', description: 'Returns the Euclidean distance between a and b.' },
    { name: 'length', signature: 'float length(T x)', description: 'Returns the length (magnitude) of a vector.' },
    { name: 'normalize', signature: 'T normalize(T x)', description: 'Returns the unit vector in the direction of x.' },
    { name: 'reflect', signature: 'T reflect(T incident, T normal)', description: 'Returns the reflection vector.' },
    { name: 'refract', signature: 'T refract(T incident, T normal, float eta)', description: 'Returns the refraction vector.', params: '`incident` — incoming direction\n\n`normal` — surface normal\n\n`eta` — ratio of indices of refraction' },
    // Matrix
    { name: 'mul', signature: 'T mul(T a, T b)', description: 'Matrix/vector multiplication.' },
    { name: 'transpose', signature: 'floatNxM transpose(floatMxN m)', description: 'Returns the transpose of a matrix.' },
    { name: 'determinant', signature: 'float determinant(floatNxN m)', description: 'Returns the determinant of a square matrix.', docSlug: 'determinant' },
    // Misc math
    { name: 'mad', signature: 'T mad(T a, T b, T c)', description: 'Returns a × b + c (fused multiply-add).', docSlug: 'mad' },
    { name: 'rcp', signature: 'T rcp(T x)', description: 'Returns 1/x (reciprocal).', docSlug: 'rcp' },
    { name: 'degrees', signature: 'T degrees(T radians)', description: 'Converts radians to degrees.' },
    { name: 'radians', signature: 'T radians(T degrees)', description: 'Converts degrees to radians.' },
    // Derivative
    { name: 'ddx', signature: 'T ddx(T x)', description: 'Partial derivative with respect to screen-space x.' },
    { name: 'ddy', signature: 'T ddy(T x)', description: 'Partial derivative with respect to screen-space y.' },
    { name: 'fwidth', signature: 'T fwidth(T x)', description: 'Returns abs(ddx(x)) + abs(ddy(x)).' },
    // Boolean
    { name: 'all', signature: 'bool all(T x)', description: 'Returns true if all components of x are non-zero.' },
    { name: 'any', signature: 'bool any(T x)', description: 'Returns true if any component of x is non-zero.' },
    { name: 'isfinite', signature: 'bool isfinite(T x)', description: 'Returns true if x is finite.' },
    { name: 'isinf', signature: 'bool isinf(T x)', description: 'Returns true if x is infinite.' },
    { name: 'isnan', signature: 'bool isnan(T x)', description: 'Returns true if x is NaN.' },
    // Texture methods
    { name: 'Sample', signature: 'float4 tex.Sample(SamplerState s, float2 uv)', description: 'Samples a texture at the given UV coordinates.', params: '`s` — sampler state\n\n`uv` — texture coordinates', docSlug: 'dx-graphics-hlsl-to-sample' },
    { name: 'SampleLevel', signature: 'float4 tex.SampleLevel(SamplerState s, float2 uv, float lod)', description: 'Samples a texture at a specific mip level.', docSlug: 'dx-graphics-hlsl-to-samplelevel' },
    { name: 'SampleGrad', signature: 'float4 tex.SampleGrad(SamplerState s, float2 uv, float2 ddx, float2 ddy)', description: 'Samples a texture with explicit gradients.', docSlug: 'dx-graphics-hlsl-to-samplegrad' },
    { name: 'SampleCmp', signature: 'float tex.SampleCmp(SamplerComparisonState s, float2 uv, float cmpVal)', description: 'Samples and compares (shadow mapping).', docSlug: 'dx-graphics-hlsl-to-samplecmp' },
    { name: 'Load', signature: 'float4 tex.Load(int3 location)', description: 'Reads a texel directly by integer coordinates (x, y, mipLevel).', docSlug: 'dx-graphics-hlsl-to-load' },
    { name: 'GetDimensions', signature: 'void tex.GetDimensions(out uint w, out uint h)', description: 'Returns the texture dimensions.', docSlug: 'dx-graphics-hlsl-to-getdimensions' },
];

// ---- 快速查找表 ----

const _funcMap = new Map<string, HlslBuiltinFunc>();
for (const f of HLSL_FUNCTIONS) _funcMap.set(f.name, f);

const _typeMap = new Map<string, HlslBuiltinType>();
for (const t of HLSL_TYPES) _typeMap.set(t.name, t);

export function findHlslFunction(name: string): HlslBuiltinFunc | undefined {
    return _funcMap.get(name);
}

export function findHlslType(name: string): HlslBuiltinType | undefined {
    return _typeMap.get(name);
}

// ---- 文档 URL 生成 ----

const HLSL_DOC_BASE = 'https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl';

/**
 * 获取内置函数的微软官方文档 URL。
 * 默认规则：dx-graphics-hlsl-<name>；少数例外通过 docSlug 覆盖。
 */
export function getHlslDocUrl(func: HlslBuiltinFunc): string {
    const slug = func.docSlug ?? `dx-graphics-hlsl-${func.name.toLowerCase()}`;
    return `${HLSL_DOC_BASE}/${slug}`;
}

/**
 * 获取内置类型的微软官方文档 URL。
 * 没有 docSlug 的类型（如 void）返回 undefined。
 */
export function getHlslTypeDocUrl(type: HlslBuiltinType): string | undefined {
    if (!type.docSlug) return undefined;
    return `${HLSL_DOC_BASE}/${type.docSlug}`;
}
