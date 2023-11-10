/*=============================================================================
    FruCoRe_DrawTile.cpp: Unreal support code for Apple Metal.
    Copyright 2023 OldUnreal. All Rights Reserved.

    Revision history:
    * Created by Stijn Volckaert
=============================================================================*/

#include "Render.h"
#include "FruCoRe.h"

/*-----------------------------------------------------------------------------
    DrawTile
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::DrawTile(FSceneNode* Frame, FTextureInfo& Info, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT U, FLOAT V, FLOAT UL, FLOAT VL, class FSpanBuffer* Span, FLOAT Z, FPlane Color, FPlane Fog, DWORD PolyFlags)
{
    SetSceneNode(Frame);
    
    SetProgram(Tile_Prog);
    auto Shader = dynamic_cast<DrawTileProgram*>(Shaders[Tile_Prog]);

    if (PolyFlags & (PF_Translucent | PF_Modulated))
        PolyFlags &= ~PF_Occlude;
    else PolyFlags |= PF_Occlude;
    PolyFlags &= ~(PF_RenderHint | PF_Unlit);
    
    // Hack to render HUD on top of everything in 469
#if UNREAL_TOURNAMENT_OLDUNREAL
    UBOOL ShouldDepthTest = ((GUglyHackFlags & HACKFLAGS_PostRender) == 0) || Abs(1.f - Z) > SMALL_NUMBER;
#endif
    
    if (PolyFlags & PF_Modulated)
        Color = FPlane(1.f, 1.f, 1.f, 1.f);
        
    if (Info.Texture && Info.Texture->Alpha > 0.f)
        Color.W = Info.Texture->Alpha;
    else Color.W = 1.0f;
    
    if (!Shader->VertexBuffer.CanBuffer(6) || !Shader->InstanceDataBuffer.CanBuffer(1))
        Shader->RotateBuffers();
    
    SetBlendMode(PolyFlags);
    SetDepthTesting(ShouldDepthTest);
    SetTexture(0, Info, PolyFlags, 0.f);
    const auto Texture = BoundTextures[0];
    
    auto InstanceData = Shader->InstanceDataBuffer.GetCurrentElementPtr();
    InstanceData->DrawColor = simd_make_float4(Color.X, Color.Y, Color.Z, Color.W);
    InstanceData->PolyFlags = PolyFlags;
    InstanceData->UPan      = Info.Pan.X;
    InstanceData->VPan      = Info.Pan.Y;
    InstanceData->UMult     = Texture->UMult;
    InstanceData->VMult     = Texture->VMult;
    
    // Buffer the tile
    Shader->DrawBuffer.StartDrawCall();
    auto Out = Shader->VertexBuffer.GetCurrentElementPtr();
    
    Out[0].Point = simd::make_float4(X      , Y     , Z, 1.f);
    Out[1].Point = simd::make_float4(X + XL , Y     , Z, 1.f);
    Out[2].Point = simd::make_float4(X + XL , Y + YL, Z, 1.f);
    Out[3].Point = simd::make_float4(X      , Y     , Z, 1.f);
    Out[4].Point = simd::make_float4(X + XL , Y + YL, Z, 1.f);
    Out[5].Point = simd::make_float4(X      , Y + YL, Z, 1.f);
    
    Out[0].UV = simd::make_float4(U         , V     , 0.f, 0.f);
    Out[1].UV = simd::make_float4(U + UL    , V     , 0.f, 0.f);
    Out[2].UV = simd::make_float4(U + UL    , V + VL, 0.f, 0.f);
    Out[3].UV = simd::make_float4(U         , V     , 0.f, 0.f);
    Out[4].UV = simd::make_float4(U + UL    , V + VL, 0.f, 0.f);
    Out[5].UV = simd::make_float4(U         , V + VL, 0.f, 0.f);
    
    Shader->VertexBuffer.Advance(6);
    Shader->InstanceDataBuffer.Advance(1);
    Shader->DrawBuffer.EndDrawCall(6);
}

/*-----------------------------------------------------------------------------
    DeactivateShader - make sure we re-enable depth testing when we're done
    with DrawTile!
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::DrawTileProgram::DeactivateShader()
{
    Flush();
    ActivePipelineState = nullptr;
    RenDev->SetDepthTesting(true);
}
