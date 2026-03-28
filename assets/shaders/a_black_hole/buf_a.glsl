// #version 450

// layout(location = 0) out vec4 FragColor;

// layout(set = 0, binding = 0) uniform UniformBuffer
// {
//     float Fov;
//     float TimeRate = 30.;  // 本部分在实际使用时又uniform输入，此外所有iTime*TimeRate应替换为游戏内时间。
//     float MBlackHole = 1.49e7;                                                                          // 单位是太阳质量 本部分在实际使用时uniform输入
//     float a0         = 0.0;                                                                             // 无量纲自旋系数 本部分在实际使用时uniform输入
//     float Rs         = 2. * MBlackHole * kGravityConstant / kSpeedOfLight / kSpeedOfLight * kSolarMass;  // 单位是米 本部分在实际使用时uniform输入
//     float z1       = 1. + pow(1. - a0 * a0, 0.333333333333333) * (pow(1. + a0 * a0, 0.333333333333333) + pow(1. - a0, 0.333333333333333));  // 辅助变量      本部分在实际使用时uniform输入
//     float RmsRatio = (3. + sqrt(3. * a0 * a0 + z1 * z1) - sqrt((3. - z1) * (3. + z1 + 2. * sqrt(3. * a0 * a0 + z1 * z1)))) / 2.;            // 赤道顺行最内稳定圆轨与Rs之比    本部分在实际使用时uniform输入
//     float AccEff   = sqrt(1. - 1. / RmsRatio);                                                                                              // 吸积放能效率,以落到Rms为准 本部分在实际使用时uniform输入
//     float mu      = 1.;                                                                            // 吸积物的比荷的倒数,氕为1 本部分在实际使用时uniform输入
//     float dmdtEdd = 6.327 * mu / kSpeedOfLight / kSpeedOfLight * MBlackHole * kSolarMass / AccEff;  // 爱丁顿吸积率 本部分在实际使用时uniform输入
//     float dmdt = (2e-6) * dmdtEdd;  // 吸积率 本部分在实际使用时uniform输入
//     float diskA = 3. * kGravityConstant * kSolarMass / Rs / Rs / Rs * MBlackHole * dmdt / (8. * kPi * kSigma);  // 吸积盘温度系数 本部分在实际使用时uniform输入
//     // 计算峰值温度的四次方,用于自适应亮度。峰值温度出现在49InterRadius/36处
//     float QuadraticedPeakTemperature = diskA * 0.05665278;  //                                                                                          本部分在实际使用时uniform输入
//     float InterRadius  = 0.7 * RmsRatio * Rs;  // 盘内缘,正常情况下等于最内稳定圆轨
//     float OuterRadius = 12. * Rs;             // 盘外缘 本部分在实际使用时uniform输入
// };

const float kPi              = 3.141592653589;
const float kGravityConstant = 6.673e-11;
const float kSpeedOfLight    = 299792458.0;
const float kSigma           = 5.670373e-8;
const float kLightYear       = 9460730472580800.0;
const float kSolarMass       = 1.9884e30;
const float TEST=1.0;

float RandomStep(vec2 Input, float Seed)
{
    return fract(sin(dot(Input + fract(11.4514 * sin(Seed)), vec2(12.9898, 78.233))) * 43758.5453);
}

float CubicInterpolate(float x)
{
    return 3.0 * x * x - 2.0 * x * x * x;
}

float PerlinNoise(vec3 Position)
{
    vec3 PosInt   = floor(Position);
    vec3 PosFloat = fract(Position);

    float v000 = 2.0 * fract(sin(dot(vec3(PosInt.x,       PosInt.y,       PosInt.z),       vec3(12.9898, 78.233, 213.765))) * 43758.5453) - 1.0;
    float v100 = 2.0 * fract(sin(dot(vec3(PosInt.x + 1.0, PosInt.y,       PosInt.z),       vec3(12.9898, 78.233, 213.765))) * 43758.5453) - 1.0;
    float v010 = 2.0 * fract(sin(dot(vec3(PosInt.x,       PosInt.y + 1.0, PosInt.z),       vec3(12.9898, 78.233, 213.765))) * 43758.5453) - 1.0;
    float v110 = 2.0 * fract(sin(dot(vec3(PosInt.x + 1.0, PosInt.y + 1.0, PosInt.z),       vec3(12.9898, 78.233, 213.765))) * 43758.5453) - 1.0;
    float v001 = 2.0 * fract(sin(dot(vec3(PosInt.x,       PosInt.y,       PosInt.z + 1.0), vec3(12.9898, 78.233, 213.765))) * 43758.5453) - 1.0;
    float v101 = 2.0 * fract(sin(dot(vec3(PosInt.x + 1.0, PosInt.y,       PosInt.z + 1.0), vec3(12.9898, 78.233, 213.765))) * 43758.5453) - 1.0;
    float v011 = 2.0 * fract(sin(dot(vec3(PosInt.x,       PosInt.y + 1.0, PosInt.z + 1.0), vec3(12.9898, 78.233, 213.765))) * 43758.5453) - 1.0;
    float v111 = 2.0 * fract(sin(dot(vec3(PosInt.x + 1.0, PosInt.y + 1.0, PosInt.z + 1.0), vec3(12.9898, 78.233, 213.765))) * 43758.5453) - 1.0;

    float v00 = v001 * CubicInterpolate(PosFloat.z) + v000 * CubicInterpolate(1.0 - PosFloat.z);
    float v10 = v101 * CubicInterpolate(PosFloat.z) + v100 * CubicInterpolate(1.0 - PosFloat.z);
    float v01 = v011 * CubicInterpolate(PosFloat.z) + v010 * CubicInterpolate(1.0 - PosFloat.z);
    float v11 = v111 * CubicInterpolate(PosFloat.z) + v110 * CubicInterpolate(1.0 - PosFloat.z);
    float v0  = v01  * CubicInterpolate(PosFloat.y) + v00  * CubicInterpolate(1.0 - PosFloat.y);
    float v1  = v11  * CubicInterpolate(PosFloat.y) + v10  * CubicInterpolate(1.0 - PosFloat.y);

    return v1 * CubicInterpolate(PosFloat.x) + v0 * CubicInterpolate(1.0 - PosFloat.x);
}
vec3 WavelengthToRgb(float wavelength) {
    vec3 color = vec3(0.0);

    if (wavelength < 380.0 || wavelength > 750.0) {
        return color; 
    }

    if (wavelength >= 380.0 && wavelength < 440.0) {
        color.r = -(wavelength - 440.0) / (440.0 - 380.0);
        color.g = 0.0;
        color.b = 1.0;
    } else if (wavelength >= 440.0 && wavelength < 490.0) {
        color.r = 0.0;
        color.g = (wavelength - 440.0) / (490.0 - 440.0);
        color.b = 1.0;
    } else if (wavelength >= 490.0 && wavelength < 510.0) {
        color.r = 0.0;
        color.g = 1.0;
        color.b = -(wavelength - 510.0) / (510.0 - 490.0);
    } else if (wavelength >= 510.0 && wavelength < 580.0) {
        color.r = (wavelength - 510.0) / (580.0 - 510.0);
        color.g = 1.0;
        color.b = 0.0;
    } else if (wavelength >= 580.0 && wavelength < 645.0) {
        color.r = 1.0;
        color.g = -(wavelength - 645.0) / (645.0 - 580.0);
        color.b = 0.0;
    } else if (wavelength >= 645.0 && wavelength <= 750.0) {
        color.r = 1.0;
        color.g = 0.0;
        color.b = 0.0;
    }

    float factor = 0.0;
    if (wavelength >= 380.0 && wavelength < 420.0) {
        factor = 0.3 + 0.7 * (wavelength - 380.0) / (420.0 - 380.0);
    } else if (wavelength >= 420.0 && wavelength < 645.0) {
        factor = 1.0;
    } else if (wavelength >= 645.0 && wavelength <= 750.0) {
        factor = 0.3 + 0.7 * (750.0 - wavelength) / (750.0 - 645.0);
    }

    return color * factor/pow(color.r*color.r+2.25*color.g*color.g+0.36*color.b*color.b,0.5)*(0.1*(color.r+color.g+color.b)+0.9);
}
float PerlinNoise1D(float Position)
{
    float PosInt   = floor(Position);
    float PosFloat = fract(Position);

    float v0 = 2.0 * fract(sin(PosInt*12.9898) * 43758.5453) - 1.0;
    float v1 = 2.0 * fract(sin((PosInt+1.0)*12.9898) * 43758.5453) - 1.0;


    return v1 * CubicInterpolate(PosFloat) + v0 * CubicInterpolate(1.0 - PosFloat);
}

