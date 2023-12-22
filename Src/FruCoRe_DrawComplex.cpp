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
    DWORD DrawFlag,
    simd::float4 *TextureCoords = nullptr,
    simd::float4 *TextureInfo = nullptr
)
{
    Data->DrawFlags |= DrawFlag;
    RenDev->SetTexture(TexNum, Info, PolyFlags, PanBias);
    const auto Texture = RenDev->BoundTextures[TexNum];
    if (TextureCoords)
        *TextureCoords = simd::make_float4(Texture->UMult, Texture->VMult, Texture->UPan, Texture->VPan);
    if (TextureInfo && Info.Texture)
        *TextureInfo = simd::make_float4(Info.Texture->Diffuse, Info.Texture->Specular, Info.Texture->Alpha, Info.Texture->Scale);
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
    
    const auto PolyFlags = FixPolyFlags(Surface.PolyFlags);
    SetBlendAndDepthMode(PolyFlags);
    
    DrawData->DrawFlags = DF_DiffuseTexture;
    
    // Bind all textures
    SetTextureHelper(this, DrawData, 0, *Surface.Texture, PolyFlags, 0.0, DF_DiffuseTexture, &DrawData->DiffuseUV, &DrawData->DiffuseInfo);
    
    if (Surface.LightMap)
        SetTextureHelper(this, DrawData, 1, *Surface.LightMap, PolyFlags, -0.5, DF_LightMap, &DrawData->LightMapUV);

    if (Surface.FogMap)
        SetTextureHelper(this, DrawData, 2, *Surface.FogMap, PF_Straight_AlphaBlend, -0.5, DF_FogMap, &DrawData->FogMapUV);
    
    if (Surface.DetailTexture && DetailTextures)
        SetTextureHelper(this, DrawData, 3, *Surface.DetailTexture, PolyFlags, 0.0, DF_DetailTexture, &DrawData->DetailUV);

    if (Surface.MacroTexture && MacroTextures)
        SetTextureHelper(this, DrawData, 4, *Surface.MacroTexture, PolyFlags, 0.0, DF_MacroTexture, &DrawData->MacroUV, &DrawData->MacroInfo);
        
    const auto FlatColor = Surface.FlatColor;
    DrawData->SurfaceXAxis = simd::make_float4(Facet.MapCoords.XAxis.X, Facet.MapCoords.XAxis.Y, Facet.MapCoords.XAxis.Z, Facet.MapCoords.XAxis | Facet.MapCoords.Origin);
    DrawData->SurfaceYAxis = simd::make_float4(Facet.MapCoords.YAxis.X, Facet.MapCoords.YAxis.Y, Facet.MapCoords.YAxis.Z, Facet.MapCoords.YAxis | Facet.MapCoords.Origin);
    DrawData->DrawColor = simd::make_float4(FlatColor.R, FlatColor.G, FlatColor.B, FlatColor.A);
    DrawData->PolyFlags = PolyFlags;

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
