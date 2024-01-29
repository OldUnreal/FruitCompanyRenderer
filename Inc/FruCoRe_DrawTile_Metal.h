typedef struct
{
    simd::float4 Point;
    simd::float4 UV;
} TileVertex;

// Data for one draw call
typedef struct
{
    simd::float4 DrawColor;
    simd::float4 HitColor;
    float UMult;
    float VMult;
    float UPan;
    float VPan;
} TileInstanceData;
