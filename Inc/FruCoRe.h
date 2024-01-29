/*=============================================================================
    FruCoRe.h: Unreal support code for Apple Metal.
    Copyright 2023 OldUnreal. All Rights Reserved.

    Revision history:
    * Created by Stijn Volckaert
=============================================================================*/

#pragma once

#include "Render.h"

#ifdef GENERATE_METAL_IMPLEMENTATION
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#endif
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <QuartzCore/CAMetalDrawable.hpp>
#include <simd/simd.h>

#define DRAWTILE_VERTEXBUFFER_SIZE 16 * 1024
#define DRAWTILE_INSTANCEDATA_SIZE 1024
#define DRAWTILE_DRAWBUFFER_SIZE 1024
#define DRAWCOMPLEX_VERTEXBUFFER_SIZE 256 * 1024
#define DRAWCOMPLEX_INSTANCEDATA_SIZE 1024
#define DRAWCOMPLEX_DRAWBUFFER_SIZE 1024
#define DRAWGOURAUD_VERTEXBUFFER_SIZE 2 * 1024 * 1024
#define DRAWGOURAUD_INSTANCEDATA_SIZE 16 * 1024
#define DRAWGOURAUD_DRAWBUFFER_SIZE 16 * 1024
#define DRAWSIMPLE_DRAWBUFFER_SIZE 1024
#define DRAWSIMPLE_VERTEXBUFFER_SIZE 1024
#define DRAWSIMPLE_INSTANCEDATA_SIZE 128
#define NUMBUFFERS 3

#if UNREAL_TOURNAMENT_OLDUNREAL
class UFruCoReRenderDevice : public URenderDeviceOldUnreal469
{
	DECLARE_CLASS(UFruCoReRenderDevice,URenderDeviceOldUnreal469,CLASS_Config,FruCoRe);
#else
class UFruCoReRenderDevice : public URenderDevice
{
	DECLARE_CLASS(UFruCoReRenderDevice,URenderDevice,CLASS_Config,FruCoRe);
#endif
	
    //
    // Renderer Options
    //
    UBOOL MacroTextures;
    UBOOL UseVSync;
    
