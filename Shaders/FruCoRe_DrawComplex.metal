#include <metal_stdlib>
using namespace metal;
using namespace simd;

#include "../../Engine/Inc/UnPolyFlag.h"
#include "../Inc/FruCoRe_Shared_Metal.h"
#include "../Inc/FruCoRe_DrawComplex_Metal.h"

typedef struct
{
    float4 Position [[position]];
    float4 DrawColor;
    float2 DiffuseUV;
    float2 DiffuseInfo;
    float2 LightMapUV;
    float2 FogMapUV;
    float2 DetailUV;
    float2 MacroUV;
    unsigned int PolyFlags;
    unsigned int DrawFlags;
} ComplexVertexOutput;

vertex ComplexVertexOutput DrawComplexVertex
(
    uint VertexID [[vertex_id]],
    uint InstanceID [[instance_id]],
    device const GlobalUniforms* Uniforms [[buffer(0)]],
    device const ComplexInstanceData* Data [[buffer(5)]],
    device const ComplexVertex* Vertices [[buffer(6)]]
)
{
    float4 InVertex = float4(Vertices[VertexID].Point.xyz, 1.0);
    unsigned int DrawFlags = Data[InstanceID].DrawFlags;

    ComplexVertexOutput Result;
    Result.Position = Uniforms->ProjectionMatrix * InVertex;

    // Calculate texture coordinates
    float3 MapCoordsXAxis = Data[InstanceID].SurfaceXAxis.xyz;
    float3 MapCoordsYAxis = Data[InstanceID].SurfaceYAxis.xyz;
    float  UDot    = Data[InstanceID].SurfaceXAxis.w;
    float  VDot    = Data[InstanceID].SurfaceYAxis.w;
    float2 MapDot  = float2(dot(MapCoordsXAxis, InVertex.xyz) - UDot, dot(MapCoordsYAxis, InVertex.xyz) - VDot);

    // Texture UV to fragment
    float2 TexMapMult = Data[InstanceID].DiffuseUV.xy;
    float2 TexMapPan  = Data[InstanceID].DiffuseUV.zw;
    Result.DiffuseUV  = (MapDot - TexMapPan) * TexMapMult;

    if (DrawFlags & DF_LightMap)
    {
        float2 LightMapMult = Data[InstanceID].LightMapUV.xy;
        float2 LightMapPan  = Data[InstanceID].LightMapUV.zw;
        Result.LightMapUV   = (MapDot - LightMapPan) * LightMapMult;
    }

    if (DrawFlags & DF_FogMap)
    {
        float2 FogMapMult = Data[InstanceID].FogMapUV.xy;
        float2 FogMapPan  = Data[InstanceID].FogMapUV.zw;
        Result.FogMapUV   = (MapDot - FogMapPan) * FogMapMult;
    }

    if (DrawFlags & DF_DetailTexture)
    {
        float2 DetailMult = Data[InstanceID].DetailUV.xy;
        float2 DetailPan  = Data[InstanceID].DetailUV.zw;
        Result.DetailUV   = (MapDot - DetailPan) * DetailMult;
    }

    if (DrawFlags & DF_MacroTexture)
    {
        float2 MacroMult = Data[InstanceID].MacroUV.xy;
        float2 MacroPan  = Data[InstanceID].MacroUV.zw;
        Result.MacroUV   = (MapDot - MacroPan) * MacroMult;
    }

    Result.DiffuseInfo = Data[InstanceID].DiffuseInfo.xz;
    Result.DrawColor = Data[InstanceID].DrawColor;
    Result.PolyFlags = Data[InstanceID].PolyFlags;
    Result.DrawFlags = Data[InstanceID].DrawFlags;
    return Result;
}

float4 fragment DrawComplexFragment
(
    ComplexVertexOutput in [[stage_in]],
    texture2d< float, access::sample > DiffuseTexture [[texture(0)]],
    texture2d< float, access::sample > LightMap [[texture(1)]],
    texture2d< float, access::sample > FogMap [[texture(2)]],
    texture2d< float, access::sample > DetailTexture [[texture(3)]],
    texture2d< float, access::sample > MacroTexture [[texture(4)]],
    device const GlobalUniforms* Uniforms [[buffer(0)]]
)
{
    constexpr sampler s(address::repeat, filter::linear);
    float4 Color = DiffuseTexture.sample(s, in.DiffuseUV).rgba;
    
    // Diffuse factor
    if (in.DiffuseInfo.x > 0.0)
        Color *= in.DiffuseInfo.x;
    
    // Alpha
    if (in.DiffuseInfo.y > 0.0)
        Color.a *= in.DiffuseInfo.y;
    
    Color = ApplyPolyFlags(Color, float4(1.0), in.PolyFlags);
    
    float4 LightColor = float4(1.0, 1.0, 1.0, 1.0);
    float4 TotalColor = Color;
    
    if (in.DrawFlags & DF_LightMap)
    {
        LightColor = LightMap.sample(s, in.LightMapUV).bgra;
        LightColor.rgb *= 255.0 / 127.0;
        LightColor.a = 1.0;
    }
    
    if (in.DrawFlags & DF_DetailTexture)
    {
        float NearZ = in.Position.z / 512.0;
        float DetailScale = 1.0;
        float bNear;
        float4 DetailTexColor;
        
        for (uint32_t i = 0; i < Uniforms->DetailMax; ++i)
        {
            if (i > 0)
            {
                NearZ *= 4.223f;
                DetailScale *= 4.223f;
            }
            bNear = clamp(0.65 - NearZ, 0.0, 1.0);
            if (bNear > 0.0)
            {
                DetailTexColor = DetailTexture.sample(s, in.DetailUV * DetailScale);
                
                float3 hsvDetailTex = rgb2hsv(DetailTexColor.rgb); // cool idea Han :)
                hsvDetailTex.b += (DetailTexColor.r - 0.1);
                hsvDetailTex = hsv2rgb(hsvDetailTex);
                DetailTexColor = float4(hsvDetailTex, 0.0);
                DetailTexColor = mix(float4(1.0, 1.0, 1.0, 1.0), DetailTexColor, bNear); //fading out.
                TotalColor.rgb *= DetailTexColor.rgb;
            }
        }
    }
    
    if (in.PolyFlags & DF_MacroTexture)
    {
        float4 MacroTexColor = MacroTexture.sample(s, in.MacroUV).rgba;
        MacroTexColor = ApplyPolyFlags(MacroTexColor, float4(1.0), in.PolyFlags);
        float3 hsvMacroTex = rgb2hsv(MacroTexColor.rgb);
        hsvMacroTex.b += (MacroTexColor.r - 0.1);
        hsvMacroTex = hsv2rgb(hsvMacroTex);
        MacroTexColor = float4(hsvMacroTex, 1.0);
        TotalColor *= MacroTexColor;
    }
    
    float4 FogColor = float4(0.0);
    if (in.DrawFlags & DF_FogMap)
        FogColor = FogMap.sample(s, in.FogMapUV).rgba * 2.0;
    
    if ((in.PolyFlags & PF_Modulated) != PF_Modulated)
        return GammaCorrect(Uniforms->Gamma * 1.7, TotalColor * LightColor + FogColor);
    
    return TotalColor + FogColor;
}
