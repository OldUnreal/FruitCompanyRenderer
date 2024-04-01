#include <metal_stdlib>
using namespace metal;
using namespace simd;

#include "../../Engine/Inc/UnPolyFlag.h"
#include "../Inc/FruCoRe_Shared_Metal.h"
#include "../Inc/FruCoRe_DrawSimple_Metal.h"

typedef struct
{
    float4 Position [[position]];
    float4 DrawColor;
} SimpleTriangleVertexOutput;

vertex SimpleTriangleVertexOutput DrawSimpleTriangleVertex
(
    uint VertexID [[vertex_id]],
    uint InstanceID [[instance_id]],
    device const GlobalUniforms* Uniforms           [[ buffer(IDX_Uniforms)                       ]],
    device const SimpleTriangleInstanceData* Data   [[ buffer(IDX_DrawSimpleTriangleInstanceData) ]],
    device const SimpleTriangleVertex* Vertices     [[ buffer(IDX_DrawSimpleTriangleVertexData)   ]]
)
{
    SimpleTriangleVertexOutput Result;
    
    float4 InVertex = float4(Vertices[VertexID].Point.xyz, 1.0);
    
    // Should we move the vertex into the near plane?
    // InVertex.z += Uniforms->zNear - 1;

    float4 Projected = Uniforms->ProjectionMatrix * InVertex;

    // The vertex is already in screen space coordinates. We just need to convert to NDC
    Result.Position = float4(
        -1 + 2 * InVertex.x / Uniforms->ViewportWidth,
        1 - 2 * InVertex.y / Uniforms->ViewportHeight,
        Projected.z / Projected.w,
        1 // We don't want any normalization so we set w to 1 here
    );
    
    Result.DrawColor = Data[InstanceID].DrawColor;
    return Result;
}

float4 fragment DrawSimpleTriangleFragment
(
    SimpleTriangleVertexOutput in [[stage_in]],
    device const GlobalUniforms* Uniforms [[ buffer(IDX_Uniforms) ]]
)
{
    return float4(in.DrawColor.rgb * Uniforms->Brightness, in.DrawColor.a);
}