    //
    // A BufferObject describes a GPU-mapped buffer object
    //
    template<typename T> class BufferObject
    {
        friend class ShaderProgam;

    public:
        BufferObject() = default;
        ~BufferObject()
        {
            DeleteBuffers();
        }

        // Current size in number of elements
        size_t Size()
        {
            return Index;
        }

        // Current size in bytes
        size_t SizeBytes()
        {
            return Index * sizeof(T);
        }

        // Moves the IndexOffset forward after buffering @ElementCount elements
        void Advance(uint32_t ElementCount)
        {
            Index += ElementCount;
        }

        // Returns true if the currently active buffer still has room for @ElementCount elements
        bool CanBuffer(uint32_t ElementCount)
        {
            return BufferSize - Index >= ElementCount;
        }

        // Returns true if we have no buffered data in the currently active buffer
        bool IsEmpty()
        {
            return Index == 0;
        }
        
        // Returns true if this buffer still has data we need to queue
        bool HasUnqueuedData()
        {
            return EnqueuedElements < Index;
        }

        //
        // Called when we've run out of available space in the buffer.
        // We will switch to a new buffer that is not currently in use by the GPU.
        // If the GPU is using all buffers, we will allocate a new buffer on the Metal @Device.
        //
        // Optionally, we can pass a pointer to the currently active @Encoder here.
        // If we do that, Rotate will automatically bind the newly activated buffer in
        // the vertex and fragment shader argument tables (if applicable).
        //
        void Rotate(MTL::Device* Device, MTL::RenderCommandEncoder* Encoder=nullptr)
        {
            // First, try and wait for an already available buffer
            if (TryWait())
            {
                // Great, there's at least one buffer the GPU is no longer using
                ActiveBuffer = (ActiveBuffer + 1) % Buffers.Num();
            }
            else
            {
                // The GPU is using all buffers. We'll just allocate a new one then
                Buffers.AddZeroed(1);
                
                // Initialize the new buffer
                auto NewBuffer = Device->newBuffer(BufferSize * sizeof(T), MTL::ResourceStorageModeShared);
                check(NewBuffer);
                
                // Now this part is pretty tricky. We need to ensure that the buffers
                // are ordered by their last use. ActiveBuffer is the most recently
                // used buffer. ActiveBuffer-1 is the second most recently used buffer.
                // etc. This means our new buffer _must_ be inserted right after
                // ActiveBuffer.
                INT InsertionPos = (ActiveBuffer + 1) % Buffers.Num();
                if (InsertionPos != Buffers.Num() - 1)
                {
                    memmove(&Buffers(InsertionPos + 1),
                            &Buffers(InsertionPos),
                            sizeof(MTL::Buffer*) * (Buffers.Num() - InsertionPos - 1));
                }
                
                Buffers(InsertionPos) = NewBuffer;
                for (INT i = 0; i < Buffers.Num(); ++i)
                    check(Buffers(i));
                ActiveBuffer = InsertionPos;
            }
            
            Index = EnqueuedElements = 0;
            
            BindBuffer(Encoder);
            
            // No need to wait for the new buffer here!
            // If TryWait() succeeded, then it will have decremented the semaphore count too
            // If it fails, it leaves the semaphore count untouched
        }
        
        // Binds the buffer object to the vertex and fragment shader argument tables, if applicable
        void BindBuffer(MTL::RenderCommandEncoder* Encoder)
        {
            if (Encoder)
            {
                auto Buffer = Buffers(ActiveBuffer);
                if (VertexBindingIndex != -1)
                    Encoder->setVertexBuffer(Buffer, 0, VertexBindingIndex);
                if (FragmentBindingIndex != -1)
                    Encoder->setFragmentBuffer(Buffer, 0, FragmentBindingIndex);
            }
        }

        // Returns a pointer to the element with index @Index, within the currently active sub-buffer
        // @Index must be >= 0 and <IndexOffset
        T* GetElementPtr(uint32_t ElementIndex)
        {
            checkSlow(ElementIndex < Index);
            auto Buffer = Buffers(ActiveBuffer);
            return &reinterpret_cast<T*>(Buffer->contents())[ElementIndex];
        }

        // Returns a pointer to the element we're currently writing
        T* GetCurrentElementPtr()
        {
            auto Buffer = Buffers(ActiveBuffer);
            return &reinterpret_cast<T*>(Buffer->contents())[Index];
        }

        // Returns a pointer to the last element we've written into the currently active buffer
        T* GetLastElementPtr()
        {
            auto Buffer = Buffers(ActiveBuffer);
            return &reinterpret_cast<T*>(Buffer->contents())[BufferSize - 1];
        }

        // Informs the GPU about data we've written into the buffer.
        // If @bFullyBuffer is true, we will buffer/enqueue the entire
        // buffer contents, even if we had already done so prior to this call
        void BufferData(bool bFullyBuffer=false)
        {
            auto Buffer = Buffers(ActiveBuffer);
            if (bFullyBuffer)
            {
                //Buffer->didModifyRange(NS::Range(0, SizeBytes()));
            }
            else if (Index - EnqueuedElements > 0)
            {
                const auto PrevEnqueuedBytes = EnqueuedElements * sizeof(T);
                //Buffer->didModifyRange(NS::Range(PrevEnqueuedBytes, SizeBytes() - PrevEnqueuedBytes));
            }
            EnqueuedElements = Index;
        }

        // Unmaps and deallocates all buffers
        void DeleteBuffers()
        {
            for (INT i = 0; i < Buffers.Num(); ++i)
            {
                Buffers(i)->release();
                Buffers(i) = nullptr;
            }
            ActiveBuffer = Index = EnqueuedElements = 0;
        }

        //
        // Signals the dispatch semaphore (if @DirectSignal=true) or
        // schedules a signal operation (if @DirectSignal=false).
        // In the latter case, the GPU driver will perform the signal
        // operation using a completedHandler. This handler gets executed
        // when the command buffer has fully processed all queued commands.
        //
        void Signal(MTL::CommandBuffer* Buffer, bool DirectSignal=false)
        {
            if (DirectSignal)
            {
                dispatch_semaphore_signal( Sync );
            }
            else
            {
                Buffer->addCompletedHandler(^void( MTL::CommandBuffer* Buf ){
                    dispatch_semaphore_signal( Sync );
                });
            }
        }

        // Waits for an available buffer.
        void Wait()
        {
            dispatch_semaphore_wait(Sync, DISPATCH_TIME_FOREVER);
        }
        
        // Tries to wait for an available buffer. If no buffers are available
        // (i.e., the GPU is still using all of our allocated buffers),
        // this function will return false
        bool TryWait()
        {
            return dispatch_semaphore_wait(Sync, DISPATCH_TIME_NOW) == 0;
        }
        
        //
        // Initializes our buffer object by creating <NUMBUFFERS> managed Metal buffers of @BufferSize * sizeof(T) bytes each.
        // The CPU can only actively use one of these buffers at any given time.
        // The GPU, however, may be using multiple buffers simultaneously.
        // We use a dispatch semaphore to keep track of how many buffers the GPU is still using.
        //
        // Optionally, we can set a @VertexIndex and @FragmentIndex here. 
        // These are the indices of this buffer object in the vertex and fragment shader argument tables, respectively.
        // If these indices are set, we can automatically (re)bind the buffer in the Bind and Rotate methods.
        //
        void Initialize(uint32_t BufferSize, MTL::Device* Device, int32_t VertexIndex=-1, int32_t FragmentIndex=-1)
        {
            this->BufferSize = BufferSize;
            Sync = dispatch_semaphore_create(NUMBUFFERS-1);
            Buffers.AddZeroed(NUMBUFFERS);
            for (INT i = 0; i < NUMBUFFERS; ++i)
                Buffers(i) = Device->newBuffer(BufferSize * sizeof(T), MTL::ResourceStorageModeShared);
            EnqueuedElements = ActiveBuffer = Index = 0;
            VertexBindingIndex = VertexIndex;
            FragmentBindingIndex = FragmentIndex;
        }

        uint8_t  ActiveBuffer{};         // Index of the active buffer
        uint32_t Index{};                // Index of the next buffer element we're going to write within the currently active buffer (in number of elements)
        uint32_t BufferSize{};           // Size of each of our buffers (in number of T-sized elements)
        uint32_t EnqueuedElements{};     // Number of elements within the currently active buffer we've sent over to the GPU
        int32_t  VertexBindingIndex{};   // Index of this buffer in the vertex shader argument table
        int32_t  FragmentBindingIndex{}; // Index of this buffer in the fragment shader argument table
        dispatch_semaphore_t Sync;       // Semaphore to keep track of available buffers
        TArray<MTL::Buffer*> Buffers;    // All registered metal buffer objects
    };
    
