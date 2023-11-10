/*=============================================================================
	FruCoRe.cpp: Unreal support code for Apple Metal.
	Copyright 2023 OldUnreal. All Rights Reserved.

	Revision history:
	* Created by Stijn Volckaert
=============================================================================*/

#include "Render.h"
#define GENERATE_METAL_IMPLEMENTATION
#include "FruCoRe.h"

/*-----------------------------------------------------------------------------
	Package Registration
-----------------------------------------------------------------------------*/
IMPLEMENT_PACKAGE(FruCoRe);
IMPLEMENT_CLASS(UFruCoReRenderDevice);

/*-----------------------------------------------------------------------------
    FStringToNSString
-----------------------------------------------------------------------------*/
static NS::String* FStringToNSString(FString& Str)
{
    return NS::String::string(appToAnsi(*Str), NS::ASCIIStringEncoding);
}

/*-----------------------------------------------------------------------------
	Init
-----------------------------------------------------------------------------*/
UBOOL UFruCoReRenderDevice::Init(UViewport* InViewport, INT NewX, INT NewY, INT NewColorBytes, UBOOL Fullscreen)
{
	// Initialize the viewport window
	Viewport = InViewport;
	Viewport->ResizeViewport( Fullscreen ? (BLIT_Fullscreen|BLIT_Metal) : (BLIT_HardwarePaint|BLIT_Metal), NewX, NewY, NewColorBytes );

	debugf(NAME_DevGraphics, TEXT("Frucore: Initializing"));

	// Initialize an SDL Metal Renderer for this Window
	SDL_Window* Window = reinterpret_cast<SDL_Window*>(InViewport->GetWindow());
	Renderer = SDL_CreateRenderer(Window, -1, (UseVSync ? SDL_RENDERER_PRESENTVSYNC : 0) | SDL_RENDERER_ACCELERATED);
	Layer    = reinterpret_cast<CA::MetalLayer*>(SDL_RenderGetMetalLayer(Renderer));
	Device   = reinterpret_cast<MTL::Device*>(Layer->device());
    CommandQueue = Device->newCommandQueue();
	
    if (!Renderer || !Layer || !Device || !CommandQueue)
    {
        debugf(TEXT("Frucore: Failed to create device"));
        return FALSE;
    }
    
	debugf(NAME_DevGraphics, TEXT("Frucore: Created Device"));
    
    CreateDepthTexture();
    
    MTL::DepthStencilDescriptor* DepthStencilDescriptor = MTL::DepthStencilDescriptor::alloc()->init();
    DepthStencilDescriptor->setDepthCompareFunction(MTL::CompareFunction::CompareFunctionLessEqual);
    DepthStencilDescriptor->setDepthWriteEnabled(true);
    DepthStencilStateEnabled = Device->newDepthStencilState(DepthStencilDescriptor);
    DepthStencilDescriptor->setDepthCompareFunction(MTL::CompareFunction::CompareFunctionAlways);
    DepthStencilDescriptor->setDepthWriteEnabled(false);
    DepthStencilStateDisabled = Device->newDepthStencilState(DepthStencilDescriptor);
    DepthStencilDescriptor->release();
    
    // Create uniforms buffer
    GlobalUniformsBuffer.Initialize(1, Device, BufferIndices::Uniforms, BufferIndices::Uniforms);
    GlobalUniformsBuffer.Advance(1);
    
    RegisterTextureFormats();

    InitShaders();
    
    VolumetricLighting = true;
    HighDetailActors = true;
    UseLightmapAtlas = true;
    DetailTextures = true;
    MacroTextures = true;
    SupportsFogMaps = true;
    Coronas = true;
    SupportsTC = true;

	// Great success
	return TRUE;
}

