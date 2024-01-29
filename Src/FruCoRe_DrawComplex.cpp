/*=============================================================================
    FruCoRe_DrawComplex.cpp: Unreal support code for Apple Metal.
    Copyright 2023 OldUnreal. All Rights Reserved.

    Revision history:
    * Created by Stijn Volckaert
=============================================================================*/

#include "Render.h"
#include "FruCoRe.h"

/*-----------------------------------------------------------------------------
    SetTexture
-----------------------------------------------------------------------------*/
static void SetTextureHelper
(
    UFruCoReRenderDevice* RenDev,
    UFruCoReRenderDevice::ComplexInstanceData *Data,
    INT TexNum,
    FTextureInfo &Info,
    DWORD PolyFlags,
    FLOAT PanBias,
    simd::float4 *TextureCoords = nullptr,
    simd::float4 *TextureInfo = nullptr
)
{
    RenDev->SetTexture(TexNum, Info, PolyFlags, PanBias);
    const auto Texture = RenDev->BoundTextures[TexNum];
    if (TextureCoords)
	{
        *TextureCoords = simd::make_float4(Texture->UMult,
										   Texture->VMult,
										   Texture->UPan,
										   Texture->VPan);
	}
    if (TextureInfo && Info.Texture)
	{
        *TextureInfo = simd::make_float4(Info.Texture->Diffuse,
										 Info.Texture->Specular,
										 Info.Texture->Alpha,
#if ENGINE_VERSION==227
										 Info.Texture->DrawScale);
#else
										 Info.Texture->Scale);
#endif
	}
}

/*-----------------------------------------------------------------------------
    DrawComplexSurface
-----------------------------------------------------------------------------*/
static simd::float4 FVectorToFloat4(FVector& Vec)
{
    return simd::make_float4(Vec.X, Vec.Y, Vec.Z, 0.f);
}

