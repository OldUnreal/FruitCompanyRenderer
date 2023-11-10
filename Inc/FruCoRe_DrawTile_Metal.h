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
    uint32_t PolyFlags;
    uint32_t Padding0;
    uint32_t Padding1;
    uint32_t Padding2;
    float UMult;
    float VMult;
    float UPan;
    float VPan;
} TileInstanceData;