/*-----------------------------------------------------------------------------
    InitShaders
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::InitShaders()
{
    Shaders[Tile_Prog] = new DrawTileProgram(this, TEXT("DrawTile"), "DrawTileVertex", "DrawTileFragment");
    Shaders[Complex_Prog] = new DrawComplexProgram(this, TEXT("DrawComplex"), "DrawComplexVertex", "DrawComplexFragment");
    Shaders[Gouraud_Prog] = new DrawGouraudProgram(this, TEXT("DrawGouraud"), "DrawGouraudVertex", "DrawGouraudFragment");
    Shaders[Simple_Triangle_Prog] = new DrawSimpleTriangleProgram(this, TEXT("DrawSimpleTriangle"), "DrawSimpleTriangleVertex", "DrawSimpleTriangleFragment");
    
    for (auto Shader : Shaders)
    {
        if (!Shader)
            continue;
        
        Shader->BuildPipeline();        
    }
}

/*-----------------------------------------------------------------------------
	SetRes
-----------------------------------------------------------------------------*/
UBOOL UFruCoReRenderDevice::SetRes(INT NewX, INT NewY, INT NewColorBytes, UBOOL Fullscreen)
{ 
	Viewport->ResizeViewport( Fullscreen ? (BLIT_Fullscreen|BLIT_Metal) : (BLIT_HardwarePaint|BLIT_Metal), NewX, NewY, NewColorBytes );	
	return TRUE; 
}

/*-----------------------------------------------------------------------------
	Exit
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::Exit()
{
    if (CommandQueue)
        CommandQueue->release();
    if (Device)
        Device->release();
	SDL_DestroyRenderer(Renderer);
}

/*-----------------------------------------------------------------------------
	Flush
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::Flush(INT AllowPrecache)
{
    for (auto It = TMap<QWORD, CachedTexture>::TIterator(BindMap); It; ++It)
    {
        auto Tex = It.Value();
        if (Tex.Texture)
        {
            Tex.Texture->release();
            Tex.Texture = nullptr;
        }
    }
    BindMap.Empty();
}

/*-----------------------------------------------------------------------------
	Package Registration
-----------------------------------------------------------------------------*/
UBOOL UFruCoReRenderDevice::Exec(const TCHAR* Cmd, FOutputDevice& Ar)
{
	if( URenderDevice::Exec( Cmd, Ar ) )
		return TRUE;
	return FALSE;
}