void UFruCoReRenderDevice::DrawComplexSurface(FSceneNode *Frame, FSurfaceInfo &Surface, FSurfaceFacet &Facet)
{
    SetProgram(SHADER_Complex);
    auto Shader = dynamic_cast<DrawComplexProgram*>(Shaders[SHADER_Complex]);
    
    if (!Shader->InstanceDataBuffer.CanBuffer(1))
        Shader->RotateBuffers();
    
    auto DrawData = Shader->InstanceDataBuffer.GetCurrentElementPtr();
    
    DWORD Options = OPT_None;
    const auto PolyFlags = GetPolyFlags(Surface.PolyFlags, Options);
    
    // Bind all textures
    SetTextureHelper(this, DrawData, IDX_DiffuseTexture, *Surface.Texture, PolyFlags, 0.0, &DrawData->DiffuseUV, &DrawData->DiffuseInfo);
    
    if (Surface.LightMap)
    {
        SetTextureHelper(this, DrawData, IDX_LightMap, *Surface.LightMap, PolyFlags, -0.5, &DrawData->LightMapUV);
        Options |= OPT_LightMap;
    }

    if (Surface.FogMap)
    {
        SetTextureHelper(this, DrawData, IDX_FogMap, *Surface.FogMap, PF_Straight_AlphaBlend, -0.5, &DrawData->FogMapUV);
        Options |= OPT_FogMap;
    }
    
    if (Surface.DetailTexture && DetailTextures)
    {
        SetTextureHelper(this, DrawData, IDX_DetailTexture, *Surface.DetailTexture, PolyFlags, 0.0, &DrawData->DetailUV);
        Options |= OPT_DetailTexture;
    }

    if (Surface.MacroTexture && MacroTextures)
    {
        SetTextureHelper(this, DrawData, IDX_MacroTexture, *Surface.MacroTexture, PolyFlags, 0.0, &DrawData->MacroUV, &DrawData->MacroInfo);
        Options |= OPT_MacroTexture;
    }
        
    const auto FlatColor = Surface.FlatColor;
    DrawData->SurfaceXAxis = simd::make_float4(Facet.MapCoords.XAxis.X, Facet.MapCoords.XAxis.Y, Facet.MapCoords.XAxis.Z, Facet.MapCoords.XAxis | Facet.MapCoords.Origin);
    DrawData->SurfaceYAxis = simd::make_float4(Facet.MapCoords.YAxis.X, Facet.MapCoords.YAxis.Y, Facet.MapCoords.YAxis.Z, Facet.MapCoords.YAxis | Facet.MapCoords.Origin);
    DrawData->DrawColor = simd::make_float4(FlatColor.R, FlatColor.G, FlatColor.B, FlatColor.A);

    Shader->SelectPipelineState(GetBlendMode(PolyFlags), static_cast<ShaderOptions>(Options));
    SetDepthMode(((PolyFlags & PF_Occlude) == PF_Occlude) ? DEPTH_Test_And_Write : DEPTH_Test_No_Write);
    Shader->DrawBuffer.StartDrawCall();
    
    INT FacetVertexCount = 0;
    for (FSavedPoly* Poly = Facet.Polys; Poly; Poly = Poly->Next)
    {
        const INT NumPts = Poly->NumPts;
        if (NumPts < 3) //Skip invalid polygons,if any?
            continue;

        if (!Shader->VertexBuffer.CanBuffer((NumPts - 2) * 3))
        {
            Shader->DrawBuffer.EndDrawCall(FacetVertexCount);
            Shader->InstanceDataBuffer.Advance(1);
            
            // Make a backup of the instance parameters so we can start our
            // new instancedata buffer with a copy of the current parameters
            ComplexInstanceData Tmp;
            memcpy(&Tmp, DrawData, sizeof(ComplexInstanceData));
            
            Shader->RotateBuffers();
            
            DrawData = Shader->InstanceDataBuffer.GetCurrentElementPtr();
            memcpy(DrawData, &Tmp, sizeof(ComplexInstanceData));
            
            Shader->DrawBuffer.StartDrawCall();
            FacetVertexCount = 0;
        }

        FTransform** In = &Poly->Pts[0];
        auto Out = Shader->VertexBuffer.GetCurrentElementPtr();

        for (INT i = 0; i < NumPts - 2 ; i++)
        {
            (Out++)->Point = FVectorToFloat4(In[0    ]->Point);
            (Out++)->Point = FVectorToFloat4(In[i + 1]->Point);
            (Out++)->Point = FVectorToFloat4(In[i + 2]->Point);
        }

        FacetVertexCount  += (NumPts - 2) * 3;
        Shader->VertexBuffer.Advance((NumPts - 2) * 3);
    }

    Shader->DrawBuffer.EndDrawCall(FacetVertexCount);
    Shader->InstanceDataBuffer.Advance(1);
}

/*-----------------------------------------------------------------------------
    BuildCommonPipelineStates
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::DrawComplexProgram::BuildCommonPipelineStates()
{
    SelectPipelineState(BLEND_None, OPT_None);
    SelectPipelineState(BLEND_None, OPT_Masked);
    SelectPipelineState(BLEND_None, OPT_LightMap);
    SelectPipelineState(BLEND_None, static_cast<ShaderOptions>(OPT_LightMap|OPT_Masked));
    SelectPipelineState(BLEND_None, static_cast<ShaderOptions>(OPT_LightMap|OPT_FogMap));
    SelectPipelineState(BLEND_None, static_cast<ShaderOptions>(OPT_LightMap|OPT_FogMap|OPT_Masked));
    SelectPipelineState(BLEND_None, OPT_DetailTexture);
    SelectPipelineState(BLEND_None, static_cast<ShaderOptions>(OPT_DetailTexture|OPT_LightMap));
    SelectPipelineState(BLEND_None, static_cast<ShaderOptions>(OPT_DetailTexture|OPT_LightMap|OPT_FogMap));
    SelectPipelineState(BLEND_None, static_cast<ShaderOptions>(OPT_DetailTexture|OPT_MacroTexture|OPT_LightMap));
}