    //
    // Helper class for drawPrimitives(type:vertexStart:vertexCount:instanceCount:baseInstance:) batching
    // TODO: We could probably use some sort of buffer to store DrawPrimitivesIndirectArguments directly on the GPU
    // TODO: This would allow us to draw using only one draw call
    //
    class MultiDrawIndirectBuffer
    {
    public:
        MultiDrawIndirectBuffer()
        {
            CommandBuffer.AddZeroed(1024);
        }
        
        MultiDrawIndirectBuffer(INT MaxMultiDraw)
        {
            CommandBuffer.AddZeroed(MaxMultiDraw);
        }

        void StartDrawCall()
        {
            if (TotalCommands == CommandBuffer.Num())
                CommandBuffer.Add(1024);
            
            CommandBuffer(TotalCommands).vertexStart = TotalVertices;
            CommandBuffer(TotalCommands).baseInstance = TotalCommands;
            CommandBuffer(TotalCommands).instanceCount = 1;
        }

        void EndDrawCall(INT Vertices)
        {
            TotalVertices += Vertices;
            CommandBuffer(TotalCommands++).vertexCount = Vertices;
        }
        
        bool HasUnqueuedCommands()
        {
            return EnqueuedCommands < TotalCommands;
        }

        void Reset()
        {
            EnqueuedCommands = TotalCommands = TotalVertices = 0;
        }