/*-----------------------------------------------------------------------------
	Lock
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::Lock(FPlane _FlashScale, FPlane _FlashFog, FPlane ScreenClear, DWORD RenderLockFlags, BYTE* HitData, INT* HitSize)
{
    SetDepthTesting(true);
    DrawingWeapon = false;
    FlashScale = _FlashScale;
    FlashFog = _FlashFog;
    Drawable = Layer->nextDrawable();
    CommandBuffer = CommandQueue->commandBuffer();
    
    CreateCommandEncoder(CommandBuffer);
    
    memset(BoundTextures, 0, sizeof(BoundTextures));
}

/*-----------------------------------------------------------------------------
    Unlock
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::Unlock(UBOOL Blit)
{
    SetProgram(No_Prog);
    
    CommandEncoder->endEncoding();
    if (Blit)
        CommandBuffer->presentDrawable(Drawable);
    CommandBuffer->commit();
    
    CommandEncoder->release();
    PassDescriptor->release();
    CommandBuffer->release();
    CommandEncoder = nullptr;
}

/*-----------------------------------------------------------------------------
	ClearZ
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::ClearZ(FSceneNode* Frame)
{
    INT OldProgram = ActiveProgram;
    SetProgram(No_Prog);
    
    // This is kind of annoying. We can't simply switch to a different 
    // DepthStencilState. Instead, we need to create a new command encoder
    // and have it clear the depth attachment
    CommandEncoder->endEncoding();
    CommandEncoder->release();
    CreateCommandEncoder(CommandBuffer, true, false);
    SetProgram(OldProgram);
}

/*-----------------------------------------------------------------------------
	GetStats
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::GetStats(TCHAR* Result)
{
}

/*-----------------------------------------------------------------------------
	ReadPixels
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::ReadPixels(FColor* Pixels)
{
    INT Width, Height;
    auto Window = reinterpret_cast<SDL_Window*>(Viewport->GetWindow());
    SDL_GetWindowSizeInPixels(Window, &Width, &Height);
    SDL_RenderReadPixels(Renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, Pixels, Width * 4);
}

/*-----------------------------------------------------------------------------
    DrawStats
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::DrawStats(FSceneNode* Frame)
{
}

/*-----------------------------------------------------------------------------
    SetSceneNode -
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::SetSceneNode(FSceneNode* Frame)
{
    if (StoredFovAngle != Frame->Viewport->Actor->FovAngle ||
        StoredFX != Frame->FX ||
        StoredFY != Frame->FY ||
        StoredOriginX != Frame->XB ||
        StoredOriginY != Frame->YB ||
        StoredBrightness != Frame->Viewport->GetOuterUClient()->Brightness)
        SetProjection(Frame, 0);
}

/*-----------------------------------------------------------------------------
    PrecacheTexture
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::PrecacheTexture(FTextureInfo& Info, DWORD PolyFlags)
{
}

/*-----------------------------------------------------------------------------
	SupportsTextureFormat
-----------------------------------------------------------------------------*/
UBOOL UFruCoReRenderDevice::SupportsTextureFormat(ETextureFormat Format)
{
    switch( Format )
    {
        case TEXF_P8:
            return true;
        case TEXF_BC1:
        case TEXF_BC2:
        case TEXF_BC3:
        case TEXF_BC4:
        case TEXF_BC5:
        case TEXF_BC6H:
        case TEXF_BC7:
            return SupportsTC;
        default:
            return false;
    }
}

