#include <metal_stdlib>
using namespace metal;
using namespace simd;

#include "../../Engine/Inc/UnPolyFlag.h"
#include "../Inc/FruCoRe_Shared_Metal.h"

vertex SimpleVertexOutput GammaCorrectVertex
(
    uint VertexID [[vertex_id]]
)
{
    const float2 InVertex = FullscreenQuad[VertexID];
    SimpleVertexOutput Result;
    Result.Position = float4(InVertex.xy, 0, 1);
    Result.Position.y *= -1;
    Result.UV = InVertex * 0.5 + 0.5;
    return Result;
}

fragment float4 GammaCorrectFragment
(
    SimpleVertexOutput in [[stage_in]],
    texture2d<float, access::sample> tex,
    device const GlobalUniforms* Uniforms [[buffer(IDX_Uniforms)]]
)
{
    constexpr sampler s(mag_filter::linear, min_filter::linear, mip_filter::none, address::clamp_to_edge);
    //constexpr sampler s(mag_filter::nearest, min_filter::nearest, mip_filter::none, address::clamp_to_edge);
    return GammaCorrect(Uniforms->Gamma, tex.sample(s, in.UV));
}
