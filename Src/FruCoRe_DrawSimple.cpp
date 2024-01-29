/*=============================================================================
    FruCoRe_DrawSimple.cpp: Unreal support code for Apple Metal.
    Copyright 2023 OldUnreal. All Rights Reserved.

    Revision history:
    * Created by Stijn Volckaert
=============================================================================*/

#include "Render.h"
#include "FruCoRe.h"

/*-----------------------------------------------------------------------------
    Draw3DLine
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::Draw3DLine(FSceneNode* Frame, FPlane Color, DWORD LineFlags, FVector OrigP, FVector OrigQ)
{
}

/*-----------------------------------------------------------------------------
    Draw2DClippedLine
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::Draw2DClippedLine(FSceneNode* Frame, FPlane Color, DWORD LineFlags, FVector P1, FVector P2)
{
}

/*-----------------------------------------------------------------------------
    Draw2DLine
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::Draw2DLine(FSceneNode* Frame, FPlane Color, DWORD LineFlags, FVector P1, FVector P2)
{
}

/*-----------------------------------------------------------------------------
    Draw2DPoint
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::Draw2DPoint(FSceneNode* Frame, FPlane Color, DWORD LineFlags, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, FLOAT Z)
{
}

/*-----------------------------------------------------------------------------
    EndFlash
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::EndFlash()
{
    SetProgram(SHADER_Simple_Triangle);
    auto Shader = dynamic_cast<DrawSimpleTriangleProgram*>(Shaders[SHADER_Simple_Triangle]);
    
    if( FlashScale == FPlane(0.5,0.5,0.5,0) && FlashFog == FPlane(0,0,0,0) )
        return;

    if (!Shader->VertexBuffer.CanBuffer(6) || !Shader->InstanceDataBuffer.CanBuffer(1))
        Shader->RotateBuffers();
    
    Shader->SelectPipelineState(GetBlendMode(PF_Highlighted), OPT_None);
    
    auto InstanceData = Shader->InstanceDataBuffer.GetCurrentElementPtr();
    InstanceData->DrawColor = simd::make_float4(FlashFog.X, FlashFog.Y, FlashFog.Z, 1.f - Min(FlashScale.X*2.f,1.f));
    
    Shader->DrawBuffer.StartDrawCall();
    auto Out = Shader->VertexBuffer.GetCurrentElementPtr();
    
    (Out++)->Point = simd::make_float4(-Viewport->SizeX, -Viewport->SizeY, 1.f, 1.f);
    (Out++)->Point = simd::make_float4(+Viewport->SizeX, -Viewport->SizeY, 1.f, 1.f);
    (Out++)->Point = simd::make_float4(+Viewport->SizeX, +Viewport->SizeY, 1.f, 1.f);
    (Out++)->Point = simd::make_float4(-Viewport->SizeX, -Viewport->SizeY, 1.f, 1.f);
    (Out++)->Point = simd::make_float4(+Viewport->SizeX, +Viewport->SizeY, 1.f, 1.f);
    (Out++)->Point = simd::make_float4(-Viewport->SizeX, +Viewport->SizeY, 1.f, 1.f);
    
    Shader->VertexBuffer.Advance(6);
    Shader->InstanceDataBuffer.Advance(1);
    Shader->DrawBuffer.EndDrawCall(6);
}
