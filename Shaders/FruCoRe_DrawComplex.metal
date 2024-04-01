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
} ComplexVertexOutput;

vertex ComplexVertexOutput DrawComplexVertex
(
    uint VertexID                           [[ vertex_id ]],
    uint InstanceID                         [[ instance_id ]],
    device const GlobalUniforms* Uniforms   [[ buffer(IDX_Uniforms)                  ]],
    device const ComplexInstanceData* Data  [[ buffer(IDX_DrawComplexInstanceData)   ]],
    device const ComplexVertex* Vertices    [[ buffer(IDX_DrawComplexVertexData)     ]]
)
{
    float4 InVertex = float4(Vertices[VertexID].Point.xyz, 1.0);

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

    if (HasLightMap)
    {
        float2 LightMapMult = Data[InstanceID].LightMapUV.xy;
        float2 LightMapPan  = Data[InstanceID].LightMapUV.zw;
        Result.LightMapUV   = (MapDot - LightMapPan) * LightMapMult;
    }

    if (HasFogMap)
    {
        float2 FogMapMult = Data[InstanceID].FogMapUV.xy;
        float2 FogMapPan  = Data[InstanceID].FogMapUV.zw;
        Result.FogMapUV   = (MapDot - FogMapPan) * FogMapMult;
    }

    if (HasDetailTexture)
    {
        float2 DetailMult = Data[InstanceID].DetailUV.xy;
        float2 DetailPan  = Data[InstanceID].DetailUV.zw;
        Result.DetailUV   = (MapDot - DetailPan) * DetailMult;
    }

    if (HasMacroTexture)
    {
        float2 MacroMult = Data[InstanceID].MacroUV.xy;
        float2 MacroPan  = Data[InstanceID].MacroUV.zw;
        Result.MacroUV   = (MapDot - MacroPan) * MacroMult;
    }

    Result.DiffuseInfo = Data[InstanceID].DiffuseInfo.xz;
    Result.DrawColor = Data[InstanceID].DrawColor;
    return Result;
}

float4 fragment DrawComplexFragment
(
    ComplexVertexOutput in [[stage_in]],
    texture2d< float, access::sample > DiffuseTexture   [[ texture(IDX_DiffuseTexture)                                      ]],
    texture2d< float, access::sample > LightMap         [[ texture(IDX_LightMap)      , function_constant(HasLightMap)      ]],
    texture2d< float, access::sample > FogMap           [[ texture(IDX_FogMap)        , function_constant(HasFogMap)        ]],
    texture2d< float, access::sample > DetailTexture    [[ texture(IDX_DetailTexture) , function_constant(HasDetailTexture) ]],
    texture2d< float, access::sample > MacroTexture     [[ texture(IDX_MacroTexture)  , function_constant(HasMacroTexture)  ]],
    device const GlobalUniforms* Uniforms               [[ buffer(IDX_Uniforms)                                             ]]
)
{
    constexpr sampler s(address::repeat, filter::linear);
    float4 Color = DiffuseTexture.sample(s, in.DiffuseUV, bias(Uniforms->LODBias)).rgba;
    
    Color = ApplyPolyFlags(Color, float4(1.0));
    
    float4 LightColor = float4(1.0, 1.0, 1.0, 1.0);
    float4 TotalColor = Color;
    
    if (HasLightMap)
    {
        LightColor = LightMap.sample(s, in.LightMapUV, bias(Uniforms->LODBias)).bgra;
        LightColor.rgb = LightColor.rgb * Uniforms->LightMapFactor;
        LightColor.a = 1.0;
    }
    
    if (HasDetailTexture)
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
                DetailTexColor = DetailTexture.sample(s, in.DetailUV * DetailScale, bias(Uniforms->LODBias));
                
                float3 hsvDetailTex = rgb2hsv(DetailTexColor.rgb); // cool idea Han :)
                hsvDetailTex.b += (DetailTexColor.r - 0.1);
                hsvDetailTex = hsv2rgb(hsvDetailTex);
                DetailTexColor = float4(hsvDetailTex, 0.0);
                DetailTexColor = mix(float4(1.0, 1.0, 1.0, 1.0), DetailTexColor, bNear); //fading out.
                TotalColor.rgb *= DetailTexColor.rgb;
            }
        }
    }
    
    if (HasMacroTexture)
    {
        float4 MacroTexColor = MacroTexture.sample(s, in.MacroUV, bias(Uniforms->LODBias)).rgba;
        MacroTexColor = ApplyPolyFlags(MacroTexColor, float4(1.0));
        float3 hsvMacroTex = rgb2hsv(MacroTexColor.rgb);
        hsvMacroTex.b += (MacroTexColor.r - 0.1);
        hsvMacroTex = hsv2rgb(hsvMacroTex);
        MacroTexColor = float4(hsvMacroTex, 1.0);
        TotalColor *= MacroTexColor;
    }
    
    float4 FogColor = float4(0.0);
    if (HasFogMap)
    {
        FogColor = FogMap.sample(s, in.FogMapUV, bias(Uniforms->LODBias));
        FogColor.rgb *= 2.0;
    }
    
    TotalColor.rgb *= Uniforms->Brightness;
    LightColor.rgb *= Uniforms->Brightness;
    FogColor.rgb *= Uniforms->Brightness;
    if (!IsModulated)
        return TotalColor * LightColor + FogColor;
    
    return TotalColor + FogColor;
}