/*-----------------------------------------------------------------------------
	PrintNSError
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::PrintNSError(const TCHAR* Prefix, const NS::Error* Error)
{
	debugf(NAME_DevGraphics, TEXT("Frucore: %ls: %ls"), Prefix,
           Error ? appFromAnsi( Error->localizedDescription()->cString(NS::StringEncoding::ASCIIStringEncoding)) : TEXT("Unknown Error"));
}

/*-----------------------------------------------------------------------------
    SetProgram
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::SetProgram(INT Program)
{
    if (Program != ActiveProgram)
    {
        if (Shaders[ActiveProgram])
            Shaders[ActiveProgram]->DeactivateShader();
        ActiveProgram = Program;
        if (Shaders[ActiveProgram])
            Shaders[ActiveProgram]->ActivateShader();
    }
}

/*-----------------------------------------------------------------------------
    SetProgram
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::SetProjection(FSceneNode *Frame, UBOOL bNearZ)
{
    // If we're doing this in the middle of a frame, we need to switch to a 
    // different buffer. This way, all of our in-flight draw calls will still
    // use the old projection matrix and uniforms
    INT OldProgram = ActiveProgram;
    if (CommandEncoder)
    {
        SetProgram(No_Prog);
        GlobalUniformsBuffer.Rotate(Device);
        GlobalUniformsBuffer.Advance(1);
    }
    
#if UNREAL_TOURNAMENT_OLDUNREAL
    zNear = 0.5f;
#else
    zNear = bNearZ ? 0.7f : 1.0f;
#endif

    zFar = (GIsEditor && Frame->Viewport->Actor->RendMap == REN_Wire) ? 131072.f : 65336.f;

    StoredFovAngle = Viewport->Actor->FovAngle;
    StoredFX = Frame->FX;
    StoredFY = Frame->FY;
    StoredOriginX = Frame->XB;
    StoredOriginY = Frame->YB;
    StoredBrightness = Frame->Viewport->GetOuterUClient()->Brightness;
    
    auto GlobalUniforms = GlobalUniformsBuffer.GetElementPtr(0);

    FLOAT InvAspect = Frame->FY / Frame->FX;
    FLOAT FovTan = appTan(Viewport->Actor->FovAngle * PI / 360.f);
    FLOAT InvFovTan = 1.f / FovTan;
    RFX2 = 2.0 * FovTan / Frame->FX;
    RFY2 = 2.0 * FovTan * InvAspect / Frame->FY;
    
    GlobalUniforms->ProjectionMatrix = simd_matrix_from_rows(
        (simd::float4){ 2 * InvFovTan * InvAspect, 0.f, 0.f, 0.f },
        // The Metal coordinate system is left-handed so we flip the Y axis here!
        (simd::float4){ 0.f, -2 * InvFovTan, 0.f, 0.f },
        (simd::float4){ 0.f, 0.f, zFar / (zFar - zNear), -zFar * zNear / (zFar - zNear) },
        (simd::float4){ 0.f, 0.f, 1.f, 0.f }
    );
    
    GlobalUniforms->ViewportWidth = Frame->FX;
    GlobalUniforms->ViewportHeight = Frame->FY;
    GlobalUniforms->ViewportOriginX = Frame->XB;
    GlobalUniforms->ViewportOriginY = Frame->YB;
    GlobalUniforms->zNear = zNear;
    GlobalUniforms->zFar = zFar;
    GlobalUniforms->Gamma = StoredBrightness;
    GlobalUniforms->DetailMax = 2;
    
    if (CommandEncoder)
    {
        CommandEncoder->endEncoding();
        CreateCommandEncoder(CommandBuffer, false, false);
        SetProgram(OldProgram);
    }
        
    // Push to the GPU
    GlobalUniformsBuffer.BufferData(true);
    
    debugf(TEXT("Frucore: Set projection matrix"));
    
    if (!DepthTexture)
        CreateDepthTexture();
}

/*-----------------------------------------------------------------------------
    CreateDepthTexture
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::CreateDepthTexture()
{
    // We need to manually create a depth texture in Metal
    if (DepthTexture)
    {
        DepthTexture->release();
        DepthTexture = nullptr;
    }
    
    auto DrawableSize = Layer->drawableSize();
    MTL::TextureDescriptor* DepthTextureDescriptor = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatDepth32Float, DrawableSize.width, DrawableSize.height, false);
    
    // Store on the GPU. We're not going to do anything fancy with this on the CPU
    DepthTextureDescriptor->setStorageMode(MTL::StorageModePrivate);
    DepthTextureDescriptor->setUsage(MTL::TextureUsageRenderTarget);
    DepthTexture = Device->newTexture(DepthTextureDescriptor);
    DepthTexture->setLabel(NS::String::string("DepthStencil", NS::UTF8StringEncoding));
    DepthTextureDescriptor->release();
    
    debugf(TEXT("Frucore: Created depth texture"));
}

/*-----------------------------------------------------------------------------
    CreateCommandEncoder -
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::CreateCommandEncoder(MTL::CommandBuffer *Buffer, bool ClearDepthBuffer, bool ClearColorBuffer)
{
    PassDescriptor = MTL::RenderPassDescriptor::renderPassDescriptor();
    
    auto ColorAttachment = PassDescriptor->colorAttachments()->object(0);
    if (ClearColorBuffer)
    {
        ColorAttachment->setLoadAction(MTL::LoadAction::LoadActionClear);
        ColorAttachment->setStoreAction(MTL::StoreAction::StoreActionStore);
    }
    else
    {
        ColorAttachment->setLoadAction(MTL::LoadAction::LoadActionLoad);
        ColorAttachment->setStoreAction(MTL::StoreAction::StoreActionStore);
    }
    ColorAttachment->setTexture(Drawable->texture());
    
    auto DepthAttachment = PassDescriptor->depthAttachment();
    if (ClearDepthBuffer)
    {
        DepthAttachment->setLoadAction(MTL::LoadAction::LoadActionClear);
        DepthAttachment->setStoreAction(MTL::StoreAction::StoreActionDontCare);
    }
    else
    {
        DepthAttachment->setLoadAction(MTL::LoadAction::LoadActionLoad);
        DepthAttachment->setStoreAction(MTL::StoreAction::StoreActionStore);
    }
    DepthAttachment->setTexture(DepthTexture);
    DepthAttachment->setClearDepth(1);
    
    /*
    auto StencilAttachment = PassDescriptor->stencilAttachment();
    StencilAttachment->setLoadAction(MTL::LoadAction::LoadActionClear);
    StencilAttachment->setStoreAction(MTL::StoreAction::StoreActionDontCare);
    StencilAttachment->setTexture(DepthTexture);
    */
    
    CommandEncoder = CommandBuffer->renderCommandEncoder(PassDescriptor);
    CommandEncoder->setDepthStencilState(DepthTestingEnabled ? DepthStencilStateEnabled : DepthStencilStateDisabled);
    CommandEncoder->setCullMode(MTL::CullModeBack);
    CommandEncoder->setFrontFacingWinding(MTL::Winding::WindingClockwise);
    
    MTL::Viewport MetalViewport;
    MetalViewport.originX = StoredOriginX;
    MetalViewport.originY = StoredOriginY;
    MetalViewport.zfar = 1.0;
    MetalViewport.znear = 0.0;
    MetalViewport.width = StoredFX;
    MetalViewport.height = StoredFY;
    
    CommandEncoder->setViewport(MetalViewport);

    GlobalUniformsBuffer.BindBuffer(CommandEncoder);
}