float SoftSaturate(float x)
{
    return 1.0 - 1.0 / (max(x, 0.0) + 1.0);
}

float GenerateAccretionDiskNoise(vec3 Position, float NoiseStartLevel, float NoiseEndLevel, float ContrastLevel)
{
    float NoiseAccumulator = 10.0;
    float start = NoiseStartLevel;
    float end = NoiseEndLevel;
    int iStart = int(floor(start));
    int iEnd = int(ceil(end));
    
    int maxIterations = iEnd - iStart;
    for (int delta = 0; delta < maxIterations; delta++)
    {
        int i = iStart + delta;
        
        float iFloat = float(i);
        float w = max(0.0, min(end, iFloat + 1.0) - max(start, iFloat));
        if (w <= 0.0) continue;
        
        float NoiseFrequency = pow(3.0, iFloat);
        vec3 ScaledPosition = NoiseFrequency * Position;
        float noise = PerlinNoise(ScaledPosition);
        NoiseAccumulator *= (1.0 + 0.1 * noise * w);
    }
    
    return log(1.0 + pow(0.1 * NoiseAccumulator, ContrastLevel));
}
float Vec2ToTheta(vec2 v1, vec2 v2)
{
    if (dot(v1, v2) > 0.0)
    {
        return asin(0.999999 * (v1.x * v2.y - v1.y * v2.x) / length(v1) / length(v2));
    }
    else if (dot(v1, v2) < 0.0 && (-v1.x * v2.y + v1.y * v2.x) < 0.0)
    {
        return kPi - asin(0.999999 * (v1.x * v2.y - v1.y * v2.x) / length(v1) / length(v2));
    }
    else if (dot(v1, v2) < 0.0 && (-v1.x * v2.y + v1.y * v2.x) > 0.0)
    {
        return -kPi - asin(0.999999 * (v1.x * v2.y - v1.y * v2.x) / length(v1) / length(v2));
    }
}

vec3 KelvinToRgb(float Kelvin)
{
    if (Kelvin < 400.01)
    {
        return vec3(0.0);
    }

    float Teff     = (Kelvin - 6500.0) / (6500.0 * Kelvin * 2.2);
    vec3  RgbColor = vec3(0.0);
    
    RgbColor.r = exp(2.05539304e4 * Teff);
    RgbColor.g = exp(2.63463675e4 * Teff);
    RgbColor.b = exp(3.30145739e4 * Teff);

    float BrightnessScale = 1.0 / max(max(1.5*RgbColor.r, RgbColor.g), RgbColor.b);
    
    if (Kelvin < 1000.0)
    {
        BrightnessScale *= (Kelvin - 400.0) / 600.0;
    }
    
    RgbColor *= BrightnessScale;
    return RgbColor;
}

float GetKeplerianAngularVelocity(float Radius, float Rs)
{
    return sqrt(kSpeedOfLight / kLightYear * kSpeedOfLight * Rs / kLightYear / ((2.0 * Radius - 3.0 * Rs) * Radius * Radius));
}

vec3 WorldToBlackHoleSpace(vec4 Position, vec3 BlackHolePos, vec3 DiskNormal,vec3 WorldUp)
{
    if (DiskNormal == WorldUp)
    {
        DiskNormal += 0.0001 * vec3(1.0, 0.0, 0.0);
    }

    vec3 BlackHoleSpaceY = normalize(DiskNormal);
    vec3 BlackHoleSpaceZ = normalize(cross(WorldUp, BlackHoleSpaceY));
    vec3 BlackHoleSpaceX = normalize(cross(BlackHoleSpaceY, BlackHoleSpaceZ));

    mat4x4 Translate = mat4x4(1.0, 0.0, 0.0, -BlackHolePos.x,
                              0.0, 1.0, 0.0, -BlackHolePos.y,
                              0.0, 0.0, 1.0, -BlackHolePos.z,
                              0.0, 0.0, 0.0, 1.0);

    mat4x4 Rotate = mat4x4(BlackHoleSpaceX.x, BlackHoleSpaceX.y, BlackHoleSpaceX.z, 0.0,
                           BlackHoleSpaceY.x, BlackHoleSpaceY.y, BlackHoleSpaceY.z, 0.0,
                           BlackHoleSpaceZ.x, BlackHoleSpaceZ.y, BlackHoleSpaceZ.z, 0.0,
                           0.0,               0.0,               0.0,               1.0);

    Position = transpose(Translate) * Position;
    Position = transpose(Rotate)    * Position;
    return Position.xyz;
}

vec3 ApplyBlackHoleRotation(vec4 Position, vec3 BlackHolePos, vec3 DiskNormal,vec3 WorldUp)
{
    if (DiskNormal == WorldUp)
    {
        DiskNormal += 0.0001 * vec3(1.0, 0.0, 0.0);
    }

    vec3 BlackHoleSpaceY = normalize(DiskNormal);
    vec3 BlackHoleSpaceZ = normalize(cross(WorldUp, BlackHoleSpaceY));
    vec3 BlackHoleSpaceX = normalize(cross(BlackHoleSpaceY, BlackHoleSpaceZ));

    mat4x4 Rotate = mat4x4(BlackHoleSpaceX.x, BlackHoleSpaceX.y, BlackHoleSpaceX.z, 0.0,
                           BlackHoleSpaceY.x, BlackHoleSpaceY.y, BlackHoleSpaceY.z, 0.0,
                           BlackHoleSpaceZ.x, BlackHoleSpaceZ.y, BlackHoleSpaceZ.z, 0.0,
                           0.0,               0.0,               0.0,               1.0);

    Position = transpose(Rotate) * Position;
    return Position.xyz;
}

vec4 GetCamera(vec4 Position)  // 相机系平移旋转  本部分在实际使用时uniform输入
{
    float Theta = 4.0 * kPi * iMouse.x / iResolution.x;
    float Phi   = 0.999 * kPi * iMouse.y / iResolution.y + 0.0005;
    float R     = 0.00027;

    if (iMouse.x / iResolution.x<=0.01&&iMouse.y / iResolution.y<=0.01)
    {
        Theta = 4.0 * kPi * 0.15;
        Phi   = 0.999 * kPi * 0.5 + 0.0005;
        R = 0.000207;
    }
    if (texelFetch(iChannel0, ivec2(83, 0), 0).x > 0.)
    {
        R =TEST* 0.000807;
    }
    if (texelFetch(iChannel0, ivec2(87, 0), 0).x > 0.)
    {
        R = 0.0000186;
    }
    vec3 Rotcen = vec3(0.0, 0.0, 0.0);

    vec3 Campos;

    vec3 reposcam = vec3(R * sin(Phi) * cos(Theta), -R * cos(Phi), -R * sin(Phi) * sin(Theta));

    Campos    = Rotcen + reposcam;
    vec3 vecy = vec3(0.0, 1.0, 0.0);

    vec3 X = normalize(cross(vecy, reposcam));
    vec3 Y = normalize(cross(reposcam, X));
    vec3 Z = normalize(reposcam);

    Position = (transpose(mat4x4(1., 0., 0., -Campos.x, 0., 1., 0., -Campos.y, 0., 0., 1., -Campos.z, 0., 0., 0., 1.)) * Position);

    Position = transpose(mat4x4(X.x, X.y, X.z, 0., Y.x, Y.y, Y.z, 0., Z.x, Z.y, Z.z, 0., 0., 0., 0., 1.)) * Position;

    return Position;
}

