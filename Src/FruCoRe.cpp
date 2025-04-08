/*=============================================================================
	FruCoRe.cpp: Unreal support code for Apple Metal.
	Copyright 2023 OldUnreal. All Rights Reserved.

	Revision history:
	* Created by Stijn Volckaert
=============================================================================*/

#include "Render.h"
#define GENERATE_METAL_IMPLEMENTATION
#include "FruCoRe.h"
#include "FruCoRe_Helpers.h"

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
    StaticConstructor
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::StaticConstructor()
{
    // Register renderer options
    new(GetClass(),TEXT("UseVSync"), RF_Public)UBoolProperty(CPP_PROPERTY(UseVSync), TEXT("Options"), CPF_Config);
    new(GetClass(),TEXT("UseAA"), RF_Public)UBoolProperty(CPP_PROPERTY(UseAA), TEXT("Options"), CPF_Config);
    new(GetClass(),TEXT("MacroTextures"), RF_Public)UBoolProperty(CPP_PROPERTY(MacroTextures), TEXT("Options"), CPF_Config );
    new(GetClass(),TEXT("OneXBlending"), RF_Public)UBoolProperty(CPP_PROPERTY(OneXBlending), TEXT("Options"), CPF_Config );
    new(GetClass(),TEXT("ActorXBlending"), RF_Public)UBoolProperty(CPP_PROPERTY(ActorXBlending), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("UseGammaCorrection"), RF_Public)UBoolProperty(CPP_PROPERTY(UseGammaCorrection), TEXT("Options"), CPF_Config );
    new(GetClass(),TEXT("NumAASamples"), RF_Public)UIntProperty(CPP_PROPERTY(NumAASamples), TEXT("Options"), CPF_Config );
    new(GetClass(),TEXT("LODBias"), RF_Public)UFloatProperty(CPP_PROPERTY(LODBias), TEXT("Options"), CPF_Config );
    new(GetClass(),TEXT("GammaOffset"), RF_Public)UFloatProperty(CPP_PROPERTY(GammaOffset), TEXT("Options"), CPF_Config );

	UEnum* FramebufferBpcEnum = new(GetClass(), TEXT("FramebufferBpc")) UEnum(nullptr);
	new(FramebufferBpcEnum->Names) FName(TEXT("8bpc"));
	new(FramebufferBpcEnum->Names) FName(TEXT("10bpc"));
	new(FramebufferBpcEnum->Names) FName(TEXT("16bpc"));
	new(GetClass(), TEXT("FramebufferBpc"), RF_Public)UByteProperty(CPP_PROPERTY(FramebufferBpc), TEXT("Options"), CPF_Config, FramebufferBpcEnum);
    
    // Generic RenderDevice settings
    VolumetricLighting = true;
    HighDetailActors = true;
    DetailTextures = true;
    SupportsFogMaps = true;
    Coronas = true;
    SupportsTC = true;
	ShinySurfaces = true;
    
    // 469-specific RenderDevice settings
#if UNREAL_TOURNAMENT_OLDUNREAL
    HighDetailActors = true;
    UseLightmapAtlas = false;
    MaxTextureSize = 2048;
#endif
    
    // Frucore-specific settings
    UseVSync = false;
    UseAA = false;
    MacroTextures = true;
    OneXBlending = false;
	ActorXBlending = true;
	UseGammaCorrection = true;
    LODBias = 0.f;
    GammaOffset = 0.f;
    NumAASamples = 4;
	FramebufferBpc = FB_BPC_10bit; 
}

