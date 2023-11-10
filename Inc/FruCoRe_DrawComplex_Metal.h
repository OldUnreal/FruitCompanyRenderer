typedef struct
{
    simd::float4 Point;
} ComplexVertex;

// Data for one draw call
typedef struct
{
    simd::float4 SurfaceXAxis;
    simd::float4 SurfaceYAxis;
    simd::float4 DiffuseUV;
    simd::float4 LightMapUV;
    simd::float4 FogMapUV;
    simd::float4 DetailUV;
    simd::float4 MacroUV;
    simd::float4 DiffuseInfo;
    simd::float4 MacroInfo;
    simd::float4 DrawColor;
    uint32_t DrawFlags;
    uint32_t PolyFlags;
    uint32_t Padding0;
    uint32_t Padding1;
} ComplexInstanceData;