vec4 GetCameraRot(vec4 Position)  // 摄影机系旋转    本部分在实际使用时uniform输入
{
    float Theta = 4.0 * kPi * iMouse.x / iResolution.x;
    float Phi   = 0.999 * kPi * iMouse.y / iResolution.y + 0.0005;
    float R     = 0.000057;

    if(iMouse.x / iResolution.x<=0.01&&iMouse.y / iResolution.y<=0.01)
    {
        Theta = 4.0 * kPi * 0.15;
        Phi   = 0.999 * kPi * 0.5 + 0.0005;
    }

    vec3 Rotcen = vec3(0.0, 0.0, 0.0);

    vec3 Campos;

    vec3 reposcam = vec3(R * sin(Phi) * cos(Theta), -R * cos(Phi), -R * sin(Phi) * sin(Theta));

    Campos    = Rotcen + reposcam;
    vec3 vecy = vec3(0.0, 1.0, 0.0);

    vec3 X = normalize(cross(vecy, reposcam));
    vec3 Y = normalize(cross(reposcam, X));
    vec3 Z = normalize(reposcam);
    
    Position = transpose(mat4x4(X.x, X.y, X.z, 0., Y.x, Y.y, Y.z, 0., Z.x, Z.y, Z.z, 0., 0., 0., 0., 1.)) * Position;
    return Position;
}

vec3 FragUvToDir(vec2 FragUv, float Fov)
{
    return normalize(vec3(Fov * (2.0 * FragUv.x - 1.0), Fov * (2.0 * FragUv.y - 1.0) * iResolution.y / iResolution.x, -1.0));
}

vec2 PosToNdc(vec4 Pos)
{
    return vec2(-Pos.x / Pos.z, -Pos.y / Pos.z * iResolution.x / iResolution.y);
}

vec2 DirToNdc(vec3 Dir)
{
    return vec2(-Dir.x / Dir.z, -Dir.y / Dir.z * iResolution.x / iResolution.y);
}

vec2 DirToFragUv(vec3 Dir)
{
    return vec2(0.5 - 0.5 * Dir.x / Dir.z, 0.5 - 0.5 * Dir.y / Dir.z * iResolution.x / iResolution.y);
}

vec2 PosToFragUv(vec4 Pos)
{
    return vec2(0.5 - 0.5 * Pos.x / Pos.z, 0.5 - 0.5 * Pos.y / Pos.z * iResolution.x / iResolution.y);
}

float Shape(float x, float Alpha, float Beta)
{
    float k = pow(Alpha + Beta, Alpha + Beta) / (pow(Alpha, Alpha) * pow(Beta, Beta));
    return k * pow(x, Alpha) * pow(1.0 - x, Beta);
}

vec4 GetInverseCameraRot(vec4 a)//摄影机系旋转    本部分在实际使用时uniform输入
{
float _Theta=4.0*kPi*iMouse.x/iResolution.x;
float _Phi=0.999*kPi*iMouse.y/iResolution.y+0.0005;
float _R=2.;

    if (iMouse.x / iResolution.x<=0.01&&iMouse.y / iResolution.y<=0.01)
        {
        _Theta = 4.0 * kPi * 0.4;
        _Phi   = 0.999 * kPi * 0.5 + 0.0005;
    }
if(texelFetch(iChannel0, ivec2(83, 0), 0).x > 0.){
_R=5.;


}
if(texelFetch(iChannel0, ivec2(87, 0), 0).x > 0.){
_R=.2;


}
vec3 _Rotcen=vec3(0.0,0.0,0.0);

vec3 _Campos;

    vec3 reposcam=vec3(
    _R * sin(_Phi) * cos(_Theta),
    _R * sin(_Phi) * sin(_Theta),
    -_R * cos(_Phi));

    _Campos = _Rotcen + reposcam;
    vec3 vecz =vec3( 0.0,0.0,1.0 );

    vec3 _X = normalize(cross(vecz, reposcam));
    vec3 _Y = normalize(cross(reposcam, _X));
    vec3 _Z = normalize(reposcam);

    a=inverse(transpose(mat4x4(
        _X.x,_X.y,_X.z,0.,
        _Y.x,_Y.y,_Y.z,0.,
        _Z.x,_Z.y,_Z.z,0.,
        0.   ,0.   ,0.   ,1.)
        ))*a;
    return a;
}

vec4 hash43x(vec3 p)
{
    uvec3 x = uvec3(ivec3(p));
    x = 1103515245U*((x.xyz >> 1U)^(x.yzx));
    uint h = 1103515245U*((x.x^x.z)^(x.y>>3U));
    uvec4 rz = uvec4(h, h*16807U, h*48271U, h*69621U); //see: http://random.mat.sbg.ac.at/results/karl/server/node4.html
    return vec4((rz >> 1) & uvec4(0x7fffffffU))/float(0x7fffffff);
}


vec3 stars(vec3 p)//from  https://www.shadertoy.com/view/fl2Bzd
{
    vec3 col = vec3(0);
    float rad = .087*iResolution.y;
    float dens = 0.15;
    float id = 0.;
    float rz = 0.;
    float z = 1.;
    
    for (float i = 0.; i < 5.; i++)
    {
        p *= mat3(0.86564, -0.28535, 0.41140, 0.50033, 0.46255, -0.73193, 0.01856, 0.83942, 0.54317);
        vec3 q = abs(p);
        vec3 p2 = p/max(q.x, max(q.y,q.z));
        p2 *= rad;
        vec3 ip = floor(p2 + 1e-5);
        vec3 fp = fract(p2 + 1e-5);
        vec4 rand = hash43x(ip*283.1);
        vec3 q2 = abs(p2);
        vec3 pl = 1.0- step(max(q2.x, max(q2.y, q2.z)), q2);
        vec3 pp = fp - ((rand.xyz-0.5)*.6 + 0.5)*pl; //don't displace points away from the cube faces
        float pr = length(ip) - rad;   
        if (rand.w > (dens - dens*pr*0.035)) pp += 1e6;

        float d = dot(pp, pp);
        d /= pow(fract(rand.w*172.1), 32.) + .25;
        float bri = dot(rand.xyz*(1.-pl),vec3(1)); //since one random value is unused to displace, we can reuse
        id = fract(rand.w*101.);
        col += bri*z*.00009/pow(d + 0.025, 3.0)*(mix(vec3(1.0,0.45,0.1),vec3(0.75,0.85,1.), id)*0.6+0.4);
        
        rad = floor(rad*1.08);
        dens *= 1.45;
        z *= 0.6;
        p = p.yxz;
    }
    
    return col;
}

