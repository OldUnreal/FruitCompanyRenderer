/*=============================================================================
    FruCoRe_DrawGouraud.cpp: Unreal support code for Apple Metal.
    Copyright 2023 OldUnreal. All Rights Reserved.

    Revision history:
    * Created by Stijn Volckaert
=============================================================================*/

#include "Render.h"
#include "FruCoRe.h"

/*-----------------------------------------------------------------------------
    RenDev Interface
-----------------------------------------------------------------------------*/

void UFruCoReRenderDevice::DrawGouraudPolygon(FSceneNode* Frame, FTextureInfo& Info, FTransTexture** Pts, INT NumPts, DWORD PolyFlags, FSpanBuffer* Span)
{
	if (RendererSuspended)
		return;
	
    SetProgram(SHADER_Gouraud);
    auto Shader = dynamic_cast<DrawGouraudProgram*>(Shaders[SHADER_Gouraud]);
    
    if (NumPts < 3 /*|| Frame->Recursion > MAX_FRAME_RECURSION*/ ) //reject invalid.
        return;

    auto InVertexCount  = NumPts - 2;
    auto OutVertexCount = InVertexCount * 3;

#if ENGINE_VERSION==227
    if (Info.Modifier)
    {
        FLOAT UM = Info.USize, VM = Info.VSize;
        for (INT i = 0; i < NumPts; ++i)
            Info.Modifier->TransformPointUV(Pts[i]->U, Pts[i]->V, UM, VM);
    }
#endif

    if (!Shader->VertexBuffer.CanBuffer(OutVertexCount) || !Shader->InstanceDataBuffer.CanBuffer(1))
        Shader->RotateBuffers();

    Shader->PrepareDrawCall(Frame, Info, PolyFlags);
    
    Shader->DrawBuffer.StartDrawCall();
    auto Out = Shader->VertexBuffer.GetCurrentElementPtr();

    // Unfan and buffer
    for (INT i=0; i < InVertexCount; i++)
    {
        Shader->BufferVert(Out++, Pts[0    ]);
        Shader->BufferVert(Out++, Pts[i + 1]);
        Shader->BufferVert(Out++, Pts[i + 2]);
    }

    Shader->DrawBuffer.EndDrawCall(OutVertexCount);
    Shader->VertexBuffer.Advance(OutVertexCount);
    Shader->FinishDrawCall(Info);
    Shader->InstanceDataBuffer.Advance(1);
}

#if ENGINE_VERSION==227 || UNREAL_TOURNAMENT_OLDUNREAL
void UFruCoReRenderDevice::DrawGouraudPolyList(FSceneNode* Frame, FTextureInfo& Info, FTransTexture* Pts, INT NumPts, DWORD PolyFlags, FSpanBuffer* Span)
{
	if (RendererSuspended)
		return;
	
    SetProgram(SHADER_Gouraud);
    auto Shader = dynamic_cast<DrawGouraudProgram*>(Shaders[SHADER_Gouraud]);
    
    if (NumPts < 3 /*|| Frame->Recursion > MAX_FRAME_RECURSION*/) //reject invalid.
        return;

#if ENGINE_VERSION==227
    if (Info.Modifier)
    {
        FLOAT UM = Info.USize, VM = Info.VSize;
        for (INT i = 0; i < NumPts; ++i)
            Info.Modifier->TransformPointUV(Pts[i].U, Pts[i].V, UM, VM);
    }
#endif

    Shader->PrepareDrawCall(Frame, Info, PolyFlags);

    Shader->DrawBuffer.StartDrawCall();
    auto Out = Shader->VertexBuffer.GetCurrentElementPtr();
    auto End = Shader->VertexBuffer.GetLastElementPtr();

    INT PolyListSize = 0;
    for (INT i = 0; i < NumPts; i++)
    {
        // Polylists can be bigger than the vertex buffer so check here if we
        // need to split the mesh up into separate drawcalls
        if ((i % 3 == 0) && (Out + 2 > End))
        {
            Shader->DrawBuffer.EndDrawCall(PolyListSize);
            Shader->VertexBuffer.Advance(PolyListSize);
            
            GouraudInstanceData Tmp;
            memcpy(&Tmp, Shader->InstanceDataBuffer.GetCurrentElementPtr(), sizeof(GouraudInstanceData));
            Shader->InstanceDataBuffer.Advance(1);
            
            Shader->RotateBuffers();

            Out = Shader->VertexBuffer.GetCurrentElementPtr();
            End = Shader->VertexBuffer.GetLastElementPtr();

            memcpy(Shader->InstanceDataBuffer.GetCurrentElementPtr(), &Tmp, sizeof(GouraudInstanceData));

            Shader->DrawBuffer.StartDrawCall();
            PolyListSize = 0;
        }

        Shader->BufferVert(Out++, &Pts[i]);
        PolyListSize++;
    }

    Shader->DrawBuffer.EndDrawCall(PolyListSize);
    Shader->VertexBuffer.Advance(PolyListSize);
    Shader->FinishDrawCall(Info);
    Shader->InstanceDataBuffer.Advance(1);
}
#endif

