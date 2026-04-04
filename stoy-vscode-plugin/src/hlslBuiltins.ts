// ============================================================
// HLSL Builtins — 内置 HLSL 函数/类型签名和文档
// ============================================================

export interface HlslBuiltinFunc {
    name: string;
    signature: string;
    description: string;
    params?: string;
}

export interface HlslBuiltinType {
    name: string;
    description: string;
}

export const HLSL_TYPES: HlslBuiltinType[] = [
    // Scalar
    { name: 'bool', description: 'Boolean (true/false)' },
    { name: 'int', description: '32-bit signed integer' },
    { name: 'uint', description: '32-bit unsigned integer' },
    { name: 'float', description: '32-bit floating point' },
    { name: 'half', description: '16-bit floating point' },
    { name: 'double', description: '64-bit floating point' },
    { name: 'void', description: 'No return value' },
    // Vector
    { name: 'float2', description: '2-component float vector' },
    { name: 'float3', description: '3-component float vector' },
    { name: 'float4', description: '4-component float vector' },
    { name: 'int2', description: '2-component int vector' },
    { name: 'int3', description: '3-component int vector' },
    { name: 'int4', description: '4-component int vector' },
    { name: 'uint2', description: '2-component uint vector' },
    { name: 'uint3', description: '3-component uint vector' },
    { name: 'uint4', description: '4-component uint vector' },
    { name: 'bool2', description: '2-component bool vector' },
    { name: 'bool3', description: '3-component bool vector' },
    { name: 'bool4', description: '4-component bool vector' },
    { name: 'half2', description: '2-component half vector' },
    { name: 'half3', description: '3-component half vector' },
    { name: 'half4', description: '4-component half vector' },
    // Matrix
    { name: 'float2x2', description: '2×2 float matrix' },
    { name: 'float3x3', description: '3×3 float matrix' },
    { name: 'float4x4', description: '4×4 float matrix' },
    { name: 'float3x4', description: '3×4 float matrix' },
    { name: 'float4x3', description: '4×3 float matrix' },
    // Texture / Sampler
    { name: 'Texture2D', description: '2D texture resource' },
    { name: 'Texture3D', description: '3D texture resource' },
    { name: 'TextureCube', description: 'Cube texture resource' },
    { name: 'Texture2DArray', description: '2D texture array resource' },
    { name: 'SamplerState', description: 'Texture sampler state' },
    { name: 'SamplerComparisonState', description: 'Comparison sampler state' },
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
    { name: 'determinant', signature: 'float determinant(floatNxN m)', description: 'Returns the determinant of a square matrix.' },
    // Misc math
    { name: 'mad', signature: 'T mad(T a, T b, T c)', description: 'Returns a × b + c (fused multiply-add).' },
    { name: 'rcp', signature: 'T rcp(T x)', description: 'Returns 1/x (reciprocal).' },
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
    { name: 'Sample', signature: 'float4 tex.Sample(SamplerState s, float2 uv)', description: 'Samples a texture at the given UV coordinates.', params: '`s` — sampler state\n\n`uv` — texture coordinates' },
    { name: 'SampleLevel', signature: 'float4 tex.SampleLevel(SamplerState s, float2 uv, float lod)', description: 'Samples a texture at a specific mip level.' },
    { name: 'SampleGrad', signature: 'float4 tex.SampleGrad(SamplerState s, float2 uv, float2 ddx, float2 ddy)', description: 'Samples a texture with explicit gradients.' },
    { name: 'SampleCmp', signature: 'float tex.SampleCmp(SamplerComparisonState s, float2 uv, float cmpVal)', description: 'Samples and compares (shadow mapping).' },
    { name: 'Load', signature: 'float4 tex.Load(int3 location)', description: 'Reads a texel directly by integer coordinates (x, y, mipLevel).' },
    { name: 'GetDimensions', signature: 'void tex.GetDimensions(out uint w, out uint h)', description: 'Returns the texture dimensions.' },
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
