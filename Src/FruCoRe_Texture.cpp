/*=============================================================================
    FruCoRe_Texture.cpp: Unreal support code for Apple Metal.
    Copyright 2023 OldUnreal. All Rights Reserved.

    Revision history:
    * Created by Stijn Volckaert
=============================================================================*/

#include "FruCoRe.h"

/*-----------------------------------------------------------------------------
    P8ToRGBA8 - P8 is not a format GPUs support natively, so we convert all P8
    textures to RGBA8 before uploading them to the GPU.
 
    This function needs to handle Unreal Engine's masked rendering feature.
    When rendering a polygon that has the PF_Masked polyflag set, all texels
    with palette index 0 should be fully transparent. We handle this by 
    changing the palette color at index 0 to #00000000.
 
    Keep in mind that this means we might have to keep two copies of each P8
    texture around: one with a masked out palette[0] color (suitable for
    PF_Masked rendering) and one with the original palette[0] color (for all
    other polyflags).
-----------------------------------------------------------------------------*/
BYTE* P8ToRGBA8(FTextureInfo& Info, DWORD PolyFlags, INT MipLevel)
{
    FColor  LocalPal[256];
    FColor* Palette = Info.Palette;

    if (PolyFlags & PF_Masked)
    {
        appMemcpy(LocalPal, Info.Palette, 256 * sizeof(FColor));
        LocalPal[0] = FColor(0, 0, 0, 0);
        Palette = LocalPal;
    }
    
    auto Mip = Info.Mips[MipLevel];
    auto Count = Mip->USize * Mip->VSize;
    auto Ptr = new DWORD[Count];
    
    Info.Load();
    for (auto i = 0; i < Count; i++)
    {
        *Ptr++ = GET_COLOR_DWORD(Palette[Mip->DataPtr[i]]);
    }
    
    return reinterpret_cast<BYTE*>(Ptr - Count);
}

/*-----------------------------------------------------------------------------
    RegisterTextureFormats - Note: I have not reviewed any of the block sizes!
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::RegisterTextureFormats()
{
    TextureFormats.Set(TEXF_P8      , {MTL::PixelFormatRGBA8Unorm   , 4, &P8ToRGBA8 });
    TextureFormats.Set(TEXF_RGBA8_  , {MTL::PixelFormatRGBA8Unorm   , 4, nullptr    });
    TextureFormats.Set(TEXF_BGRA8   , {MTL::PixelFormatRGBA8Unorm   , 4, nullptr    });
    TextureFormats.Set(TEXF_BGRA8_LM, {MTL::PixelFormatRGBA8Unorm   , 4, nullptr    });
    TextureFormats.Set(TEXF_BC1     , {MTL::PixelFormatBC1_RGBA     , 2, nullptr    });
    TextureFormats.Set(TEXF_BC2     , {MTL::PixelFormatBC2_RGBA     , 4, nullptr    });
    TextureFormats.Set(TEXF_BC3     , {MTL::PixelFormatBC3_RGBA     , 4, nullptr    });
    TextureFormats.Set(TEXF_BC4     , {MTL::PixelFormatBC4_RUnorm   , 4, nullptr    });
    TextureFormats.Set(TEXF_BC5     , {MTL::PixelFormatBC5_RGUnorm  , 4, nullptr    });
    TextureFormats.Set(TEXF_BC6H    , {MTL::PixelFormatBC6H_RGBFloat, 4, nullptr    });
    TextureFormats.Set(TEXF_BC7     , {MTL::PixelFormatBC7_RGBAUnorm, 4, nullptr    });
}

/*-----------------------------------------------------------------------------
    FixCacheID - Masked P8 textures have the alpha byte of their Palette[0]
    color set to 0, but non-masked textures have non-zero alpha bytes for this
    palette color. This means we potentially need two copies of each P8
    texture: one with a masked Palette[0].A and one with the original
    Palette[0].A. FixCacheID ensures that these two copies have different
    cache IDs.
-----------------------------------------------------------------------------*/
#define MASKED_TEXTURE_TAG 4
static void FixCacheID(FTextureInfo& Info, DWORD PolyFlags)
{
    if ((PolyFlags & PF_Masked) && Info.Format == TEXF_P8)
    {
        // We're writing this tag into unused CacheID bits
        Info.CacheID |= MASKED_TEXTURE_TAG;
    }
}