#if UNREAL_TOURNAMENT_OLDUNREAL
void UFruCoReRenderDevice::DrawGouraudTriangles(const FSceneNode* Frame, const FTextureInfo& Info, FTransTexture* const Pts, INT NumPts, DWORD PolyFlags, DWORD DataFlags, FSpanBuffer* Span)
{
	if (RendererSuspended)
		return;
	
    SetProgram(SHADER_Gouraud);
    auto Shader = dynamic_cast<DrawGouraudProgram*>(Shaders[SHADER_Gouraud]);
    
    INT StartOffset = 0;
    INT i = 0;

    if (Frame->NearClip.W != 0.0)
    {
        Shader->Flush();
        Shader->PushClipPlane(Frame->NearClip);
    }

    for (; i < NumPts; i += 3)
    {
        if (Frame->Mirror == -1.0)
            Exchange(Pts[i+2], Pts[i]);

        // Environment mapping.
        if (PolyFlags & PF_Environment)
        {
            FLOAT UScale = Info.UScale * Info.USize / 256.0f;
            FLOAT VScale = Info.VScale * Info.VSize / 256.0f;

            for (INT j = 0; j < 3; j++)
            {
                FVector T = Pts[i+j].Point.UnsafeNormal().MirrorByVector(Pts[i+j].Normal).TransformVectorBy(Frame->Uncoords);
                Pts[i+j].U = (T.X + 1.0f) * 0.5f * 256.0f * UScale;
                Pts[i+j].V = (T.Y + 1.0f) * 0.5f * 256.0f * VScale;
            }
        }

        // If outcoded, skip it.
        if (Pts[i].Flags & Pts[i + 1].Flags & Pts[i + 2].Flags)
        {
            // stijn: push the triangles we've already processed (if any)
            if (i - StartOffset > 0)
            {
                DrawGouraudPolyList(const_cast<FSceneNode*>(Frame), const_cast<FTextureInfo&>(Info), Pts + StartOffset, i - StartOffset, PolyFlags, nullptr);
                StartOffset = i + 3;
            }
            continue;
        }

        // Backface reject it.
        if ((PolyFlags & PF_TwoSided) && FTriple(Pts[i].Point, Pts[i+1].Point, Pts[i+2].Point) <= 0.0)
        {
            if (!(PolyFlags & PF_TwoSided))
            {
                // stijn: push the triangles we've already processed (if any)
                if (i - StartOffset > 0)
                {
                    DrawGouraudPolyList(const_cast<FSceneNode*>(Frame), const_cast<FTextureInfo&>(Info), Pts + StartOffset, i - StartOffset, PolyFlags, nullptr);
                    StartOffset = i + 3;
                }
                continue;
            }
            Exchange(Pts[i+2], Pts[i]);
        }
    }

    // stijn: push the remaining triangles
    if (i - StartOffset > 0)
        DrawGouraudPolyList(const_cast<FSceneNode*>(Frame), const_cast<FTextureInfo&>(Info), Pts + StartOffset, i - StartOffset, PolyFlags, nullptr);

    if (Frame->NearClip.W != 0.0)
    {
        Shader->Flush();
        Shader->PopClipPlane();
    }
}
#endif

void UFruCoReRenderDevice::DrawGouraudProgram::BufferVert(GouraudVertex* Vert, FTransTexture* P)
{
    Vert->Point       = simd::make_float4(P->Point.X, P->Point.Y, P->Point.Z, 1.f);
    Vert->Normal      = simd::make_float4(P->Normal.X, P->Normal.Y, P->Normal.Z, 1.f);
    Vert->UV          = simd::make_float4(P->U, P->V, 0.f, 0.f);
    Vert->LightColor  = simd::make_float4(P->Light.X, P->Light.Y, P->Light.Z, P->Light.W);
    Vert->FogColor    = simd::make_float4(P->Fog.X, P->Fog.Y, P->Fog.Z, P->Fog.W);
}

