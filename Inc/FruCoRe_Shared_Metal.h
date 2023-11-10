enum DrawFlags
{
    DF_None             = 0x00000000, 
    DF_DiffuseTexture   = 0x00000001,
    DF_LightMap         = 0x00000002,
    DF_FogMap           = 0x00000004,
    DF_DetailTexture    = 0x00000008,
    DF_MacroTexture     = 0x00000010,
    DF_BumpMap          = 0x00000020,
    DF_EnvironmentMap   = 0x00000040,
    DF_HeightMap        = 0x00000080,
    DF_NoNearZ          = 0x00000100,
};

//
// Uniforms
//
struct GlobalUniforms
{
    // Camera coordinates => NDC
    // Note: we don't need a model matrix anywhere in UE1 because the base renderer passes
    // all level geometry and mesh coordinates in camera space
    simd::float4x4 ProjectionMatrix;

    // For screen coordinates => NDC
    float ViewportWidth;
    float ViewportHeight;
    float ViewportOriginX;
    float ViewportOriginY;
    float zNear;
    float zFar;
    
    float Gamma;
    bool HitTesting;
    uint32_t RendMap;
    uint32_t DetailMax;
};

#if __METAL__
inline float4 GammaCorrect(float Gamma, float4 Color)
{
    float InvGamma = 1.0 / Gamma;
    Color.r = pow(Color.r, InvGamma);
    Color.g = pow(Color.g, InvGamma);
    Color.b = pow(Color.b, InvGamma);
    return Color;
}

inline float4 ApplyPolyFlags(float4 Color, float4 LightColor, unsigned int PolyFlags)
{
    if (PolyFlags & PF_Masked)
    {
        if (Color.a < 0.5)
            discard_fragment();
        else
            Color.rgb *= Color.a;
    }
    else if (PolyFlags & (PF_Straight_AlphaBlend|PF_Premultiplied_AlphaBlend))
    {
        Color.a *= LightColor.a;
        if (Color.a < 0.01)
            discard_fragment();
    }
    return Color;
}

inline float3 rgb2hsv(float3 c)
{
  float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0); // some nice stuff from http://lolengine.net/blog/2013/07/27/rgb-to-hsv-in-glsl
  float4 p = c.g < c.b ? float4(c.bg, K.wz) : float4(c.gb, K.xy);
  float4 q = c.r < p.x ? float4(p.xyw, c.r) : float4(c.r, p.yzx);
  float d = q.x - min(q.w, q.y);
  float e = 1.0e-10;
  return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

inline float3 hsv2rgb(float3 c)
{
  float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
  float3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
#endif
