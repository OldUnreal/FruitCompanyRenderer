#include <metal_stdlib>
using namespace metal;
using namespace simd;  

#include "../../Engine/Inc/UnPolyFlag.h"
#include "../Inc/FruCoRe_Shared_Metal.h"
#include "../Inc/FruCoRe_DrawTile_Metal.h"

typedef struct
{
    float4 Position [[position]];
    float4 DrawColor;
    float2 UV;
    unsigned int PolyFlags;
} TileVertexOutput;

vertex TileVertexOutput DrawTileVertex
(
    uint VertexID [[vertex_id]],
    uint InstanceID [[instance_id]],
    device const GlobalUniforms* Uniforms [[buffer(0)]],
    device const TileInstanceData* Data [[buffer(1)]],
    device const TileVertex* Vertices [[buffer(2)]]
)
{
    TileVertexOutput Result;
    float4 InVertex  = Vertices[VertexID].Point;
    float4 Projected = Uniforms->ProjectionMatrix * InVertex;
    // Make sure that points _on_ the near plane have an NDC depth of 0
    Projected.z -= Uniforms->zNear;
    // The vertex is already in screen space coordinates. We just need to convert to clip space
    Result.Position = float4(
        -1.f + 2.f * InVertex.x / Uniforms->ViewportWidth,
        +1.f - 2.f * InVertex.y / Uniforms->ViewportHeight,
        Projected.z / Projected.w,
        1.f // We don't want any normalization so we set w to 1 here. This makes the clip space coordinates equal to the final NDC coordinates
    );
    Result.UV = float2(
        Vertices[VertexID].UV.x * Data[InstanceID].UMult,
        Vertices[VertexID].UV.y * Data[InstanceID].VMult
    );
    Result.DrawColor = Data[InstanceID].DrawColor;
    Result.PolyFlags = Data[InstanceID].PolyFlags;
    return Result;
}

float4 fragment DrawTileFragment
(
    TileVertexOutput in [[stage_in]],
    texture2d< float, access::sample > tex [[texture(0)]],
    device const GlobalUniforms* Uniforms [[buffer(0)]]
)
{
    constexpr sampler s( address::repeat, filter::linear );
    float4 Color = ApplyPolyFlags(tex.sample(s, in.UV).rgba, float4(1.0), in.PolyFlags);
    float4 TotalColor = Color * in.DrawColor;
    
    if ((in.PolyFlags & PF_Modulated) != PF_Modulated)
        TotalColor = GammaCorrect(Uniforms->Gamma, TotalColor);
    
    return TotalColor;
}