void UFruCoReRenderDevice::DrawGouraudProgram::PrepareDrawCall(FSceneNode* Frame, FTextureInfo& Info, DWORD PolyFlags)
{
    BOOL NoNearZ = ((GUglyHackFlags & HACKFLAGS_NoNearZ) == HACKFLAGS_NoNearZ);
    if (!RenDev->DrawingWeapon && NoNearZ)
    {
        RenDev->ClearZ(Frame);
        RenDev->DrawingWeapon = TRUE;
    }
    
    if (!InstanceDataBuffer.CanBuffer(1))
        RotateBuffers();
    
    GouraudInstanceData* Data = InstanceDataBuffer.GetCurrentElementPtr();
    
    LastShaderOptions = OPT_None;
    PolyFlags = RenDev->GetPolyFlagsAndShaderOptions(PolyFlags, LastShaderOptions);

    RenDev->SetTexture(IDX_DiffuseTexture, Info, PolyFlags, 0.f);
    Data->DiffuseInfo = simd::make_float4(RenDev->BoundTextures[IDX_DiffuseTexture]->UMult, RenDev->BoundTextures[IDX_DiffuseTexture]->VMult, 1.f, 1.f);
    
    if (Info.Texture)
    {
        Data->DiffuseInfo[2] = Info.Texture->Diffuse;
        Data->DiffuseInfo[3] = Info.Texture->Alpha;
    }

    if (Info.Texture && Info.Texture->DetailTexture && RenDev->DetailTextures)
    {
#if ENGINE_VERSION==227
		DetailTextureInfo = *Info.Texture->DetailTexture->GetTexture(INDEX_NONE, RenDev);
#else
        Info.Texture->DetailTexture->Lock(DetailTextureInfo, Frame->Viewport->CurrentTime, -1, RenDev);
#endif
		RenDev->SetTexture(IDX_DetailTexture, DetailTextureInfo, PolyFlags, 0.f);
        Data->DetailMacroInfo[0] = RenDev->BoundTextures[IDX_DetailTexture]->UMult;
        Data->DetailMacroInfo[1] = RenDev->BoundTextures[IDX_DetailTexture]->VMult;
        LastShaderOptions |= OPT_DetailTexture;
    }

    if (Info.Texture && Info.Texture->MacroTexture && RenDev->MacroTextures)
    {
#if ENGINE_VERSION==227
		MacroTextureInfo = *Info.Texture->MacroTexture->GetTexture(INDEX_NONE, RenDev);
#else
        Info.Texture->MacroTexture->Lock(MacroTextureInfo, Frame->Viewport->CurrentTime, -1, RenDev);
#endif
		RenDev->SetTexture(IDX_MacroTexture, MacroTextureInfo, PolyFlags, 0.f);
        Data->DetailMacroInfo[2] = RenDev->BoundTextures[IDX_MacroTexture]->UMult;
        Data->DetailMacroInfo[3] = RenDev->BoundTextures[IDX_MacroTexture]->VMult;
        LastShaderOptions |= OPT_MacroTexture;
    }
    
    SelectPipelineState(GetBlendMode(PolyFlags), static_cast<ShaderOptions>(LastShaderOptions));
    RenDev->SetDepthMode(((PolyFlags & PF_Occlude) == PF_Occlude) ? DEPTH_Test_And_Write : DEPTH_Test_No_Write);
}

void UFruCoReRenderDevice::DrawGouraudProgram::FinishDrawCall(FTextureInfo& Info)
{
    GouraudInstanceData* Data = InstanceDataBuffer.GetCurrentElementPtr();
#if ENGINE_VERSION!=227
    if (LastShaderOptions & OPT_DetailTexture)
        Info.Texture->DetailTexture->Unlock(DetailTextureInfo);

    if (LastShaderOptions & OPT_MacroTexture)
        Info.Texture->MacroTexture->Unlock(MacroTextureInfo);
#endif
}

void UFruCoReRenderDevice::DrawGouraudProgram::PushClipPlane(const FPlane &ClipPlane)
{
    // TODO: Implement Me!
}

void UFruCoReRenderDevice::DrawGouraudProgram::PopClipPlane()
{
    // TODO: Implement Me!
}

/*-----------------------------------------------------------------------------
    BuildCommonPipelineStates
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::DrawGouraudProgram::BuildCommonPipelineStates()
{
    SelectPipelineState(BLEND_None, OPT_None);
    SelectPipelineState(BLEND_None, OPT_Modulated);
    SelectPipelineState(BLEND_None, OPT_RenderFog);
}
