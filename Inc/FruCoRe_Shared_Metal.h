enum ShaderOptions
{
    OPT_None            = 0x0000,
    OPT_DetailTexture   = 0x0001,
    OPT_MacroTexture    = 0x0002,
    OPT_LightMap        = 0x0004,
    OPT_FogMap          = 0x0008,
    OPT_RenderFog       = 0x0010,
    OPT_Modulated       = 0x0020,
    OPT_Masked          = 0x0040,
    OPT_AlphaBlended    = 0x0080,  // straight or premultiplied. doesn't matter
    OPT_NoMSAA          = 0x0100,
    OPT_MSAAx2          = 0x0200,
    OPT_MSAAx4          = 0x0400,
    OPT_MSAAx8          = 0x0800,
	OPT_NoSmooth        = 0x1000,
    OPT_Max             = 0x1000
};

// Metal vertex shaders all share the same argument table.
// As such, we cannot/should not change vertex/instance buffers when setting a new pipeline state.
// Instead, we bind every vertex buffer and instance data buffer to a unique buffer index.
enum BufferIndices
{
    IDX_Uniforms,                       // 0
    IDX_DrawTileInstanceData,           // 1
    IDX_DrawTileVertexData,             // 2
    IDX_DrawGouraudInstanceData,        // 3
    IDX_DrawGouraudVertexData,          // 4
    IDX_DrawComplexInstanceData,        // 5
    IDX_DrawComplexVertexData,          // 6
    IDX_DrawSimpleTriangleInstanceData, // 7
    IDX_DrawSimpleTriangleVertexData,   // 8
    IDX_DrawSimpleLineInstanceData,     // 9
    IDX_DrawSimpleLineVertexData        // 10
};

enum TextureIndices
{
    IDX_DiffuseTexture,
    IDX_LightMap,
    IDX_FogMap,
    IDX_DetailTexture,
    IDX_MacroTexture
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
    
    float LODBias;
    float Brightness;
    float Gamma;
    float LightMapFactor; // 2 for OneXBlending, 4 for !OneXBlending
	float LightColorIntensity; // 1 for ActorXBlending, 1.5 for !ActorXBlending
	bool HitTesting;
    uint32_t RendMap;
    uint32_t DetailMax;
};

#if __METAL_VERSION__

//
// Shader specialization options
//
constant bool HasLightMap       [[ function_constant(OPT_LightMap)      ]];
constant bool HasFogMap         [[ function_constant(OPT_FogMap)        ]];
constant bool HasDetailTexture  [[ function_constant(OPT_DetailTexture) ]];
constant bool HasMacroTexture   [[ function_constant(OPT_MacroTexture)  ]];
constant bool IsModulated       [[ function_constant(OPT_Modulated)     ]];
constant bool IsMasked          [[ function_constant(OPT_Masked)        ]];
constant bool IsAlphaBlended    [[ function_constant(OPT_AlphaBlended)  ]];
constant bool ShouldRenderFog   [[ function_constant(OPT_RenderFog)     ]];
constant bool NoSmooth          [[ function_constant(OPT_NoSmooth)      ]];

constant float2 FullscreenQuad[] =
{
    float2(-1, -1),
    float2(-1,  1),
    float2( 1,  1),
    float2(-1, -1),
    float2( 1,  1),
    float2( 1, -1)
};

typedef struct
{
    float4 Position [[position]];
    float2 UV;
} SimpleVertexOutput;

inline float4 GammaCorrect(float Gamma, float4 Color)
{
    return float4(pow(Color.rgb, 1.0 / Gamma), 1.0);
}

inline float4 ApplyPolyFlags(float4 Color, float4 LightColor)
{
    if (IsMasked)
    {
        if (Color.a < 0.5)
            discard_fragment();
        else
            Color.rgb *= Color.a;
    }
    else if (IsAlphaBlended)
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
