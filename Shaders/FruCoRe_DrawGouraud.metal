#include <metal_stdlib>
using namespace metal;
using namespace simd;

#include "../../Engine/Inc/UnPolyFlag.h"
#include "../Inc/FruCoRe_Shared_Metal.h"
#include "../Inc/FruCoRe_DrawGouraud_Metal.h"

typedef struct
{
    float4 Position [[position]];
    float4 DrawColor;
	float4 LightColor;
	float4 FogColor;
    float2 DiffuseUV;
    float2 DiffuseInfo;
    float2 DetailUV;
    float2 MacroUV;
} GouraudVertexOutput;

vertex GouraudVertexOutput DrawGouraudVertex
(
    uint VertexID                           [[ vertex_id ]],
    uint InstanceID                         [[ instance_id ]],
    device const GlobalUniforms* Uniforms   [[ buffer(IDX_Uniforms)                  ]],
    device const GouraudInstanceData* Data  [[ buffer(IDX_DrawGouraudInstanceData)   ]],
    device const GouraudVertex* Vertices    [[ buffer(IDX_DrawGouraudVertexData)     ]]
)
{
    float4 InVertex = Vertices[VertexID].Point;
    
    // Some z-hacking to make sure the weapon render properly
    //if (Data[InstanceID].DrawFlags & DF_NoNearZ)
    //    InVertex.z += Uniforms->zNear - 1;
    
    GouraudVertexOutput Result;
    Result.Position     = Uniforms->ProjectionMatrix * InVertex;
    
    //if (Data[InstanceID].DrawFlags & DF_NoNearZ)
    //    Result.Position.w -= Uniforms->zNear - 1;
    
    Result.LightColor   = Vertices[VertexID].LightColor;
    Result.FogColor     = Vertices[VertexID].FogColor;
    Result.DiffuseUV    = Vertices[VertexID].UV.xy * Data[InstanceID].DiffuseInfo.xy;
    Result.DiffuseInfo  = Data[InstanceID].DiffuseInfo.zw;
    Result.DetailUV     = Vertices[VertexID].UV.xy * Data[InstanceID].DetailMacroInfo.xy;
    Result.MacroUV      = Vertices[VertexID].UV.xy * Data[InstanceID].DetailMacroInfo.zw;
    return Result;
}

float4 fragment DrawGouraudFragment
(
    GouraudVertexOutput in [[stage_in]],
    texture2d< float, access::sample > DiffuseTexture [[ texture(IDX_DiffuseTexture)                                      ]],
    texture2d< float, access::sample > DetailTexture  [[ texture(IDX_DetailTexture) , function_constant(HasDetailTexture) ]],
    texture2d< float, access::sample > MacroTexture   [[ texture(IDX_MacroTexture)  , function_constant(HasMacroTexture)  ]],
    device const GlobalUniforms* Uniforms             [[ buffer(IDX_Uniforms)                                             ]]
)
{
    constexpr sampler s( address::repeat, filter::linear );
    float4 Color = DiffuseTexture.sample(s, in.DiffuseUV, bias(Uniforms->LODBias)).rgba;
    
    Color = ApplyPolyFlags(Color, in.LightColor);
    in.Position.w = 1.0;
    
    float4 TotalColor = float4(1.0);
    
    // Handle fog
    if (ShouldRenderFog)
    {
        // Special case: fog+modulated
        if (IsModulated)
        {
            // Code stolen errr borrowed from XOpenGLDrv
            float3 Delta = float3(0.5) - Color.xyz;
            Delta *= 1.0 - in.FogColor.a;
            Delta *= float3(1.0) - in.FogColor.rgb;
            TotalColor = float4(float3(0.5) - Delta, Color.a);
        }
        else
        {
            Color *= in.LightColor;
            TotalColor.rgb = Color.rgb * (1.0 - in.FogColor.a) + in.FogColor.rgb;
            TotalColor.a = Color.a;
        }
    }
    else if (IsModulated)
    {
        // Modulated and no fog
        TotalColor = Color;
    }
    else
    {
        // Just lighting
        TotalColor = Color * float4(in.LightColor.rgb, 1.0);
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
        float3 hsvMacroTex = rgb2hsv(MacroTexColor.rgb);
        hsvMacroTex.b += (MacroTexColor.r - 0.1);
        hsvMacroTex = hsv2rgb(hsvMacroTex);
        MacroTexColor = float4(hsvMacroTex, 1.0);
        TotalColor *= MacroTexColor;
    }

    if (!IsModulated)
	    TotalColor.rgb *=  Uniforms->Brightness;    
    return TotalColor;
}