vec4 DiskColor(vec4 BaseColor, float TimeRate, float StepLength, vec3 RayPos, vec3 LastRayPos,
               vec3 RayDir, vec3 LastRayDir, vec3 WorldUp, vec3 BlackHolePos, vec3 DiskNormal,
               float Rs, float InterRadius, float OuterRadius,float Thin,float Hopper , float Brightmut,float Darkmut,float Reddening,float Saturation,float DiskTemperatureArgument,
                float BlackbodyIntensityExponent,float RedShiftColorExponent,float RedShiftIntensityExponent,
               float PeakTemperature, float ShiftMax)
{
    vec3 CameraPos = WorldToBlackHoleSpace(vec4(0.0, 0.0, 0.0, 1.0), BlackHolePos, DiskNormal, WorldUp);
    vec3 PosOnDisk = WorldToBlackHoleSpace(vec4(RayPos, 1.0),        BlackHolePos, DiskNormal, WorldUp);
    vec3 LastPosOnDisk = WorldToBlackHoleSpace(vec4(LastRayPos, 1.0),        BlackHolePos, DiskNormal, WorldUp);
    vec3 DirOnDisk = ApplyBlackHoleRotation(vec4(RayDir, 1.0),       BlackHolePos, DiskNormal, WorldUp);

    float PosR = length(PosOnDisk);
    float PosY = PosOnDisk.y;
    float LPosY = LastPosOnDisk.y;
    if( LPosY*PosY<0.0)//当光线穿过盘，让它停在盘上，以适应极薄的盘
    {
    
    vec3 CPoint=(-PosOnDisk*LPosY+LastPosOnDisk*PosY)/(PosY-LPosY);
    PosOnDisk=CPoint+min(Thin,length(CPoint-LastPosOnDisk))*DirOnDisk*(-1.0+2.0*RandomStep(10000.0*(PosOnDisk.zx/OuterRadius), fract(iTime * 1.0 + 0.5)));
    StepLength=length(PosOnDisk-LastPosOnDisk);
    PosR = length(PosOnDisk);
    PosY = PosOnDisk.y;
    }
    
    //Thin与DenAndThiFactor相乘构成轮廓
    Thin+=max(0.0,(length(PosOnDisk.xz)-3.0*Rs)*Hopper);//*(0.7*OuterRadius-PosR)/0.7/OuterRadius);
    //Thin*=0.5+0.5*exp(-30.0*pow(PosR/OuterRadius,4.0));
    
    float frac=max(0.0,2.0-0.6*Thin/Rs);
    vec4 Color = vec4(0.0);
    vec4 Result=vec4(0.0);
    if (abs(PosY) < Thin && PosR < OuterRadius && PosR > InterRadius)
    {
        
        float x=(PosR-InterRadius)/(OuterRadius-InterRadius);
        float a=max(1.0,(OuterRadius-InterRadius)/(10.0*Rs));
        float EffectiveRadius=(-1.+sqrt(1.+4.*a*a*x-4.*x*a))/(2.*a-2.);
        float InterCloudEffectiveRadius=(PosR-InterRadius)/min(OuterRadius-InterRadius,12.0*Rs);
        if(a==1.0){EffectiveRadius=x;}
        
        float DenAndThiFactor=Shape(EffectiveRadius, 0.9, 1.5);
        if ((abs(PosY) < Thin * DenAndThiFactor) || (PosY < Thin * (1.0 - 5.0 * pow(InterCloudEffectiveRadius, 2.0))))//第一个条件是盘轮廓，第二个是薄盘内层团块云的轮廓
        {
            float AngularVelocity  = GetKeplerianAngularVelocity(PosR, Rs);
            float HalfPiTimeInside = kPi / GetKeplerianAngularVelocity(3.0 * Rs, Rs);

            float SpiralTheta=12.0*2.0/sqrt(3.0)*(atan(sqrt(0.6666666*(PosR/Rs)-1.0)));//盘以恒定径向速度向内收缩，同时走过轨迹使得切向速度是此处圆轨道速度
            float InnerTheta= kPi / HalfPiTimeInside *iTime * TimeRate ;
            float PosThetaForInnerCloud = Vec2ToTheta(PosOnDisk.zx, vec2(cos(0.666666*InnerTheta),sin(0.666666*InnerTheta)));
            float PosTheta            = Vec2ToTheta(PosOnDisk.zx, vec2(cos(-SpiralTheta), sin(-SpiralTheta)));
            float PosLogarithmicTheta            = Vec2ToTheta(PosOnDisk.zx, vec2(cos(-2.0*log(PosR/Rs)), sin(-2.0*log(PosR/Rs))));


            // 计算盘温度
            float DiskTemperature = pow(DiskTemperatureArgument * pow(Rs/PosR,3.0) * max(1.0 - sqrt(InterRadius / PosR), 0.000001), 0.25);
            // 计算云相对速度
            vec3  CloudVelocity    = kLightYear / kSpeedOfLight * AngularVelocity * cross(vec3(0., 1., 0.), PosOnDisk);
            float RelativeVelocity = dot(-DirOnDisk, CloudVelocity);
            // 计算多普勒因子
            float Dopler = sqrt((1.0 + RelativeVelocity) / (1.0 - RelativeVelocity));
            // 总红移量，含多普勒因子和引力红移和
            float RedShift = Dopler * sqrt(max(1.0 - Rs / PosR, 0.000001)) / sqrt(max(1.0 - Rs / length(CameraPos), 0.000001));


            float RotPosR=PosR/Rs+0.3*sqrt(3.0)*kSpeedOfLight/kLightYear /3.0/sqrt(3.0)/Rs*TimeRate*iTime;
            
            float Density           =DenAndThiFactor;
            
            
            //厚度，后面的项在薄盘时制造起伏，但不会大于1
            float Thick             = Thin * Density* (0.4+0.6*clamp(Thin/Rs-0.5,0.0,2.5)/2.5 + (1.0-(0.4+0.6*clamp(Thin/Rs-0.5,0.0,2.5)/2.5))* SoftSaturate(GenerateAccretionDiskNoise(vec3(1.5 * PosTheta,RotPosR, 1.0), -0.7+frac, 1.3+frac, 80.0))); // 盘厚
            float ThickM = Thin * Density;
            float VerticalMixFactor = 0.0;
            float DustColor         = 0.0;
            
            vec4  Color0            = vec4(0.0);
            
            if (abs(PosY) <Thin*  Density)
            {
                float Levelmut=0.91*log(1.0+(0.06/0.91*max(0.0,min(1000.0,PosR/Rs)-10.0)));//越靠外，噪声空间频率越低
                float Conmut=   80.0*log(1.0+(0.1*0.06*max(0.0,min(1000000.0,PosR/Rs)-10.0)));
                //云以及一圈时平滑过渡
                Color0      =                                vec4(GenerateAccretionDiskNoise(vec3(0.1 * (RotPosR-0.0*OuterRadius/Rs*PosTheta), 0.1 * PosY / Rs, 0.02*pow(OuterRadius/Rs,0.7) * PosTheta), frac+2.0-Levelmut, frac+4.0-Levelmut, 80.0-Conmut)); // 云本体
                if(PosTheta+kPi<0.1*kPi)
                {
                    Color0*=(PosTheta+kPi)/(0.1*kPi);
                    Color0+=(1.0-((PosTheta+kPi)/(0.1*kPi)))*vec4(GenerateAccretionDiskNoise(vec3(0.1 *(RotPosR-0.0*OuterRadius/Rs*(PosTheta+2.0*kPi)), 0.1 * PosY / Rs, 0.02*pow(OuterRadius/Rs,0.7) * (PosTheta+2.0*kPi)), frac+2.0-Levelmut, frac+4.0-Levelmut, 80.0-Conmut));
                }
                //当前云的实现在大半径变成同心圆，所以在远处人为加上螺旋条纹
                if(PosR>max(0.15379*OuterRadius,0.15379*64.0*Rs))
                {
                    float Spir      = (GenerateAccretionDiskNoise(vec3(0.1 * (PosR-0.1*sqrt(3.0)*kSpeedOfLight/kLightYear /3.0/sqrt(3.0)/Rs*TimeRate*iTime-0.08*OuterRadius/Rs*PosLogarithmicTheta), 0.1 * PosY / Rs, 0.02*pow(OuterRadius/Rs,0.7) * PosLogarithmicTheta), frac+2.0-Levelmut, frac+3.0-Levelmut, 80.0-Conmut)); // 云本体
                    if(PosLogarithmicTheta+kPi<0.1*kPi)
                    {
                        Spir*=(PosLogarithmicTheta+kPi)/(0.1*kPi);
                        Spir+=(1.0-((PosLogarithmicTheta+kPi)/(0.1*kPi)))*(GenerateAccretionDiskNoise(vec3(0.1 *(PosR-0.1*sqrt(3.0)*kSpeedOfLight/kLightYear /3.0/sqrt(3.0)/Rs*TimeRate*iTime-0.08*OuterRadius/Rs*(PosLogarithmicTheta+2.0*kPi)), 0.1 * PosY / Rs, 0.02*pow(OuterRadius/Rs,0.7) * (PosLogarithmicTheta+2.0*kPi)), frac+2.0-Levelmut, frac+3.0-Levelmut, 80.0-Conmut));
                    }
                    Color0*=(mix(1.0,clamp(0.7*Spir*1.5-0.5,0.0,3.0),0.5+0.5*max(-1.0,1.0-exp(-1.5*0.1*(100.0*PosR/max(OuterRadius,64.0*Rs)-20.0)))));
                }

                VerticalMixFactor = max(0.0, (1.0 - abs(PosY) / Thick));//密度离盘越远越低
                Density    *= 0.7 * VerticalMixFactor * Density;
                
                
                Color0.xyz *= Density * 1.4;
                Color0.a   *= (Density)*(Density)/0.3;
                Color0.xyz *=max(0.0, (0.2+2.0*sqrt(pow(PosY / Thick,2.0)+0.001)));//在中心加不透明度
            }
            if (abs(PosY) < Thin * (1.0 - 5.0 * pow( InterCloudEffectiveRadius, 2.0)))//薄盘内层团块云
            {
              DustColor = max(1.0 - pow(PosY / (Thin * max(1.0 - 5.0 * pow(InterCloudEffectiveRadius, 2.0), 0.0001)), 2.0), 0.0) * GenerateAccretionDiskNoise(vec3(1.5 * fract((1.5 *  PosThetaForInnerCloud + kPi / HalfPiTimeInside *iTime*TimeRate) / 2.0 / kPi) * 2.0 * kPi, PosR / Rs, PosY / Rs), 0., 6., 80.0);
              Color0 += 0.02 * vec4(vec3(DustColor), 0.2 * DustColor) * sqrt(1.0001 - DirOnDisk.y * DirOnDisk.y) * min(1.0, Dopler * Dopler);
            }
           
           
           
            Color =  Color0;
//                                        超大盘增亮外侧                                           正常亮度衰减

            
            float BrightWithoutRedshift = 0.05*min(OuterRadius/(1000.0*Rs),1000.0*Rs/OuterRadius)+0.55 / exp(5.0*EffectiveRadius)*mix(0.2+0.8*abs(RayDir.y),1.0,clamp(Thick/(1.0*Rs)-0.8,0.2,1.0)); // 原亮度
            BrightWithoutRedshift *= pow(DiskTemperature/PeakTemperature, BlackbodyIntensityExponent); // 计算物理温度对亮度的影响。BlackbodyIntensityExponent的默认值为0.0，物理值为4.0。默认值较低是为了避免低温部分太暗。
            
            float VisionTemperature = DiskTemperature * pow(RedShift, RedShiftColorExponent); // 计算视觉温度。RedShiftColorExponent的默认值为3.0，物理值为1.0。默认值较高是为了增强红蓝对比。
            
            Color.xyz *= BrightWithoutRedshift * KelvinToRgb(VisionTemperature); 
            Color.xyz *= min(pow(RedShift, RedShiftIntensityExponent),ShiftMax); // 计算红蓝移对亮度的影响。RedShiftIntensityExponent的默认值为4.0，物理值为4.0。

            Color.xyz *= min(1.0, 1.8 * (OuterRadius - PosR) / (OuterRadius - InterRadius)); // 一个非物理的亮度更改，让外围更暗。1.0 + 0.5 * ((PosR - InterRadius) / InterRadius + InterRadius / (PosR - InterRadius)) - max(1.0, RedShift));
            Color.xyz*=0.757125*1.2;
            Color.a*=0.5;
            
            //解耦总光深与外径
            Color*=max(mix(vec4(5.0*Rs/(max(Thin,0.2*Rs)+(0.0+Hopper*0.5)*OuterRadius)),vec4(vec3(0.3+0.7*5.0*Rs/(Thin+(0.0+Hopper*0.5)*OuterRadius)),1.0),0.0*exp(-pow(20.0*PosR/OuterRadius,2.0)))
                  , mix(vec4(100.0*Rs/OuterRadius),vec4(vec3(0.3+0.7*100.0*Rs/OuterRadius),1.0),exp(-pow(20.0*PosR/OuterRadius,2.0))));

            Color.xyz*=mix(1.0,max(1.0,abs(DirOnDisk.y)/0.2),clamp(0.3-0.6*(ThickM/Rs-1.0),0.0,0.3));
            //
            //小半径（约100Rs）下提高中心能见度
            //float SmallRCenterSparse=min(1.0,max(0.0,(OuterRadius/Rs/100.0)-1.0));
            //Color.a*=  0.5+0.5*SmallRCenterSparse+(0.5-0.5*SmallRCenterSparse)*clamp((PosR/Rs-18.0)*0.3,0.0,1.0)  ;
            //Color.xyz*=0.64+0.36*SmallRCenterSparse+(0.36-0.36*SmallRCenterSparse)*clamp((PosR/Rs-18.0)*0.3,0.0,1.0);
            
            Color *= StepLength / Rs;
        }
    }
    else
    {
        return BaseColor;
    }
    
    Color.xyz*=Brightmut;
    Color.a*=Darkmut;
    //星际红化和饱和度控制

    float aR = 1.0+ Reddening*(1.0-1.0);
    float aG = 1.0+ Reddening*(3.0-1.0);
    float aB = 1.0+ Reddening*(6.0-1.0);
    float Sum_rgb = (Color.r + Color.g + Color.b)*pow(1.0 - BaseColor.a, aG);
    Sum_rgb *= 1.0;
    
    float r001 = 0.0;
    float g001 = 0.0;
    float b001 = 0.0;
        
    float Denominator = Color.r*pow(1.0 - BaseColor.a, aR) + Color.g*pow(1.0 - BaseColor.a, aG) + Color.b*pow(1.0 - BaseColor.a, aB);
    if (Denominator > 0.000001)
    {
        r001 = Sum_rgb * Color.r * pow(1.0 - BaseColor.a, aR) / Denominator;
        g001 = Sum_rgb * Color.g * pow(1.0 - BaseColor.a, aG) / Denominator;
        b001 = Sum_rgb * Color.b * pow(1.0 - BaseColor.a, aB) / Denominator;
        
       r001 *= pow(3.0*r001/(r001+g001+b001),Saturation);
       g001 *= pow(3.0*g001/(r001+g001+b001),Saturation);
       b001 *= pow(3.0*b001/(r001+g001+b001),Saturation);
        
    }

    Result.r=BaseColor.r + r001;
    Result.g=BaseColor.g + g001;
    Result.b=BaseColor.b + b001;
    Result.a=BaseColor.a + Color.a * pow((1.0 - BaseColor.a),1.0);

    
    return Result;
}
vec4 JetColor(vec4 BaseColor, float TimeRate, float StepLength, vec3 RayPos, vec3 LastRayPos,
               vec3 RayDir, vec3 LastRayDir, vec3 WorldUp, vec3 BlackHolePos, vec3 DiskNormal,
               float Rs, float InterRadius, float OuterRadius,float JetRedShiftIntensityExponent,float Thin, float Hopper ,float DiskTemperatureArgument,float Bright,
               float PeakTemperature, float ShiftMax)
{
    vec3 CameraPos = WorldToBlackHoleSpace(vec4(0.0, 0.0, 0.0, 1.0), BlackHolePos, DiskNormal, WorldUp);
    vec3 PosOnDisk = WorldToBlackHoleSpace(vec4(RayPos, 1.0),        BlackHolePos, DiskNormal, WorldUp);
    vec3 DirOnDisk = ApplyBlackHoleRotation(vec4(RayDir, 1.0),       BlackHolePos, DiskNormal, WorldUp);

    float PosR = length(PosOnDisk);
    float PosY = PosOnDisk.y;
    vec4  Color            = vec4(0.0);
            
    bool NotInJet=true;        
    vec4 Result=vec4(0.0);
    if(length(PosOnDisk.xz)*length(PosOnDisk.xz)<2.0*InterRadius*InterRadius+0.03*0.03*PosY*PosY&&PosR<sqrt(2.0)*OuterRadius){
            vec4 Color0;
            NotInJet=false;

            float InnerTheta= 3.0*GetKeplerianAngularVelocity(InterRadius, Rs) *(iTime*TimeRate-kLightYear/0.8/kSpeedOfLight*abs(PosY));

            float Shape=1.0/sqrt(InterRadius*InterRadius+0.02*0.02*PosY*PosY);
            float a=mix(0.7+0.3*PerlinNoise1D(0.3*(iTime*TimeRate-kLightYear/0.8/kSpeedOfLight*abs(abs(PosY)+100.0*(dot(PosOnDisk.xz,PosOnDisk.xz)/PosR)))/(OuterRadius/100.0)/(kLightYear/0.8/kSpeedOfLight)),1.0,exp(-0.01*0.01*PosY*PosY/Rs/Rs));
            Color0=vec4(1.0,1.0,1.0,0.5)*max(0.0,1.0-5.0*Rs*Shape*
                                         abs(1.0-pow(length(PosOnDisk.xz
                                                          //+0.3*(1.1-exp(-0.01*0.01*PosY*PosY/Rs/Rs))*Rs*
                                                          //(PerlinNoise1D   (0.5*(iTime*TimeRate-kLightYear/0.8/kSpeedOfLight*abs(PosY))/Rs/(kLightYear/0.8/kSpeedOfLight)   )    -0.5)
                                                          //*vec2(cos(0.666666*InnerTheta),sin(0.666666*InnerTheta))
                                                          )*Shape,2.0)))*Rs*Shape;
            Color0*=a;
            Color0*=max(0.0,1.0-1.0*exp(-0.0001*PosY/InterRadius*PosY/InterRadius));
            Color0*=exp(-4.0/(2.0)*PosR/OuterRadius*PosR/OuterRadius);
            Color0*=0.5;
            Color+=Color0;
        
    }
    float Wid=Rs*40.0*(sqrt(2.0*abs(PosY)/40.0/Rs+1.0)-1.0);
    Wid=abs(PosY);
    if(length(PosOnDisk.xz)<1.3*InterRadius+0.25*Wid&&length(PosOnDisk.xz)>0.7*InterRadius+0.15*Wid&&PosR<30.0*InterRadius){
            vec4 Color1;
            NotInJet=false;

            float InnerTheta= 2.0*GetKeplerianAngularVelocity(InterRadius, Rs) *(iTime*TimeRate-kLightYear/0.8/kSpeedOfLight*abs(PosY));

            float Shape=1.0/(InterRadius+0.2*Wid);
            
            Color1=vec4(1.0,1.0,1.0,0.5)*max(0.0,1.0-2.0*
                                         abs(1.0-pow(length(PosOnDisk.xz
                                                          +0.2*(1.1-exp(-0.1*0.1*PosY*PosY/Rs/Rs))*Rs*
                                                          (PerlinNoise1D   (0.35*(iTime*TimeRate-kLightYear/0.8/kSpeedOfLight*abs(PosY))/Rs/(kLightYear/0.8/kSpeedOfLight)   )    -0.5)
                                                          *vec2(cos(0.666666*InnerTheta),-sin(0.666666*InnerTheta))
                                                          )*Shape,2.0
                                                     )
                                             )
                                             )*Rs*Shape;
            Color1*=1.0-exp(-PosY/InterRadius*PosY/InterRadius);
            Color1*=exp(-0.005*PosY/InterRadius*PosY/InterRadius);
            Color1*=0.5;
            Color+=Color1;
        
    }
    if(!NotInJet)
    {
            float JetTemperature = 100000.0;

            
            float BrightWithoutRedshift = 1.0;  // 原亮度

            Color.xyz *= BrightWithoutRedshift * 
                         KelvinToRgb(JetTemperature );

            float RelativeVelocity =-(DirOnDisk.y)*sqrt(1.0/(PosR/Rs))*sign(PosY);
            float Dopler = sqrt((1.0 + RelativeVelocity) / (1.0 - RelativeVelocity));
            float RedShift = Dopler * sqrt(max(1.0 - Rs / PosR, 0.000001)) / sqrt(max(1.0 - Rs / length(CameraPos), 0.000001));
            Color.xyz*=min(pow(RedShift,JetRedShiftIntensityExponent),3.0);
            Color.a*=0.;
            Color *= StepLength / Rs*Bright;
    }
    if(NotInJet)
    {
        return BaseColor;
    }
        //星际红化和饱和度控制
    float Reddening=0.3;
    float Saturation=0.;
    float aR = 1.0+ Reddening*(1.0-1.0);
    float aG = 1.0+ Reddening*(2.5-1.0);
    float aB = 1.0+ Reddening*(4.5-1.0);
    float Sum_rgb = (Color.r + Color.g + Color.b)*pow(1.0 - BaseColor.a, aG);
    Sum_rgb *= 1.0;
    
    float r001 = 0.0;
    float g001 = 0.0;
    float b001 = 0.0;
        
    float Denominator = Color.r*pow(1.0 - BaseColor.a, aR) + Color.g*pow(1.0 - BaseColor.a, aG) + Color.b*pow(1.0 - BaseColor.a, aB);
    if (Denominator > 0.000001)
    {
        r001 = Sum_rgb * Color.r * pow(1.0 - BaseColor.a, aR) / Denominator;
        g001 = Sum_rgb * Color.g * pow(1.0 - BaseColor.a, aG) / Denominator;
        b001 = Sum_rgb * Color.b * pow(1.0 - BaseColor.a, aB) / Denominator;
        
       r001 *= pow(3.0*r001/(r001+g001+b001),Saturation);
       g001 *= pow(3.0*g001/(r001+g001+b001),Saturation);
       b001 *= pow(3.0*b001/(r001+g001+b001),Saturation);
        
    }

    Result.r=BaseColor.r + r001;
    Result.g=BaseColor.g + g001;
    Result.b=BaseColor.b + b001;
    Result.a=BaseColor.a + Color.a * pow((1.0 - BaseColor.a),1.0);

    
    return Result;
}
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    fragColor      = vec4(0., 0., 0., 0.);
    vec2  FragUv   = fragCoord / iResolution.xy;
    float Fov      =0.5;
    float TimeRate = TEST*30000.;  // 本部分在实际使用时又uniform输入，此外所有iTime*TimeRate应替换为游戏内时间。

    float MBlackHole = 1.49e7;                                                                          // 单位是太阳质量 本部分在实际使用时uniform输入
    float a0         = 0.0;                                                                             // 无量纲自旋系数 本部分在实际使用时uniform输入
    float Rs         = 2. * MBlackHole * kGravityConstant / kSpeedOfLight / kSpeedOfLight * kSolarMass;  // 单位是米 本部分在实际使用时uniform输入

    float z1       = 1. + pow(1. - a0 * a0, 0.333333333333333) * (pow(1. + a0 * a0, 0.333333333333333) + pow(1. - a0, 0.333333333333333));  // 辅助变量      本部分在实际使用时uniform输入
    float RmsRatio = (3. + sqrt(3. * a0 * a0 + z1 * z1) - sqrt((3. - z1) * (3. + z1 + 2. * sqrt(3. * a0 * a0 + z1 * z1)))) / 2.;            // 赤道顺行最内稳定圆轨与Rs之比    本部分在实际使用时uniform输入
    float AccEff   = sqrt(1. - 1. / RmsRatio);                                                                                              // 吸积放能效率,以落到Rms为准 本部分在实际使用时uniform输入

    float mu      = 1.;                                                                            // 吸积物的比荷的倒数,氕为1 本部分在实际使用时uniform输入
    float dmdtEdd = 6.327 * mu / kSpeedOfLight / kSpeedOfLight * MBlackHole * kSolarMass / AccEff;  // 爱丁顿吸积率 本部分在实际使用时uniform输入

    float dmdt = (2.0*pow(10.0,-6.0+3.5+3.5*cos(0.7*iTime))) * dmdtEdd;  // 吸积率 本部分在实际使用时uniform输入

    float diskA = 3. * kGravityConstant * kSolarMass / Rs / Rs / Rs * MBlackHole * dmdt / (8. * kPi * kSigma);  // 吸积盘温度系数 本部分在实际使用时uniform输入

                                                                                      

    Rs         = Rs / kLightYear;       // 单位是ly 本部分在实际使用时uniform输入
    
    
    
    float BlackbodyIntensityExponent=0.5;
    float RedShiftColorExponent=3.0;
    float RedShiftIntensityExponent=4.0;
    float Brightmut=1.0;//相对亮度
    float Darkmut  =0.25;//相对不透明度
    float Reddening=0.3;//红化
    float Saturation=0.5;//饱和
    //上边参数都是盘的
    float JetRedShiftIntensityExponent=2.0;
    
    
    float InterRadius  = 0.7 * RmsRatio * Rs;  // 盘内半径,正常情况下等于最内稳定圆轨
    float OuterRadius = TEST*(57.0+45.0*cos(0.5*iTime))* Rs;             // 盘外半径 本部分在实际使用时uniform输入
    float Thin = (2.25+1.75*cos(0.21*iTime))*Rs;             // 盘半厚 本部分在实际使用时uniform输入
    float Hopper=0.5*(0.75-0.75*cos(0.6*iTime));
    float shiftMax = 1.25;  // 设定一个蓝移的亮度增加上限,以免亮部过于亮
    
        // 计算峰值温度的四次方,用于自适应亮度。峰值温度出现在49InterRadius/36处
    float PeakTemperature = pow(diskA * 0.05665278*pow(Rs/InterRadius,3.0),0.25);  //    本部分在实际使用时uniform输入
    

    vec3 WorldUp            = GetCameraRot(vec4(0., 1., 0., 0.)).xyz;
    vec4 BlackHoleAPos     = vec4(.0*Rs, .0*Rs, 5. * Rs, 1.0);             // 黑洞世界位置 本部分在实际使用时没有
    vec4 BlackHoleADiskNormal = vec4(normalize(vec3(0.4,1.0,-0.4)), 0.0);  // 吸积盘世界法向 本部分在实际使用时没有
    // 以下在相机系
    vec3  BlackHoleRPos     = GetCamera(BlackHoleAPos).xyz;         //                                                                                     本部分在实际使用时uniform输入
    vec3  BlackHoleRDiskNormal = GetCameraRot(BlackHoleADiskNormal).xyz;  //                                                                          本部分在实际使用时uniform输入
    vec3  RayDir            = FragUvToDir(FragUv + 0.5 * vec2(RandomStep(FragUv, fract(iTime * 1.0 + 0.5)), RandomStep(FragUv, fract(iTime * 1.0))) / iResolution.xy, Fov);
    vec3  RayPos            = vec3(0.0, 0.0, 0.0);
    
    vec3  PosToBlackHole           = RayPos - BlackHoleRPos;
    float DistanceToBlackHole = length(PosToBlackHole);
    vec3  NormalizedPosToBlackHole      = PosToBlackHole / DistanceToBlackHole;
    float BackgroundShiftMax       = 2.0;
    float BackgroundBlueShift= min(1.0 / sqrt(1.0 - Rs /max( length(DistanceToBlackHole),1.001*Rs)+0.005),BackgroundShiftMax);
    RayDir=normalize(RayDir-NormalizedPosToBlackHole*dot(NormalizedPosToBlackHole,RayDir)*(-sqrt(max(1.0-Rs*CubicInterpolate(max(min(1.0-(0.01*DistanceToBlackHole/Rs-1.0)/4.0,1.0),0.0))/DistanceToBlackHole,0.00000000000000001))+1.0));
    vec3  LastRayPos;
    vec3  LastRayDir;
    float StepLength = 0.;
    float LastR      = length(PosToBlackHole);
    float CosTheta;
    float DeltaPhi;
    float DeltaPhiRate;
    float RayStep;
    bool  flag  = true;
    int   Count = 0;
    while (flag == true)
    {  // 测地raymarching

        PosToBlackHole           = RayPos - BlackHoleRPos;
        DistanceToBlackHole      = length(PosToBlackHole);
        NormalizedPosToBlackHole = PosToBlackHole / DistanceToBlackHole;

        if (DistanceToBlackHole > (2.5 * OuterRadius) && DistanceToBlackHole > LastR && Count > 50)
        {  // 远离黑洞
            flag   = false;
            FragUv = DirToFragUv(RayDir);
            //fragColor.r+=0.5*texelFetch(iChannel1, ivec2(vec2(fract(FragUv.x),fract(FragUv.y))*iChannelResolution[1].xy), 0 ).r*pow((1.0 - fragColor.a),1.0+0.3*(1.0-1.0));
            //fragColor.g+=0.5*texelFetch(iChannel1, ivec2(vec2(fract(FragUv.x),fract(FragUv.y))*iChannelResolution[1].xy), 0 ).g*pow((1.0 - fragColor.a),1.0+0.3*(3.0-1.0));
            //fragColor.b+=0.5*texelFetch(iChannel1, ivec2(vec2(fract(FragUv.x),fract(FragUv.y))*iChannelResolution[1].xy), 0 ).b*pow((1.0 - fragColor.a),1.0+0.3*(6.0-1.0));
            //fragColor.a+=0.5*texelFetch(iChannel1, ivec2(vec2(fract(FragUv.x),fract(FragUv.y))*iChannelResolution[1].xy), 0 ).a*pow((1.0 - fragColor.a),1.0);

            //if(fragCoord.x / iResolution.x>0.5)
            {
                vec4 bk=vec4(stars(GetInverseCameraRot(vec4(RayDir,1.0)).xyz),0.0);

                vec4 TexColor=bk;
                if( length(BlackHoleRPos)<200.0*Rs){
                    vec3 Rcolor=bk.r*1.0*WavelengthToRgb(max(453.0,645.0/BackgroundBlueShift));
                    vec3 Gcolor=bk.g*1.5*WavelengthToRgb(max(416.0,510.0/BackgroundBlueShift));
                    vec3 Bcolor=bk.b*0.6*WavelengthToRgb(max(380.0,440.0/BackgroundBlueShift));
                    vec3 Scolor=Rcolor+Gcolor+Bcolor;
                    float OStrength=0.3*bk.r+0.6*bk.g+0.1*bk.b;
                    float RStrength=0.3*Scolor.r+0.6*Scolor.g+0.1*Scolor.b;
                    Scolor*=OStrength/max(RStrength,0.001);
                    TexColor = vec4(Scolor,bk.a)*BackgroundBlueShift*BackgroundBlueShift*BackgroundBlueShift*BackgroundBlueShift;
                }
            fragColor.r+=0.5*0.4*TexColor.r*pow((1.0 - fragColor.a),1.0+0.3*(1.0-1.0));
            fragColor.g+=0.5*0.4*TexColor.g*pow((1.0 - fragColor.a),1.0+0.3*(3.0-1.0));
            fragColor.b+=0.5*0.4*TexColor.b*pow((1.0 - fragColor.a),1.0+0.3*(6.0-1.0));
            fragColor.a+=0.5*0.4*TexColor.a*pow((1.0 - fragColor.a),1.0);
            }//else{
            //fragColor.r+=0.5*0.4*vec4(255.0/255.0,241.0/255.0,205.0/255.0,1.0).r*pow((1.0 - fragColor.a),1.0+0.3*(1.0-1.0));
            //fragColor.g+=0.5*0.4*vec4(255.0/255.0,241.0/255.0,205.0/255.0,1.0).g*pow((1.0 - fragColor.a),1.0+0.3*(3.0-1.0));
            //fragColor.b+=0.5*0.4*vec4(255.0/255.0,241.0/255.0,205.0/255.0,1.0).b*pow((1.0 - fragColor.a),1.0+0.3*(6.0-1.0));
            //fragColor.a+=0.5*0.4*vec4(255.0/255.0,241.0/255.0,205.0/255.0,1.0).a*pow((1.0 - fragColor.a),1.0);
            //}
        }
        if (DistanceToBlackHole < 0.1 * Rs)
        {
            flag = false;
        }
        if (flag == true)
        {
            fragColor = DiskColor(fragColor, TimeRate, StepLength, RayPos, LastRayPos, RayDir, LastRayDir, WorldUp, BlackHoleRPos, BlackHoleRDiskNormal, Rs, InterRadius, OuterRadius,Thin,Hopper, Brightmut,Darkmut,Reddening,Saturation,diskA,  BlackbodyIntensityExponent,RedShiftColorExponent,RedShiftIntensityExponent,PeakTemperature, shiftMax);  // 吸积盘颜色
            fragColor = JetColor(fragColor, TimeRate, StepLength, RayPos, LastRayPos, RayDir, LastRayDir, WorldUp, BlackHoleRPos, BlackHoleRDiskNormal, Rs, InterRadius, OuterRadius,JetRedShiftIntensityExponent,Thin,Hopper, diskA, (0.5+0.5*tanh(log(dmdt/dmdtEdd)+1.0)),  PeakTemperature, shiftMax);  // 喷流颜色   
        }

        if (fragColor.a > 0.99)
        {
            flag = false;
        }
        LastRayPos   = RayPos;
        LastRayDir   = RayDir;
        LastR        = DistanceToBlackHole;
        CosTheta     = length(cross(NormalizedPosToBlackHole, RayDir));                           // 前进方向与切向夹角
        DeltaPhiRate = -1.0 * CosTheta * CosTheta * CosTheta * (1.5 * Rs / DistanceToBlackHole);  // 单位长度光偏折角
        if (Count == 0)
        {
            RayStep = RandomStep(FragUv, fract(iTime * 1.0));  // 光起步步长抖动
        }
        else
        {
            RayStep = 1.0;
        }

        RayStep *= 0.15 + 0.25 * min(max(0.0, 0.5 * (0.5 * DistanceToBlackHole / max(10.0 * Rs, OuterRadius) - 1.0)), 1.0);

        if ((DistanceToBlackHole) >= 2.0 * OuterRadius)
        {
            RayStep *= DistanceToBlackHole;
        }
        else if ((DistanceToBlackHole) >= 1.0 * OuterRadius)
        {
            RayStep *= ((Rs+0.25*max(DistanceToBlackHole-12.0*Rs,0.0)) * (2.0 * OuterRadius - DistanceToBlackHole) +
                        DistanceToBlackHole * (DistanceToBlackHole - OuterRadius)) / OuterRadius;
        }
        else
        {
            RayStep *= min(Rs+0.25*max(DistanceToBlackHole-12.0*Rs,0.0),DistanceToBlackHole);
        }

        
        DeltaPhi = RayStep / DistanceToBlackHole * DeltaPhiRate;
        RayDir     = normalize(RayDir + (DeltaPhi + DeltaPhi * DeltaPhi * DeltaPhi / 3.0) *
                     cross(cross(RayDir, NormalizedPosToBlackHole), RayDir) / CosTheta);  // 更新方向，里面的（dthe +DeltaPhi^3/3）是tan（dthe）
        RayPos += RayDir * RayStep;
        StepLength = RayStep;
        //if(DistanceToBlackHole>2.0 * OuterRadius || DistanceToBlackHole<2.0 * Rs){
        Count++;
        //}
    }
    // 为了套bloom先逆处理一遍
    float colorRFactor = 3.0*fragColor.r / (fragColor.r+fragColor.g+fragColor.b);
    float colorBFactor = 3.0*fragColor.b / (fragColor.r+fragColor.g+fragColor.b);
    float colorGFactor = 3.0*fragColor.g / (fragColor.r+fragColor.g+fragColor.b);

    float bloomMax = 12.0;
    fragColor.r    = min(-4.0 * log(1. - pow(fragColor.r, 2.2)), bloomMax * colorRFactor);
    fragColor.g    = min(-4.0 * log(1. - pow(fragColor.g, 2.2)), bloomMax * colorGFactor);
    fragColor.b    = min(-4.0 * log(1. - pow(fragColor.b, 2.2)), bloomMax * colorBFactor);
    fragColor.a    = min(-4.0 * log(1. - pow(fragColor.a, 2.2)), 4.0);

    // TAA

    float blendWeight = 1.0 - pow(0.5, (iTimeDelta) / max(min((0.131 * 36.0 / (TimeRate) * (GetKeplerianAngularVelocity(3. * 0.00000465, 0.00000465)) / (GetKeplerianAngularVelocity(3. * Rs, Rs))), 0.3),
                                                          0.02));  // 本部分在实际使用时max(min((0.131*36.0/(TimeRate)*(omega(3.*0.00000465,0.00000465))/(omega(3.*Rs,Rs))),0.3),0.02)由uniform输入
    blendWeight       = (iFrame < 2 || iMouse.z > 0.0) ? 1.0 : blendWeight;

    vec4 previousColor = texelFetch(iChannel3, ivec2(fragCoord), 0);                     // 获取前一帧的颜色
    fragColor          = (blendWeight)*fragColor + (1.0 - blendWeight) * previousColor;  // 混合当前帧和前一帧

    // FragUv=DirToFragUv();

    // fragColor=texelFetch(iChannel1, ivec2(FragUv*iChannelResolution[1].xy), 0 );
    // fragColor=vec4(0.1*log(fragColor.r+1.),0.1*log(fragColor.g+1.),0.1*log(fragColor.b+1.),0.1*log(fragColor.a+1.));
}

//void main()
//{
//}
