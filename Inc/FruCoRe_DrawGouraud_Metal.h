typedef struct
{
    simd::float4 Point;
    simd::float4 Normal;
    simd::float4 UV;
    simd::float4 LightColor;
    simd::float4 FogColor;
} GouraudVertex;

// Data for one draw call
typedef struct
{
    simd::float4 DiffuseInfo;
    simd::float4 DetailMacroInfo;
    simd::float4 HitColor;
} GouraudInstanceData;