        void Draw(MTL::PrimitiveType Type, MTL::RenderCommandEncoder* Encoder)
        {
            for (INT i = EnqueuedCommands; i < TotalCommands; ++i)
            {
                Encoder->drawPrimitives(
                    Type,
                    CommandBuffer(i).vertexStart,
                    CommandBuffer(i).vertexCount,
                    CommandBuffer(i).instanceCount,
                    CommandBuffer(i).baseInstance
                );
            }
            EnqueuedCommands = TotalCommands;
        }

        TArray<MTL::DrawPrimitivesIndirectArguments> CommandBuffer;
        INT TotalVertices{};
        INT TotalCommands{};
        INT EnqueuedCommands{};
    };
    
    //
    // Shaders
    //
    enum ShaderProgType
    {
        SHADER_None,
        SHADER_Simple_Line,
        SHADER_Simple_Triangle,
        SHADER_Tile,
        SHADER_Gouraud,
        SHADER_Complex,
        SHADER_Max
    };
    
    enum BlendMode
    {
        BLEND_None,
        BLEND_Invisible,
        BLEND_Modulated,
        BLEND_Translucent,
        BLEND_Masked,
        BLEND_StraightAlpha,
        BLEND_PremultipliedAlpha,
        BLEND_Max
    };
    
    enum DepthMode
    {
        DEPTH_Test_And_Write,
        DEPTH_Test_No_Write,
        DEPTH_No_Test_No_Write,
        DEPTH_Max
    };
    
    #include "FruCoRe_Shared_Metal.h"
    