/*-----------------------------------------------------------------------------
    SetTexture
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::SetTexture(INT TexNum, FTextureInfo &Info, DWORD PolyFlags, FLOAT PanBias)
{
    FixCacheID(Info, PolyFlags);
    
    CachedTexture* Texture = BindMap.Find(Info.CacheID);

#if UNREAL_TOURNAMENT_OLDUNREAL
    if (Texture && !Info.NeedsRealtimeUpdate(Texture->RealTimeChangeCount))
//#elif ENGINE_VERSION==227
//	if (Texture && (!Info.bRealtimeChanged || Info.RenderTag != Texture->RealTimeChangeCount))
#else
	if (Texture && !Info.bRealtimeChanged)
#endif
    {
        if (BoundTextures[TexNum] != Texture)
            Shaders[ActiveProgram]->Flush();
    }
    else
    {
        Shaders[ActiveProgram]->Flush();
		
#if !UNREAL_TOURNAMENT_OLDUNREAL
		Info.bRealtimeChanged = false;
#endif
        
        MTL::Texture* MetalTexture = Texture ? Texture->Texture : nullptr;
        
        BOOL bShouldDeleteTextureData = FALSE;
        BYTE* TextureData = nullptr;
        
        // Look up the texture format
        auto TextureFormat = TextureFormats.Find(Info.Format);
        if (!TextureFormat)
        {
            debugf(TEXT("Frucore: Unsupported texture format: %d (%ls)"), Info.Format, *FTextureFormatString(Info.Format));
            
            // Generate checkerboard texture (code copied from XOpenGLDrv)
            DWORD PaletteBM[16] =
            {
                0x00000000u, 0x000000FFu, 0x0000FF00u, 0x0000FFFFu,
                0x00FF0000u, 0x00FF00FFu, 0x00FFFF00u, 0x00FFFFFFu,
                0xFF000000u, 0xFF0000FFu, 0xFF00FF00u, 0xFF00FFFFu,
                0xFFFF0000u, 0xFFFF00FFu, 0xFFFFFF00u, 0xFFFFFFFFu,
            };
            TextureData = new BYTE[256 * 256 * 4];
            DWORD* Ptr = reinterpret_cast<DWORD*>(TextureData);
            for (INT i = 0; i < (256 * 256); i++)
                *Ptr++ = PaletteBM[(i / 16 + i / (256 * 16)) % 16];
            bShouldDeleteTextureData = TRUE;
        }
        
        // Allocate a new texture
        if (!Texture)
        {
            MTL::TextureDescriptor* TextureDescriptor = MTL::TextureDescriptor::alloc()->init();
            TextureDescriptor->setWidth( Info.USize );
            TextureDescriptor->setHeight( Info.VSize );
            TextureDescriptor->setTextureType( MTL::TextureType2D );
            TextureDescriptor->setStorageMode( MTL::StorageModeShared );
            TextureDescriptor->setUsage( MTL::ResourceUsageSample | MTL::ResourceUsageRead );
            TextureDescriptor->setPixelFormat(TextureFormat ? TextureFormat->MetalFormat : MTL::PixelFormatRGBA8Unorm);
            TextureDescriptor->setMipmapLevelCount( Info.NumMips );
            
            MetalTexture = Device->newTexture(TextureDescriptor);
            TextureDescriptor->release();
        }
        
        Info.Load();
        
        for (INT MipLevel = 0; MipLevel < Info.NumMips; ++MipLevel)
        {
            auto VSize = Info.Mips[MipLevel]->VSize;
            auto USize = Info.Mips[MipLevel]->USize;
            
            // Move the texture data into the Metal texture backing store
            if (TextureFormat && TextureFormat->ConversionFunction)
            {
                TextureData = TextureFormat->ConversionFunction(Info, PolyFlags, MipLevel);
                bShouldDeleteTextureData = TRUE;
            }
            else if (TextureFormat)
            {
                TextureData = Info.Mips[MipLevel]->DataPtr;
            }
            
            MetalTexture->replaceRegion(MTL::Region(0, 0, 0, USize, VSize, 1), MipLevel, TextureData, USize * TextureFormat->BlockSize);
        }
        
        Texture = &BindMap.Set(Info.CacheID,
							   {Info.CacheID,
								MetalTexture,
#if UNREAL_TOURNAMENT_OLDUNREAL
								Info.Texture ? Info.Texture->RealtimeChangeCount : 0,
#elif ENGINE_VERSION==227
								static_cast<INT>(Info.RenderTag),
#else
								0,
#endif
								0.f,
								0.f,
								0.f,
								0.f}
			);

#if ENGINE_VERSION==227
		Texture->RealTimeChangeCount = Info.RenderTag;
#endif
		
        if (bShouldDeleteTextureData)
            delete[] TextureData;
    }
    
    if (BoundTextures[TexNum] != Texture)
    {
        CommandEncoder->setFragmentTexture(Texture->Texture, TexNum);
        BoundTextures[TexNum] = Texture;
    }
    
    // recalculate texture params
    Texture->UPan  = Info.Pan.X + PanBias * Info.UScale;
    Texture->VPan  = Info.Pan.Y + PanBias * Info.VScale;
    Texture->UMult = 1.f / (Info.UScale * static_cast<FLOAT>(Info.USize));
    Texture->VMult = 1.f / (Info.VScale * static_cast<FLOAT>(Info.VSize));
}