/*-----------------------------------------------------------------------------
    SetDepthTesting
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::SetDepthTesting(UBOOL Enabled)
{
    if (Enabled != DepthTestingEnabled)
    {
        DepthTestingEnabled = Enabled;
        if (CommandEncoder)
            CommandEncoder->setDepthStencilState(Enabled ? DepthStencilStateEnabled : DepthStencilStateDisabled);
    }
}

/*-----------------------------------------------------------------------------
    SetDepthTesting
-----------------------------------------------------------------------------*/
MTL::Library* UFruCoReRenderDevice::GetShaderLibrary()
{
    NS::Error* Error = nullptr;
    MTL::Library* Library = nullptr;
#if FRUCORE_SHADER_DEFAULT_LIBRARY
    Library = Device->newDefaultLibrary();
#else
    Library = Device->newLibrary(NS::String::string("FruCoReShaders.metallib", NS::UTF8StringEncoding), &Error);
#endif
    
    if (!Library)
    {
        PrintNSError(TEXT("Error creating shader library"), Error);
        return nullptr;
    }
    
    return Library;
}

/*-----------------------------------------------------------------------------
    BuildPipelineStates - builds a pipeline state for each blending mode
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::ShaderProgram::BuildPipelineStates
(
    const TCHAR* Label,
    MTL::Function *VertexShader,
    MTL::Function *FragmentShader
)
{
    auto PipelineDescriptor = MTL::RenderPipelineDescriptor::alloc()->init();
    PipelineDescriptor->setVertexFunction(VertexShader);
    PipelineDescriptor->setFragmentFunction(FragmentShader);
    PipelineDescriptor->setDepthAttachmentPixelFormat(MTL::PixelFormat::PixelFormatDepth32Float);
    //PipelineDescriptor->setStencilAttachmentPixelFormat(MTL::PixelFormat::PixelFormatDepth32Float_Stencil8);
    
    auto ColorAttachment = PipelineDescriptor->colorAttachments()->object(0);
    ColorAttachment->setPixelFormat(MTL::PixelFormat::PixelFormatBGRA8Unorm);
    
    struct BlendState
    {
        BlendModes BlendMode;
        const TCHAR* Name;
        bool BlendingEnabled;
        MTL::BlendOperation BlendOperation;
        MTL::BlendFactor SourceFactor;
        MTL::BlendFactor DestinationFactor;
    };
    
    const BlendState BlendStates[BLEND_Max] = {
        { BLEND_None, TEXT("NoBlending"), false, MTL::BlendOperationAdd, MTL::BlendFactorZero, MTL::BlendFactorOne },
        { BLEND_Invisible, TEXT("Invisible"), true, MTL::BlendOperationAdd, MTL::BlendFactorZero, MTL::BlendFactorZero },
        { BLEND_Modulated, TEXT("Modulated"), true, MTL::BlendOperationAdd, MTL::BlendFactorDestinationColor, MTL::BlendFactorSourceColor },
        { BLEND_Translucent, TEXT("Translucent"), true, MTL::BlendOperationAdd, MTL::BlendFactorOne, MTL::BlendFactorOneMinusSourceColor },
        { BLEND_Masked, TEXT("Masked"), true, MTL::BlendOperationAdd, MTL::BlendFactorOne, MTL::BlendFactorOneMinusSourceAlpha },
        { BLEND_StraightAlpha, TEXT("StraightAlpha"), true, MTL::BlendOperationAdd, MTL::BlendFactorSourceAlpha, MTL::BlendFactorOneMinusSourceAlpha },
        { BLEND_PremultipliedAlpha, TEXT("PremultipliedAlpha"), true, MTL::BlendOperationAdd, MTL::BlendFactorOne, MTL::BlendFactorOneMinusSourceAlpha },
    };
    
    for (INT i = 0; i < BLEND_Max; ++i)
    {
        ColorAttachment->setBlendingEnabled(BlendStates[i].BlendingEnabled);
        ColorAttachment->setRgbBlendOperation(BlendStates[i].BlendOperation);
        ColorAttachment->setSourceRGBBlendFactor(BlendStates[i].SourceFactor);
        ColorAttachment->setDestinationRGBBlendFactor(BlendStates[i].DestinationFactor);
        ColorAttachment->setAlphaBlendOperation(BlendStates[i].BlendOperation);
        ColorAttachment->setSourceAlphaBlendFactor(BlendStates[i].SourceFactor);
        ColorAttachment->setDestinationAlphaBlendFactor(BlendStates[i].DestinationFactor);
        
        FString PipelineLabel = FString::Printf(TEXT("%ls%ls"), Label, BlendStates[i].Name);
        NS::String* NSLabel = FStringToNSString(PipelineLabel);
        PipelineDescriptor->setLabel(NSLabel);
        
        NS::Error* Error = nullptr;
        PipelineStates[BlendStates[i].BlendMode] = RenDev->Device->newRenderPipelineState( PipelineDescriptor, &Error );
        if (!PipelineStates[BlendStates[i].BlendMode])
        {
            PrintNSError(TEXT("Error creating pipeline states"), Error);
            return;
        }
    }
}

/*-----------------------------------------------------------------------------
    SetBlendMode
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::SetBlendMode(DWORD PolyFlags)
{
    auto BlendMode = BLEND_None;
    if (PolyFlags & PF_Invisible)
        BlendMode = BLEND_Invisible;
    else if (PolyFlags & PF_Translucent)
        BlendMode = BLEND_Translucent;
    else if (PolyFlags & PF_Modulated)
        BlendMode = BLEND_Modulated;
    else if (PolyFlags & PF_AlphaBlend)
        BlendMode = BLEND_StraightAlpha;
    else if (PolyFlags & PF_Highlighted)
        BlendMode = BLEND_PremultipliedAlpha;
    else if (PolyFlags & PF_Masked)
        BlendMode = BLEND_Masked;
    
    const auto ActiveShader = Shaders[ActiveProgram];
    if (ActiveShader->ActivePipelineState != ActiveShader->PipelineStates[BlendMode])
    {
        ActiveShader->Flush();
        CommandEncoder->setRenderPipelineState(ActiveShader->PipelineStates[BlendMode]);
        ActiveShader->ActivePipelineState = ActiveShader->PipelineStates[BlendMode];
    }
}