    static FString ShaderOptionsString(ShaderOptions Options)
    {
        FString Result;
#define ADD_OPTION(x)               \
    if (Options & x)                \
    {                               \
        if (Result.Len() > 0)       \
            Result += TEXT("|");    \
        Result += TEXT(#x);         \
    }
        ADD_OPTION(OPT_DetailTexture);
        ADD_OPTION(OPT_MacroTexture);
        ADD_OPTION(OPT_LightMap);
        ADD_OPTION(OPT_FogMap);
        ADD_OPTION(OPT_RenderFog);
        ADD_OPTION(OPT_Modulated);
        ADD_OPTION(OPT_Masked);
        ADD_OPTION(OPT_AlphaBlended);
        if (Result.Len() == 0)
            Result = TEXT("OPT_None");
        return Result;
    }
    
    struct ShaderSpecializationKey
    {
        BlendMode Mode;
        ShaderOptions Options;
        
        UBOOL operator==(const ShaderSpecializationKey& Other)
        {
            return Other.Mode == Mode && Other.Options == Options;
        }
        
        friend DWORD GetTypeHash(const ShaderSpecializationKey& Key)
        {
            return (Key.Options << 4) + Key.Mode;
        }
    };

    // Common interface for all shaders
    class ShaderProgram
    {
    public:
        ShaderProgram() = default;
        
        //
        // Common functions
        //

        void DumpShader(const char* Source, bool AddLineNumbers);
        void BuildPipelineStates(ShaderOptions Options, const TCHAR* Label, MTL::Function* VertexShader, MTL::Function* FragmentShader);
        
        //
        // Shader-specific functions
        //
        
        // Builds the shaders and creates buffers and pipeline states
        virtual void BuildCommonPipelineStates() = 0;
        
        // Retrieves the specialized pipeline state for the given options. If the desired state does not exist, this function should create it on-the-fly
        virtual void SelectPipelineState(BlendMode Mode, ShaderOptions Options) = 0;

        // Activates the default pipeline state for this shader and (potentially) flushes/resets shader-specific buffers
        virtual void ActivateShader() = 0;

        // Called when we're about to switch to a pipeline state for a different shader
        virtual void DeactivateShader() = 0;
        
        // Called when one of our buffers is full. Commits any pending data, then rotates the vertex buffers and resets the draw buffer.
        virtual void RotateBuffers() = 0;

        // Dispatches buffered data
        virtual void Flush() = 0;
        
        //
        // Common Variables
        //
        TMap<ShaderSpecializationKey, MTL::RenderPipelineState*>
                                        PipelineStates;
        UFruCoReRenderDevice*           RenDev{};
        
        // Shader properties
        const TCHAR*                    ShaderName;
        const char*                     VertexFunctionName;
        const char*                     FragmentFunctionName;
    };
    
    template
    <
        typename V,
        typename I,
        auto VertexBufferSize,
        auto VertexBufferBindingIndex,
        auto InstanceDataBufferSize,
        auto InstanceDataBufferBindingIndex
    >
    class ShaderProgramImpl : public ShaderProgram
    {
    public:
        // Persistent state
        MTL::Library*                   Library{};
        
        // Buffered render data
        BufferObject<V>                 VertexBuffer;
        BufferObject<I>                 InstanceDataBuffer;
        MultiDrawIndirectBuffer         DrawBuffer;
        
        // Previously selected state
        ShaderSpecializationKey         CachedStateKey{};
        MTL::RenderPipelineState*       CachedState{};
        
        ShaderProgramImpl() = default;
        
        virtual ~ShaderProgramImpl()
        {
            for (TMap<ShaderSpecializationKey, MTL::RenderPipelineState*>::TIterator It(PipelineStates); It; ++It)
            {
                It.Value()->release();
            }
        }
        
        virtual void BuildCommonPipelineStates()
        {
            // TODO: Build commonly used states here so we don't get too many frame drops due to on-the-fly compilation
            
            // Build buffers
            if (VertexBuffer.Size() == 0)
                VertexBuffer.Initialize(VertexBufferSize, RenDev->Device, VertexBufferBindingIndex);
            if (InstanceDataBuffer.Size() == 0)
                InstanceDataBuffer.Initialize(InstanceDataBufferSize, RenDev->Device, InstanceDataBufferBindingIndex);
        }

        // Builds the shaders and creates buffers and pipeline states
        virtual void SelectPipelineState(BlendMode Mode, ShaderOptions Options)
        {
            ShaderSpecializationKey Key = {Mode, Options};
            
            // Fast path to avoid an expensive pipeline state lookup
            if (Key == CachedStateKey && CachedState)
            {
                RenDev->SetPipelineState(CachedState);
                return;
            }
            
            // We have to switch to a new state. See if we've already compiled the shaders
            auto State = PipelineStates.Find(Key);
            if (State)
            {
                RenDev->SetPipelineState(*State);
                return;
            }
            
            // No such state exists yet. We need to create it on the fly
            MTL::Library* Library = RenDev->GetShaderLibrary();
            check(Library && "Could not create shader library");

            MTL::FunctionConstantValues* ConstantValues = MTL::FunctionConstantValues::alloc()->init();
            for (INT i = 0x01; i <= OPT_Max; i *= 2)
            {
                const bool Value = (Options & i) ? true : false;
                ConstantValues->setConstantValue(&Value, MTL::DataTypeBool, i);
            }
            
            NS::Error* Error = nullptr;
            MTL::Function* VertexShader = Library->newFunction(NS::String::string(VertexFunctionName, NS::UTF8StringEncoding), ConstantValues, &Error);
            MTL::Function* FragmentShader = Library->newFunction(NS::String::string(FragmentFunctionName, NS::UTF8StringEncoding), ConstantValues, &Error);

            BuildPipelineStates(Options, ShaderName, VertexShader, FragmentShader);

            debugf(NAME_DevGraphics, TEXT("Frucore: Specialized %ls Shaders for Options %ls"), ShaderName, *ShaderOptionsString(Options));
            
            VertexShader->release();
            FragmentShader->release();
            ConstantValues->release();
            Library->release();
            
            State = PipelineStates.Find(Key);
            check(State);
            
            RenDev->SetPipelineState(*State);
        }

        // Binds this shader's buffer to the active commandencoder
        virtual void ActivateShader()
        {
            VertexBuffer.BindBuffer(RenDev->CommandEncoder);
            InstanceDataBuffer.BindBuffer(RenDev->CommandEncoder);
        }

        // Called when we're about to switch to a pipeline state for a different shader
        virtual void DeactivateShader()
        {
            Flush();
        }
        
        // Called when one of our buffers is full. Commits any pending data, then rotates the vertex buffers and resets the draw buffer.
        virtual void RotateBuffers()
        {
            bool ShouldFlush = DrawBuffer.HasUnqueuedCommands();
            if (ShouldFlush)
                Flush();
            
            VertexBuffer.Rotate(RenDev->Device, RenDev->CommandEncoder);
            InstanceDataBuffer.Rotate(RenDev->Device, RenDev->CommandEncoder);
            DrawBuffer.Reset();
            
            VertexBuffer.Signal(RenDev->CommandBuffer, !ShouldFlush);
            InstanceDataBuffer.Signal(RenDev->CommandBuffer, !ShouldFlush);
        }

        // Dispatches buffered data
        virtual void Flush()
        {
            if (!DrawBuffer.HasUnqueuedCommands())
                return;
            
            VertexBuffer.BufferData();
            InstanceDataBuffer.BufferData();
                
            DrawBuffer.Draw(MTL::PrimitiveTypeTriangle, RenDev->CommandEncoder);
        }
    };

    ShaderProgram* Shaders[SHADER_Max]{};
    void ResetShaders();
    void RecompileShaders();
    void InitShaders();
    INT ActiveProgram{};
    
    #include "FruCoRe_DrawComplex_Metal.h"
    #include "FruCoRe_DrawGouraud_Metal.h"
    #include "FruCoRe_DrawTile_Metal.h"
    #include "FruCoRe_DrawSimple_Metal.h"
    
    class DrawComplexProgram : public ShaderProgramImpl<ComplexVertex, ComplexInstanceData, DRAWCOMPLEX_VERTEXBUFFER_SIZE, IDX_DrawComplexVertexData, DRAWCOMPLEX_INSTANCEDATA_SIZE, IDX_DrawComplexInstanceData>
    {
    public:
        DrawComplexProgram(UFruCoReRenderDevice* _RenDev, const TCHAR* _ShaderName, const char* _VertexFunctionName, const char* _FragmentFunctionName)
        {
            this->RenDev = _RenDev;
            this->ShaderName = _ShaderName;
            this->VertexFunctionName = _VertexFunctionName;
            this->FragmentFunctionName = _FragmentFunctionName;
        }
    };
    
    class DrawGouraudProgram : public ShaderProgramImpl<GouraudVertex, GouraudInstanceData, DRAWGOURAUD_VERTEXBUFFER_SIZE, IDX_DrawGouraudVertexData, DRAWGOURAUD_INSTANCEDATA_SIZE, IDX_DrawGouraudInstanceData>
    {
    public:
        DrawGouraudProgram(UFruCoReRenderDevice* _RenDev, const TCHAR* _ShaderName, const char* _VertexFunctionName, const char* _FragmentFunctionName)
        {
            this->RenDev = _RenDev;
            this->ShaderName = _ShaderName;
            this->VertexFunctionName = _VertexFunctionName;
            this->FragmentFunctionName = _FragmentFunctionName;
        }
        void PrepareDrawCall(FSceneNode* Frame, FTextureInfo& Info, DWORD PolyFlags);
        void FinishDrawCall(FTextureInfo& Info);
        void BufferVert(GouraudVertex* Vert, FTransTexture* P);
        void PushClipPlane(const FPlane& ClipPlane);
        void PopClipPlane();
        
        DWORD LastShaderOptions{};
        FTextureInfo DetailTextureInfo{};
        FTextureInfo MacroTextureInfo{};
    };
    
    class DrawTileProgram : public ShaderProgramImpl<TileVertex, TileInstanceData, DRAWTILE_VERTEXBUFFER_SIZE, IDX_DrawTileVertexData, DRAWTILE_INSTANCEDATA_SIZE, IDX_DrawTileInstanceData>
    {
    public:
        DrawTileProgram(UFruCoReRenderDevice* _RenDev, const TCHAR* _ShaderName, const char* _VertexFunctionName, const char* _FragmentFunctionName)
        {
            this->RenDev = _RenDev;
            this->ShaderName = _ShaderName;
            this->VertexFunctionName = _VertexFunctionName;
            this->FragmentFunctionName = _FragmentFunctionName;
        }
    };
    
    class DrawSimpleTriangleProgram : public ShaderProgramImpl<SimpleTriangleVertex, SimpleTriangleInstanceData, DRAWSIMPLE_VERTEXBUFFER_SIZE, IDX_DrawSimpleTriangleVertexData, DRAWSIMPLE_INSTANCEDATA_SIZE, IDX_DrawSimpleTriangleInstanceData>
    {
    public:
        DrawSimpleTriangleProgram(UFruCoReRenderDevice* _RenDev, const TCHAR* _ShaderName, const char* _VertexFunctionName, const char* _FragmentFunctionName)
        {
            this->RenDev = _RenDev;
            this->ShaderName = _ShaderName;
            this->VertexFunctionName = _VertexFunctionName;
            this->FragmentFunctionName = _FragmentFunctionName;
        }
    };
    
    //
    // UObject interface
    //
    void StaticConstructor();
    void PostEditChange();
    void ShutdownAfterError();

    //
	// URenderDevice interface
    //
	UBOOL Init(UViewport* InViewport, INT NewX, INT NewY, INT NewColorBytes, UBOOL Fullscreen);
	UBOOL SetRes(INT NewX, INT NewY, INT NewColorBytes, UBOOL Fullscreen ); 
	void Exit();
	void Flush(INT AllowPrecache);
	UBOOL Exec(const TCHAR* Cmd, FOutputDevice& Ar);
	void Lock(FPlane FlashScale, FPlane FlashFog, FPlane ScreenClear, DWORD RenderLockFlags, BYTE* HitData, INT* HitSize);
	void Unlock(UBOOL Blit);
	void DrawComplexSurface(FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet);
	void DrawGouraudPolygon(FSceneNode* Frame, FTextureInfo& Info, FTransTexture** Pts, INT NumPts, DWORD PolyFlags, FSpanBuffer* Span);
	void DrawTile(FSceneNode* Frame, FTextureInfo& Info, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT U, FLOAT V, FLOAT UL, FLOAT VL, class FSpanBuffer* Span, FLOAT Z, FPlane Color, FPlane Fog, DWORD PolyFlags);
	void Draw3DLine(FSceneNode* Frame, FPlane Color, DWORD LineFlags, FVector OrigP, FVector OrigQ);
	void Draw2DClippedLine(FSceneNode* Frame, FPlane Color, DWORD LineFlags, FVector P1, FVector P2);
	void Draw2DLine(FSceneNode* Frame, FPlane Color, DWORD LineFlags, FVector P1, FVector P2);
	void Draw2DPoint(FSceneNode* Frame, FPlane Color, DWORD LineFlags, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, FLOAT Z);
	void ClearZ(FSceneNode* Frame);
	void PushHit(const BYTE* Data, INT Count);
	void PopHit(INT Count, UBOOL bForce);
	void GetStats(TCHAR* Result);
	void ReadPixels(FColor* Pixels);
    void EndFlash();
    void DrawStats( FSceneNode* Frame );
    void SetSceneNode( FSceneNode* Frame );
    void PrecacheTexture( FTextureInfo& Info, DWORD PolyFlags );

	// 469 interface
	void DrawGouraudTriangles(const FSceneNode* Frame, const FTextureInfo& Info, FTransTexture* const Pts, INT NumPts, DWORD PolyFlags, DWORD DataFlags, FSpanBuffer* Span);
	UBOOL SupportsTextureFormat(ETextureFormat Format);
    
    // 227 interface
    void DrawGouraudPolyList(FSceneNode* Frame, FTextureInfo& Info, FTransTexture* Pts, INT NumPts, DWORD PolyFlags, FSpanBuffer* Span);
    
    //
    // Renderer-specific functions
    //
    void SetProgram(INT Program);
    DWORD GetPolyFlags(DWORD PolyFlags, DWORD& Options);
    static BlendMode GetBlendMode(DWORD PolyFlags);
    void SetDepthMode(DepthMode Mode);
    void SetTexture(INT TexNum, FTextureInfo& Info, DWORD PolyFlags, FLOAT PanBias);
    void SetProjection(FSceneNode* Frame, UBOOL bNearZ);
    void CreateDepthTexture();
    void RegisterTextureFormats();
    void CreateCommandEncoder(MTL::CommandBuffer* Buffer, bool ClearDepthBuffer=true, bool ClearColorBuffer=true);
    MTL::Library* GetShaderLibrary();
    void SetPipelineState(const MTL::RenderPipelineState* State);

//private:
    // Persistent state
	UViewport*                      Viewport;
	SDL_Renderer*                   Renderer;
	CA::MetalLayer*                 Layer;
	MTL::Device*                    Device;
    BufferObject<GlobalUniforms>    GlobalUniformsBuffer;
    MTL::CommandQueue*              CommandQueue;
    MTL::Texture*                   DepthTexture;
    MTL::DepthStencilState*         DepthStencilStates[DEPTH_Max];
    DepthMode                       CurrentDepthMode;
    
    // Texture state
    typedef BYTE* (*ConversionFunc)(FTextureInfo&, DWORD, INT);
    struct TextureFormat
    {
        MTL::PixelFormat    MetalFormat;
        INT                 BlockSize;
        ConversionFunc      ConversionFunction;
    };
    struct CachedTexture
    {
        QWORD               CacheID;
        MTL::Texture*       Texture;
        INT                 RealTimeChangeCount;
        FLOAT               UMult;
        FLOAT               VMult;
        FLOAT               UPan;
        FLOAT               VPan;
    };
    TMap<INT, TextureFormat>        TextureFormats;
    TMap<QWORD, CachedTexture>      BindMap;
    
    // Per-frame state
    const MTL::RenderPipelineState* ActivePipelineState{};
    MTL::CommandBuffer*             CommandBuffer;
    MTL::RenderPassDescriptor*      PassDescriptor;
    MTL::RenderCommandEncoder*      CommandEncoder;
    CA::MetalDrawable*              Drawable;
    CachedTexture*                  BoundTextures[8];
    
    // Cached projection state. If any of these change, we need to recalculate our projection matrices
    FLOAT                           StoredFovAngle;
    FLOAT                           StoredFX; // Viewport width
    FLOAT                           StoredFY; // Viewport height
    FLOAT                           StoredOriginX;
    FLOAT                           StoredOriginY;

    // Depth info
    FLOAT                           zNear;
    FLOAT                           zFar;
    
    // Brightness and gamma
    FLOAT                           StoredBrightness;
    
    // Screen flashes
    FPlane                          FlashScale;
    FPlane                          FlashFog;
    
    // Cached info for polyflag => shader options conversion
    DWORD                           CachedPolyFlags;
    DWORD                           CachedShaderOptions;
    
    // Hack: When we detect the first draw call within PostRender, we clear the Z buffer so the weapon and HUD render on top of anything else
    BOOL                            DrawingWeapon;

	//
	// Helper functions
	//
	static void PrintNSError(const TCHAR* Prefix, const NS::Error* Error);
};

#define AUTO_INITIALIZE_REGISTRANTS_FRUCORE \
	UFruCoReRenderDevice::StaticClass();