/*-----------------------------------------------------------------------------
    ShutdownAfterError
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::ShutdownAfterError()
{
}

/*-----------------------------------------------------------------------------
    PostEditChange
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::PostEditChange()
{
    UniformsChanged = TRUE;
    SetMSAAOptions();
    if (UseAA && !MultisampleTexture)
        CreateMultisampleRenderTargets();
    if (Layer)
        SetMetalVSync(Layer, UseVSync);
}

/*-----------------------------------------------------------------------------
    SetMSAAOptions - Sanitizes MSAA settings and precaches MSAA shader options
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::SetMSAAOptions()
{
    DWORD NewOptions = 0;
    
    if (!UseAA)
    {
        CachedMSAAOptions = OPT_None;
        return;
    }
 
    const auto OldAASamples = NumAASamples;
    if (NumAASamples >= 8 && Device->supportsTextureSampleCount(8))
    {
        NumAASamples = 8;
        NewOptions = OPT_MSAAx8;
    }
    else if (NumAASamples >= 4 && Device->supportsTextureSampleCount(4))
    {
        NumAASamples = 4;
        NewOptions = OPT_MSAAx4;
    }
    else if (Device->supportsTextureSampleCount(2))
    {
        NumAASamples = 2;
        NewOptions = OPT_MSAAx2;
    }
    else
    {
        NumAASamples = 1;
        NewOptions = OPT_NoMSAA;
    }
    
    if (OldAASamples != NumAASamples)
    {
        debugf(TEXT("Frucore: NumAASamples was %d but this device only supports %dx MSAA"), OldAASamples, NumAASamples);
    }
    
    if (NewOptions != CachedMSAAOptions)
    {
        CachedMSAAOptions = NewOptions;
        MSAASettingsChanged = true;
        ActivePipelineState = nullptr;
    }
}

/*-----------------------------------------------------------------------------
    BuildPostprocessPipelineState
-----------------------------------------------------------------------------*/
MTL::RenderPipelineState* UFruCoReRenderDevice::BuildPostprocessPipelineState(const char *VertexFunctionName, const char *FragmentFunctionName, const char *StateName)
{
    MTL::Library* Library = GetShaderLibrary();
    check(Library && "Could not create shader library");
    
    MTL::Function* VertexShader = Library->newFunction(NS::String::string(VertexFunctionName, NS::UTF8StringEncoding));
    MTL::Function* FragmentShader = Library->newFunction(NS::String::string(FragmentFunctionName, NS::UTF8StringEncoding));
    
    auto PipelineDescriptor = MTL::RenderPipelineDescriptor::alloc()->init();
    PipelineDescriptor->setVertexFunction(VertexShader);
    PipelineDescriptor->setFragmentFunction(FragmentShader);
    
    auto ColorAttachment = PipelineDescriptor->colorAttachments()->object(0);
    ColorAttachment->setPixelFormat(FrameBufferPixelFormat);
    ColorAttachment->setBlendingEnabled(false);
    PipelineDescriptor->setLabel(NS::String::string(StateName, NS::UTF8StringEncoding));
        
    NS::Error* Error = nullptr;
    auto State = Device->newRenderPipelineState( PipelineDescriptor, &Error );
    if (!State)
    {
        PrintNSError(TEXT("Error creating postprocess pipeline state"), Error);
        return nullptr;
    }
    
    PipelineDescriptor->release();
    VertexShader->release();
    FragmentShader->release();
    
    return State;
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
    SDL_MetalView View = SDL_Metal_CreateView(Window);
    Layer    = reinterpret_cast<CA::MetalLayer*>(SDL_Metal_GetLayer(View));
//	Device   = reinterpret_cast<MTL::Device*>(Layer->device());
    if (Layer)
    {
        Device  = MTL::CreateSystemDefaultDevice();
        Layer->setDevice(Device);
        SetMetalVSync(Layer, UseVSync);
    }
    if (Device)
    {
        CommandQueue = Device->newCommandQueue();
        
        SDL_PixelFormat* SDLPixelFormat = nullptr;
        if (FramebufferBpc == FB_BPC_10bit && Device->supportsFeatureSet(MTL::FeatureSet_macOS_GPUFamily1_v1))
        {
            FrameBufferPixelFormat = MTL::PixelFormatRGB10A2Unorm;
			debugf(TEXT("Frucore: Using RGB10A2 frame buffer"));
        }
        else if (FramebufferBpc == FB_BPC_16bit)
        {
            FrameBufferPixelFormat = MTL::PixelFormatRGBA16Float;
			debugf(TEXT("Frucore: Using RGBA16Float frame buffer"));
        }
		else
		{
			FrameBufferPixelFormat = MTL::PixelFormatBGRA8Unorm;
			debugf(TEXT("Frucore: Using BGRA8 frame buffer"));
		}
        Layer->setPixelFormat(FrameBufferPixelFormat);
        Layer->setFramebufferOnly(true);
    }
    if (!Layer || !Device || !CommandQueue)
    {
        debugf(TEXT("Frucore: Failed to create device"));
        return FALSE;
    }

	debugf(NAME_DevGraphics, TEXT("Frucore: Created Device"));
    
    SetMSAAOptions();
    MSAAComposePipelineState = BuildPostprocessPipelineState("MSAAComposeVertex", "MSAAComposeFragment", "MSAA Compose");
    GammaCorrectPipelineState = BuildPostprocessPipelineState("GammaCorrectVertex", "GammaCorrectFragment", "GammaCorrect");
    
    CreateRenderTargets();
    if (UseAA)
        CreateMultisampleRenderTargets();
    
    MTL::DepthStencilDescriptor* DepthStencilDescriptor = MTL::DepthStencilDescriptor::alloc()->init();
    DepthStencilDescriptor->setDepthCompareFunction(MTL::CompareFunction::CompareFunctionLessEqual);
    DepthStencilDescriptor->setDepthWriteEnabled(true);
    DepthStencilStates[DEPTH_Test_And_Write] = Device->newDepthStencilState(DepthStencilDescriptor);
    DepthStencilDescriptor->setDepthWriteEnabled(false);
    DepthStencilStates[DEPTH_Test_No_Write] = Device->newDepthStencilState(DepthStencilDescriptor);
    DepthStencilDescriptor->setDepthCompareFunction(MTL::CompareFunction::CompareFunctionAlways);
    DepthStencilStates[DEPTH_No_Test_No_Write] = Device->newDepthStencilState(DepthStencilDescriptor);
    DepthStencilDescriptor->release();
    
    // Create uniforms buffer
    GlobalUniformsBuffer.Initialize(1, Device, IDX_Uniforms, IDX_Uniforms);
    GlobalUniformsBuffer.Advance(1);
    
    RegisterTextureFormats();

    InitShaders();
    
    ActivePipelineState = nullptr;

	// Great success
	return TRUE;
}

/*-----------------------------------------------------------------------------
    InitShaders
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::InitShaders()
{
    Shaders[SHADER_Tile] = new DrawTileProgram(this, TEXT("DrawTile"), "DrawTileVertex", "DrawTileFragment");
    Shaders[SHADER_Complex] = new DrawComplexProgram(this, TEXT("DrawComplex"), "DrawComplexVertex", "DrawComplexFragment");
    Shaders[SHADER_Gouraud] = new DrawGouraudProgram(this, TEXT("DrawGouraud"), "DrawGouraudVertex", "DrawGouraudFragment");
    Shaders[SHADER_Simple_Triangle] = new DrawSimpleTriangleProgram(this, TEXT("DrawSimpleTriangle"), "DrawSimpleTriangleVertex", "DrawSimpleTriangleFragment");
    
    for (auto Shader : Shaders)
    {
        if (!Shader)
            continue;
        
        Shader->BuildCommonPipelineStates();
        Shader->InitializeBuffers();
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
    for (auto Tex : {DepthTexture, GammaCorrectInputTexture, MultisampleTexture, ResolveTexture, MultisampleDepthTexture, ResolveDepthTexture})
    {
        if (Tex)
            Tex->release();
    }
    if (CommandQueue)
        CommandQueue->release();
    if (Device)
        Device->release();
}

/*-----------------------------------------------------------------------------
	Flush
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::Flush(INT AllowPrecache)
{
    for (auto It = TMap<FCacheID, CachedTexture*>::TIterator(BindMap); It; ++It)
    {
        auto Tex = It.Value();
        if (Tex->Texture)
        {
            Tex->Texture->release();
            Tex->Texture = nullptr;
        }
		delete Tex;
    }
    BindMap.Empty();
}

/*-----------------------------------------------------------------------------
	Exec
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
	if (NumInFlightFrames >= MAX_IN_FLIGHT_FRAMES)
	{
		RendererSuspended = TRUE;
		return;
	}

	RendererSuspended = FALSE;
	__sync_fetch_and_add(&NumInFlightFrames, 1);
	
    SetDepthMode(DEPTH_Test_And_Write);
    DrawingWeapon = false;
    FlashScale = _FlashScale;
    FlashFog = _FlashFog;
    Drawable = Layer->nextDrawable();
    CommandBuffer = CommandQueue->commandBuffer();

    CreateCommandEncoder(CommandBuffer);
}

/*-----------------------------------------------------------------------------
    Unlock
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::Unlock(UBOOL Blit)
{
	if (RendererSuspended)
		return;
	
    SetProgram(SHADER_None);
    
    CommandEncoder->endEncoding();
    
    ActivePipelineState = nullptr;
    auto ColorAttachment = PassDescriptor->colorAttachments()->object(0);
    ColorAttachment->setStoreAction(MTL::StoreActionStore);
    PassDescriptor->setDepthAttachment(nullptr);
    
    if (UseAA)
    {
		ColorAttachment->setTexture(UseGammaCorrection ? GammaCorrectInputTexture : Drawable->texture());
		ColorAttachment->setResolveTexture(nullptr);
        CommandEncoder->release();
        CommandEncoder = CommandBuffer->renderCommandEncoder(PassDescriptor);
        CommandEncoder->setLabel(NS::String::string("MSAA Compose", NS::UTF8StringEncoding));
        CommandEncoder->setRenderPipelineState(MSAAComposePipelineState);
        CommandEncoder->setFragmentTexture(ResolveTexture, 0);
        CommandEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(6));
        CommandEncoder->endEncoding();
    }

	if (UseGammaCorrection)
	{
		ColorAttachment->setTexture(Drawable->texture());
		CommandEncoder->release();
		CommandEncoder = CommandBuffer->renderCommandEncoder(PassDescriptor);
		CommandEncoder->setLabel(NS::String::string("GammaCorrect", NS::UTF8StringEncoding));
		CommandEncoder->setRenderPipelineState(GammaCorrectPipelineState);
		CommandEncoder->setFragmentTexture(GammaCorrectInputTexture, 0);
		GlobalUniformsBuffer.BindBuffer(CommandEncoder);
		CommandEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(6));
		CommandEncoder->endEncoding();
	}
    
    if (Blit)
        CommandBuffer->presentDrawable(Drawable);

	CommandBuffer->addCompletedHandler(^void( MTL::CommandBuffer* Buf ){
			__sync_fetch_and_sub(&NumInFlightFrames, 1);
		});
		
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
	if (RendererSuspended)
		return;
	
    INT OldProgram = ActiveProgram;
    SetProgram(SHADER_None);
    
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
	FString Stats;
	auto SimpleShader = dynamic_cast<DrawSimpleTriangleProgram*>(Shaders[SHADER_Simple_Triangle]);
	auto TileShader =
		dynamic_cast<DrawTileProgram*>(Shaders[SHADER_Tile]);
	auto ComplexShader =
		dynamic_cast<DrawComplexProgram*>(Shaders[SHADER_Complex]);
	auto GouraudShader =
		dynamic_cast<DrawGouraudProgram*>(Shaders[SHADER_Gouraud]);

	Stats = TEXT("Frucore");
	
	if (!SimpleShader || !TileShader || !ComplexShader || !GouraudShader)
		return;
	

	Stats += FString::Printf(TEXT("Buffer Counts: Simple %05d/%05d/%05d - Tile %05d/%05d/%05d - Complex %05d/%05d/%05d - Gouraud %05d/%05d/%05d"),
							 SimpleShader->VertexBuffer.BufferCount(),
							 SimpleShader->InstanceDataBuffer.BufferCount(),
							 SimpleShader->DrawBuffer.CommandBuffer.Num(),
							 TileShader->VertexBuffer.BufferCount(),
							 TileShader->InstanceDataBuffer.BufferCount(),
							 TileShader->DrawBuffer.CommandBuffer.Num(),
							 ComplexShader->VertexBuffer.BufferCount(),
							 ComplexShader->InstanceDataBuffer.BufferCount(),
							 ComplexShader->DrawBuffer.CommandBuffer.Num(),
							 GouraudShader->VertexBuffer.BufferCount(),
							 GouraudShader->InstanceDataBuffer.BufferCount(),
							 GouraudShader->DrawBuffer.CommandBuffer.Num());
	appStrcpy(Result, *Stats);
}

/*-----------------------------------------------------------------------------
	ReadPixels
-----------------------------------------------------------------------------*/
#if ENGINE_VERSION==227
void UFruCoReRenderDevice::ReadPixels(FColor* Pixels, UBOOL GammaCorrectOutput)
#else
void UFruCoReRenderDevice::ReadPixels(FColor* Pixels)
#endif
{
    check(!CommandEncoder);
    Drawable = Layer->nextDrawable();
    CommandBuffer = CommandQueue->commandBuffer();

	// Spawn a blit encoder to synchronize the CPU-accessible copy of the
	// drawable with its GPU counterpart
    auto BlitEncoder = CommandBuffer->blitCommandEncoder();
    BlitEncoder->synchronizeResource(Drawable->texture());
    BlitEncoder->endEncoding();
    BlitEncoder->release();

	// Commit and wait for the synchronization
    CommandBuffer->commit();
    CommandBuffer->waitUntilCompleted();
    CommandBuffer->release();
    CommandBuffer = nullptr;

	// And now just read the drawable. Easy peasy
    MTL::Region Region(StoredOriginX, StoredOriginY, StoredFX, StoredFY);
    Drawable->texture()->getBytes(Pixels, StoredFX * 4, Region, 0);
	Drawable->release();
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
	if (RendererSuspended)
		return;
	
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
    SetProjection
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::SetProjection(FSceneNode *Frame, UBOOL bNearZ)
{
    const auto ChangedUniforms = 
        (StoredFovAngle != Frame->Viewport->Actor->FovAngle ||
         StoredBrightness != Frame->Viewport->GetOuterUClient()->Brightness ||
         UniformsChanged);
    const auto ChangedProjectionParams =
        (StoredFX != Frame->FX ||
         StoredFY != Frame->FY ||
         StoredOriginX != Frame->XB ||
         StoredOriginY != Frame->YB);
	const auto ChangedDrawableSize =
		(!DepthTexture ||
		 DepthTexture->width() != Drawable->texture()->width() ||
		 DepthTexture->height() != Drawable->texture()->height());
    
    if (!ChangedUniforms && !ChangedProjectionParams && !ChangedDrawableSize)
        return;
    
    UniformsChanged = FALSE;
    
    // If we're doing this in the middle of a frame, we need to switch to a
    // different buffer. This way, all of our in-flight draw calls will still
    // use the old projection matrix and uniforms
    auto OldProgram = ActiveProgram;
    if (CommandEncoder)
    {
        SetProgram(SHADER_None);
        GlobalUniformsBuffer.Signal(CommandBuffer);
        GlobalUniformsBuffer.Rotate(Device, CommandEncoder);
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

    FLOAT Aspect = Frame->FX / Frame->FY;
    FLOAT FovTan = appTan(Viewport->Actor->FovAngle * PI / 360.f);
    FLOAT InvFovTan = 1.f / FovTan;
    
    GlobalUniforms->ProjectionMatrix = simd_matrix_from_rows(
        (simd::float4){ InvFovTan, 0.f, 0.f, 0.f },
        // The Metal coordinate system is left-handed so we flip the Y axis here!
        (simd::float4){ 0.f, -InvFovTan * Aspect, 0.f, 0.f },
        (simd::float4){ 0.f, 0.f, zFar / (zFar - zNear), -zFar * zNear / (zFar - zNear) },
        (simd::float4){ 0.f, 0.f, 1.f, 0.f }
    );
    
    GlobalUniforms->ViewportWidth = Frame->FX;
    GlobalUniforms->ViewportHeight = Frame->FY;
    GlobalUniforms->ViewportOriginX = Frame->XB;
    GlobalUniforms->ViewportOriginY = Frame->YB;
    GlobalUniforms->zNear = zNear;
    GlobalUniforms->zFar = zFar;
    GlobalUniforms->Brightness = StoredBrightness;
    GlobalUniforms->Gamma = 1.7 + GammaOffset;
    GlobalUniforms->LODBias = LODBias;
    GlobalUniforms->DetailMax = 2;
    GlobalUniforms->LightMapFactor = OneXBlending ? 2.f : 4.f;
	GlobalUniforms->LightColorIntensity = ActorXBlending ? 1.f : 1.5f;

    // Push to the GPU
    GlobalUniformsBuffer.BufferData(true);
    
    if (CommandEncoder)
    {
        if (ChangedProjectionParams)
        {
            CommandEncoder->endEncoding();
            CommandEncoder->release();
            CreateCommandEncoder(CommandBuffer, false, false);
        }
        SetProgram(OldProgram);
    }
    
    // debugf(TEXT("Frucore: Set projection matrix"));
    
    if (ChangedDrawableSize)
        CreateRenderTargets();
    if (UseAA && (!MultisampleTexture || MSAASettingsChanged || ChangedDrawableSize))
        CreateMultisampleRenderTargets();
}

/*-----------------------------------------------------------------------------
    CreateRenderTargets
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::CreateRenderTargets()
{
    for (auto Tex : {DepthTexture, GammaCorrectInputTexture})
    {
        if (Tex)
            Tex->release();
    }
    
    auto DrawableSize = Layer->drawableSize();
    auto Width = DrawableSize.width;
    auto Height = DrawableSize.height;
    
    MTL::TextureDescriptor* TextureDescriptor = MTL::TextureDescriptor::alloc()->init();
    TextureDescriptor->setWidth(Width);
    TextureDescriptor->setHeight(Height);
    TextureDescriptor->setTextureType(MTL::TextureType2D);
    TextureDescriptor->setStorageMode(MTL::StorageModePrivate);
    TextureDescriptor->setUsage(MTL::TextureUsageRenderTarget);
    TextureDescriptor->setPixelFormat(MTL::PixelFormatDepth32Float);
    
    DepthTexture = Device->newTexture(TextureDescriptor);
    DepthTexture->setLabel(NS::String::string("DepthStencil", NS::UTF8StringEncoding));
    
    TextureDescriptor->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    TextureDescriptor->setPixelFormat(FrameBufferPixelFormat);
    GammaCorrectInputTexture = Device->newTexture(TextureDescriptor);
    GammaCorrectInputTexture->setLabel(NS::String::string("GammaCorrectInput", NS::UTF8StringEncoding));
    
    TextureDescriptor->release();
}

/*-----------------------------------------------------------------------------
    CreateMultisampleRenderTargets
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::CreateMultisampleRenderTargets()
{
    for (auto Tex : {MultisampleTexture, ResolveTexture, MultisampleDepthTexture, ResolveDepthTexture})
    {
        if (Tex)
            Tex->release();
    }
    
    auto DrawableSize = Layer->drawableSize();
    auto Width = DrawableSize.width;
    auto Height = DrawableSize.height;
    
    MTL::TextureDescriptor* TextureDescriptor = MTL::TextureDescriptor::alloc()->init();
    TextureDescriptor->setWidth(Width);
    TextureDescriptor->setHeight(Height);
    TextureDescriptor->setSampleCount(NumAASamples);
    TextureDescriptor->setTextureType(MTL::TextureType2DMultisample);
    TextureDescriptor->setStorageMode(MTL::StorageModePrivate);
    TextureDescriptor->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    TextureDescriptor->setPixelFormat(FrameBufferPixelFormat);
    
    MultisampleTexture = Device->newTexture(TextureDescriptor);
    MultisampleTexture->setLabel(NS::String::string("Multisample", NS::UTF8StringEncoding));
    
    TextureDescriptor->setTextureType(MTL::TextureType2D);
    TextureDescriptor->setSampleCount(1);
    ResolveTexture = Device->newTexture(TextureDescriptor);
    ResolveTexture->setLabel(NS::String::string("Resolve", NS::UTF8StringEncoding));
    
    TextureDescriptor->setTextureType(MTL::TextureType2DMultisample);
    TextureDescriptor->setPixelFormat(MTL::PixelFormatDepth32Float);
    TextureDescriptor->setSampleCount(NumAASamples);
    MultisampleDepthTexture = Device->newTexture(TextureDescriptor);
    MultisampleDepthTexture->setLabel(NS::String::string("MultisampleDepthStencil", NS::UTF8StringEncoding));
    
    TextureDescriptor->setTextureType(MTL::TextureType2D);
    TextureDescriptor->setSampleCount(1);
    ResolveDepthTexture = Device->newTexture(TextureDescriptor);
    ResolveDepthTexture->setLabel(NS::String::string("ResolveDepthStencil", NS::UTF8StringEncoding));
    
    TextureDescriptor->release();
    
    MSAASettingsChanged = false;
    
    //debugf(TEXT("Frucore: Created multisample textures"));
}

/*-----------------------------------------------------------------------------
    CreateCommandEncoder -
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::CreateCommandEncoder(MTL::CommandBuffer *Buffer, bool ClearDepthBuffer, bool ClearColorBuffer)
{
    PassDescriptor = MTL::RenderPassDescriptor::renderPassDescriptor();
    
    auto ColorAttachment = PassDescriptor->colorAttachments()->object(0);
    auto DepthAttachment = PassDescriptor->depthAttachment();
    
    ColorAttachment->setLoadAction(ClearColorBuffer ? MTL::LoadAction::LoadActionClear : MTL::LoadAction::LoadActionLoad);
    DepthAttachment->setLoadAction(ClearDepthBuffer ? MTL::LoadAction::LoadActionClear : MTL::LoadAction::LoadActionLoad);
    
    DepthAttachment->setClearDepth(1);
    ColorAttachment->setClearColor(MTL::ClearColor(0,0,0,1));
    
    if (UseAA)
    {
        ColorAttachment->setTexture(MultisampleTexture);
        ColorAttachment->setResolveTexture(ResolveTexture);
        ColorAttachment->setStoreAction(MTL::StoreAction::StoreActionStoreAndMultisampleResolve);
        DepthAttachment->setTexture(MultisampleDepthTexture);
        DepthAttachment->setResolveTexture(ResolveDepthTexture);
        DepthAttachment->setStoreAction(MTL::StoreAction::StoreActionStoreAndMultisampleResolve);
    }
    else
    {
        ColorAttachment->setTexture(UseGammaCorrection ? GammaCorrectInputTexture : Drawable->texture());
        ColorAttachment->setStoreAction(MTL::StoreAction::StoreActionStore);
        DepthAttachment->setTexture(DepthTexture);
        DepthAttachment->setStoreAction(MTL::StoreAction::StoreActionStore);
    }
    
    /*
    auto StencilAttachment = PassDescriptor->stencilAttachment();
    StencilAttachment->setLoadAction(MTL::LoadAction::LoadActionClear);
    StencilAttachment->setStoreAction(MTL::StoreAction::StoreActionDontCare);
    StencilAttachment->setTexture(DepthTexture);
    */ 
    
    CommandEncoder = CommandBuffer->renderCommandEncoder(PassDescriptor);
	check(CommandEncoder);
	CommandEncoder->setDepthStencilState(DepthStencilStates[CurrentDepthMode]);
    CommandEncoder->setCullMode(MTL::CullModeNone);
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
    
    if (ActivePipelineState)
        CommandEncoder->setRenderPipelineState(ActivePipelineState);
    
    memset(BoundTextures, 0, sizeof(BoundTextures));
}

/*-----------------------------------------------------------------------------
    SetDepthMode
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::SetDepthMode(DepthMode Mode)
{
    if (Mode != CurrentDepthMode)
    {
        CurrentDepthMode = Mode;
        if (CommandEncoder)
        {
            Shaders[ActiveProgram]->Flush();
            CommandEncoder->setDepthStencilState(DepthStencilStates[Mode]);
        }
    }
}

/*-----------------------------------------------------------------------------
    GetShaderLibrary
-----------------------------------------------------------------------------*/
MTL::Library* UFruCoReRenderDevice::GetShaderLibrary()
{
    NS::Error* Error = nullptr;
    auto Bundle = NS::Bundle::mainBundle();
    auto Library = Bundle ? Device->newDefaultLibrary(Bundle, &Error) : Device->newDefaultLibrary();
    
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
    ShaderOptions Options,
    const TCHAR* Label,
    MTL::Function *VertexShader,
    MTL::Function *FragmentShader
)
{
    auto PipelineDescriptor = MTL::RenderPipelineDescriptor::alloc()->init();
    PipelineDescriptor->setVertexFunction(VertexShader);
    PipelineDescriptor->setFragmentFunction(FragmentShader);
    PipelineDescriptor->setDepthAttachmentPixelFormat(MTL::PixelFormat::PixelFormatDepth32Float);
    
    if (Options & (OPT_MSAAx2|OPT_MSAAx4|OPT_MSAAx8))
        PipelineDescriptor->setSampleCount(RenDev->NumAASamples);
    //PipelineDescriptor->setStencilAttachmentPixelFormat(MTL::PixelFormat::PixelFormatDepth32Float_Stencil8);
    
    auto ColorAttachment = PipelineDescriptor->colorAttachments()->object(0);
    ColorAttachment->setPixelFormat(RenDev->FrameBufferPixelFormat);
    
    struct BlendState
    {
        BlendMode BlendMode;
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
        
        FString PipelineLabel = FString::Printf(TEXT("%ls%ls%ls"), Label, BlendStates[i].Name, *ShaderOptionsString(Options));
        NS::String* NSLabel = FStringToNSString(PipelineLabel);
        PipelineDescriptor->setLabel(NSLabel);
        
        NS::Error* Error = nullptr;
        auto State = RenDev->Device->newRenderPipelineState( PipelineDescriptor, &Error );
        if (!State)
        {
            PrintNSError(TEXT("Error creating pipeline states"), Error);
            return;
        }
        
        ShaderSpecializationKey Key = {BlendStates[i].BlendMode, Options};
        PipelineStates.Set(Key, State);
    }
}

/*-----------------------------------------------------------------------------
    GetPolyFlagsAndShaderOptions
-----------------------------------------------------------------------------*/
DWORD UFruCoReRenderDevice::GetPolyFlagsAndShaderOptions(DWORD PolyFlags, DWORD& Options, bool RemoveOccludeIfSolid)
{
    if( (PolyFlags & (PF_RenderFog|PF_Translucent)) != PF_RenderFog )
        PolyFlags &= ~PF_RenderFog;

    if (!(PolyFlags & (PF_Translucent | PF_Modulated | PF_AlphaBlend | PF_Highlighted)))
        PolyFlags |= PF_Occlude;
    else if (RemoveOccludeIfSolid)
        PolyFlags &= ~PF_Occlude;
    
    // fast path. If no relevant polyflags have changed since our previous query, then just return the same ShaderOptions as last time
    const DWORD RelevantPolyFlags = (PF_Modulated|PF_RenderFog|PF_Masked|PF_Straight_AlphaBlend|PF_Premultiplied_AlphaBlend|PF_NoSmooth);
    if ((CachedPolyFlags&RelevantPolyFlags) ^ (PolyFlags&RelevantPolyFlags))
    {
        Options = OPT_None;
        
        if (PolyFlags & PF_Modulated)
            Options |= OPT_Modulated;
        
        if (PolyFlags & PF_RenderFog)
            Options |= OPT_RenderFog;
        
        if (PolyFlags & PF_Masked)
            Options |= OPT_Masked;
        
        if (PolyFlags & (PF_Straight_AlphaBlend|PF_Premultiplied_AlphaBlend))
            Options |= OPT_AlphaBlended;

		if (PolyFlags & PF_NoSmooth)
			Options |= OPT_NoSmooth;
        
        Options |= CachedMSAAOptions;
        
        CachedPolyFlags = PolyFlags;
        CachedShaderOptions = Options | CachedMSAAOptions;
    }
    else
    {
        // nothing changed
        Options = CachedShaderOptions | CachedMSAAOptions;
    }
    
    return PolyFlags;
}

/*-----------------------------------------------------------------------------
    GetBlendMode
-----------------------------------------------------------------------------*/
UFruCoReRenderDevice::BlendMode UFruCoReRenderDevice::GetBlendMode(DWORD PolyFlags)
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
    return BlendMode;
}

/*-----------------------------------------------------------------------------
    SetPipelineState
-----------------------------------------------------------------------------*/
void UFruCoReRenderDevice::SetPipelineState(const MTL::RenderPipelineState *State)
{
    if (ActivePipelineState == State)
        return;
    
    if (Shaders[ActiveProgram])
        Shaders[ActiveProgram]->Flush();
    
    CommandEncoder->setRenderPipelineState(State);
    ActivePipelineState = State;
}
