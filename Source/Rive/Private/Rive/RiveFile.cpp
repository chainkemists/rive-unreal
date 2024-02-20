// Copyright Rive, Inc. All rights reserved.

#include "Rive/RiveFile.h"
#include "IRiveRenderTarget.h"
#include "IRiveRenderer.h"
#include "IRiveRendererModule.h"
#include "Logs/RiveLog.h"
#include "Rive/Assets/RiveAsset.h"
#include "Rive/Assets/URAssetImporter.h"
#include "Rive/Assets/URFileAssetLoader.h"
#include "RiveRendererUtils.h"
#include "RiveTextureResource.h"
#include "Rive/RiveArtboard.h"


#if WITH_RIVE
THIRD_PARTY_INCLUDES_START
#include "rive/pls/pls_render_context.hpp"
THIRD_PARTY_INCLUDES_END
#endif // WITH_RIVE

UE_DISABLE_OPTIMIZATION

URiveFile::URiveFile()
{
	// OverrideFormat = PF_R8G8B8A8;
	// RenderTargetFormat = RTF_RGBA8;
	//
	// bCanCreateUAV = false;
	SRGB = false; //todo: check if needed on all platforms
	bIsResolveTarget = true;
	SamplerAddressMode = AM_Wrap;
	Format = PF_R8G8B8A8;

	Size.X = Size.Y = 500;
	SizeX = Size.X;
	SizeY = Size.Y;

	ArtboardIndex = 0;
}

void URiveFile::BeginDestroy()
{
	bIsInitialized = false;
	bIsFileImported = false;
	
	if (IsValid(CopyRenderTarget))
	{
		CopyRenderTarget->ReleaseResource();
		CopyRenderTarget->MarkAsGarbage();
	}
	if (IsValid(RenderTarget))
	{
		RenderTarget->ReleaseResource();
		RenderTarget->MarkAsGarbage();
	}
	
	RiveRenderTarget.Reset();
	
	if (IsValid(Artboard))
	{
		Artboard->MarkAsGarbage();
	}
	
	RiveNativeFileSpan = {};
	RiveNativeFilePtr.reset();
	
	Super::BeginDestroy();
}

TStatId URiveFile::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URiveFile, STATGROUP_Tickables);
}

void URiveFile::Tick(float InDeltaSeconds)
{
	if (!IsValidChecked(this))
	{
		return;
	}

	UE::Rive::Renderer::IRiveRenderer* RiveRenderer = UE::Rive::Renderer::IRiveRendererModule::Get().GetRenderer();

#if WITH_RIVE
	if (!bIsInitialized && bIsFileImported && GetArtboard()) //todo: move away from Tick
	{
		// Resize textures and Flush
		ResizeRenderTargets(Artboard->GetSize());
		
		// Initialize Rive Render Target Only after we resize the texture
		RiveRenderTarget = RiveRenderer->CreateTextureTarget_GameThread(*GetPathName(), GetRenderTargetToDrawOnto());
		RiveRenderTarget->Initialize();

		// Everything is now ready, we can start Rive Rendering
		bIsInitialized = true;
	}
	if (bIsInitialized && bIsRendering)
	{
		// Empty reported events at the beginning
		TickRiveReportedEvents.Empty();
		if (GetArtboard())
		{
			UE::Rive::Core::FURStateMachine* StateMachine = Artboard->GetStateMachine();
			if (StateMachine && StateMachine->IsValid() && ensure(RiveRenderTarget))
			{
				FScopeLock Lock(&RiveRenderer->GetThreadDataCS());
				if (!bIsReceivingInput)
				{
					auto AdvanceStateMachine = [this, StateMachine, InDeltaSeconds]()
					{
						if (StateMachine->HasAnyReportedEvents())
						{
							PopulateReportedEvents();
						}
#if PLATFORM_ANDROID
						UE_LOG(LogRive, Verbose, TEXT("[%s] StateMachine->Advance"), IsInRHIThread() ? TEXT("RHIThread") : (IsInRenderingThread() ? TEXT("RenderThread") : TEXT("OtherThread")));
#endif
						StateMachine->Advance(InDeltaSeconds);
					};
					if (UE::Rive::Renderer::IRiveRendererModule::RunInGameThread())
					{
						AdvanceStateMachine();
					}
					else
					{
						ENQUEUE_RENDER_COMMAND(DrawArtboard)(
							[AdvanceStateMachine = MoveTemp(AdvanceStateMachine)](
							FRHICommandListImmediate& RHICmdList) mutable
							{
#if PLATFORM_ANDROID
								RHICmdList.EnqueueLambda(TEXT("StateMachine->Advance"),
									[AdvanceStateMachine = MoveTemp(AdvanceStateMachine)](FRHICommandListImmediate& RHICmdList)
								{
#endif
								AdvanceStateMachine();
#if PLATFORM_ANDROID
								});
#endif
							});
					}
				}
			}

			const FVector2f RiveAlignmentXY = GetRiveAlignment();
			RiveRenderTarget->DrawArtboard((uint8)RiveFitType, RiveAlignmentXY.X, RiveAlignmentXY.Y,
										   Artboard->GetNativeArtboard(), DebugColor);
			bDrawOnceTest = true;
		}

		// Copy from render target
		// TODO. move from here
		// Separate target might be needed to let Rive draw only to separate texture
		//if (GetRenderTargetToDrawOnto() != this && ensure(RenderTarget))
		if (CurrentResource && ensure(RenderTarget))
		{
			//FTextureRenderTargetResource* RiveFileResource = GameThread_GetRenderTargetResource();
			
			FTextureRenderTargetResource* RiveFileRenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();

			ENQUEUE_RENDER_COMMAND(CopyRenderTexture)(
				[this, RiveFileRenderTargetResource](FRHICommandListImmediate& RHICmdList)
				{
					UE::Rive::Renderer::FRiveRendererUtils::CopyTextureRDG(RHICmdList, RiveFileRenderTargetResource->TextureRHI, CurrentResource->TextureRHI);
				});
		}
	}
#endif // WITH_RIVE
}

bool URiveFile::IsTickable() const
{
	return !HasAnyFlags(RF_ClassDefaultObject) && bIsRendering;
}

FTextureResource* URiveFile::CreateResource()
{
	//UTexture::ReleaseResource() calls the delete
	CurrentResource = new FRiveTextureResource(this);
	SetResource(CurrentResource);
	InitializeResources();

	return CurrentResource;
}

void URiveFile::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

	if (CurrentResource != nullptr)
	{
		CumulativeResourceSize.AddUnknownMemoryBytes(CurrentResource->GetResourceSize());
	}
}

void URiveFile::PostLoad()
{
	UObject::PostLoad();
	
	if (!IsRunningCommandlet())
	{
		CreateRenderTargets();
		
		UE::Rive::Renderer::IRiveRendererModule::Get().CallOrRegister_OnRendererInitialized(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &URiveFile::Initialize));
	}
}

#if WITH_EDITOR

void URiveFile::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	const FName ActiveMemberNodeName = *PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue()->GetName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(URiveFile, ArtboardIndex) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(URiveFile, ArtboardName))
	{
		InstantiateArtboard();
	}
	else if (ActiveMemberNodeName == GET_MEMBER_NAME_CHECKED(URiveFile, Size))
	{
		SizeX = Size.X;
		SizeY = Size.Y;
		InitializeResources();


		if (RenderTarget)
		{
			RenderTarget->ResizeTarget(Size.X, Size.Y); // SizeX is now the right size
		}
		if (RiveRenderTarget)
		{
			RiveRenderTarget->Initialize();
		}
		FlushRenderingCommands();
	}

	// TODO. WE need custom implementation here to handle the Rive File Editor Changes
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif // WITH_EDITOR

URiveFile* URiveFile::CreateInstance(const FString& InArtboardName, const FString& InStateMachineName)
{
	auto NewRiveFileInstance = NewObject<
		URiveFile>(this, URiveFile::StaticClass(), NAME_None, RF_Public | RF_Transient);
	NewRiveFileInstance->ParentRiveFile = this;
	NewRiveFileInstance->ArtboardName = InArtboardName.IsEmpty() ? ArtboardName : InArtboardName;
	NewRiveFileInstance->StateMachineName = InStateMachineName.IsEmpty() ? StateMachineName : InStateMachineName;
	NewRiveFileInstance->ArtboardIndex = ArtboardIndex;
	NewRiveFileInstance->Initialize();
	return NewRiveFileInstance;
}

void URiveFile::FireTrigger(const FString& InPropertyName) const
{
#if WITH_RIVE
	if (GetArtboard())
	{
		if (UE::Rive::Core::FURStateMachine* StateMachine = Artboard->GetStateMachine())
		{
			StateMachine->FireTrigger(InPropertyName);
		}
	}
#endif // WITH_RIVE
}

bool URiveFile::GetBoolValue(const FString& InPropertyName) const
{
#if WITH_RIVE
	if (GetArtboard())
	{
		if (UE::Rive::Core::FURStateMachine* StateMachine = Artboard->GetStateMachine())
		{
			return StateMachine->GetBoolValue(InPropertyName);
		}
	}
#endif // !WITH_RIVE
	
	return false;
}

float URiveFile::GetNumberValue(const FString& InPropertyName) const
{
#if WITH_RIVE
	if (GetArtboard())
	{
		if (UE::Rive::Core::FURStateMachine* StateMachine = Artboard->GetStateMachine())
		{
			return StateMachine->GetNumberValue(InPropertyName);
		}
	}
#endif // !WITH_RIVE

	return 0.f;
}

FLinearColor URiveFile::GetDebugColor() const
{
	return DebugColor;
}

FVector2f URiveFile::GetLocalCoordinates(const FVector2f& InTexturePosition) const
{
#if WITH_RIVE

	if (GetArtboard())
	{
		const FVector2f RiveAlignmentXY = GetRiveAlignment();

		const rive::Mat2D Transform = rive::computeAlignment(
			(rive::Fit)RiveFitType,
			rive::Alignment(RiveAlignmentXY.X, RiveAlignmentXY.Y),
			rive::AABB(0, 0, Size.X, Size.Y),
			Artboard->GetBounds()
		);

		const rive::Vec2D ResultingVector = Transform.invertOrIdentity() * rive::Vec2D(InTexturePosition.X, InTexturePosition.Y);
		return {ResultingVector.x, ResultingVector.y};
	}

#endif // WITH_RIVE

	return FVector2f::ZeroVector;
}

FVector2f URiveFile::GetLocalCoordinatesFromExtents(const FVector2f& InPosition, const FBox2f& InExtents) const
{
	const FVector2f RelativePosition = InPosition - InExtents.Min;
	const FVector2f Ratio { Size.X / InExtents.GetSize().X, SizeY / InExtents.GetSize().Y}; // Ratio should be the same for X and Y
	const FVector2f TextureRelativePosition = RelativePosition * Ratio;
	
	return GetLocalCoordinates(TextureRelativePosition);
}

void URiveFile::SetBoolValue(const FString& InPropertyName, bool bNewValue)
{
#if WITH_RIVE
	if (GetArtboard())
	{
		if (UE::Rive::Core::FURStateMachine* StateMachine = Artboard->GetStateMachine())
		{
			StateMachine->SetBoolValue(InPropertyName, bNewValue);
		}
	}
#endif // WITH_RIVE
}

void URiveFile::SetNumberValue(const FString& InPropertyName, float NewValue)
{
#if WITH_RIVE
	if (GetArtboard())
	{
		if (UE::Rive::Core::FURStateMachine* StateMachine = Artboard->GetStateMachine())
		{
			StateMachine->SetNumberValue(InPropertyName, NewValue);
		}
	}
#endif // WITH_RIVE
}



FVector2f URiveFile::GetRiveAlignment() const
{
	FVector2f NewAlignment = FRiveAlignment::Center;

	switch (RiveAlignment)
	{
	case ERiveAlignment::TopLeft:
		NewAlignment = FRiveAlignment::TopLeft;
		break;
	case ERiveAlignment::TopCenter:
		NewAlignment = FRiveAlignment::TopCenter;
		break;
	case ERiveAlignment::TopRight:
		NewAlignment = FRiveAlignment::TopRight;
		break;
	case ERiveAlignment::CenterLeft:
		NewAlignment = FRiveAlignment::CenterLeft;
		break;
	case ERiveAlignment::Center:
		break;
	case ERiveAlignment::CenterRight:
		NewAlignment = FRiveAlignment::CenterRight;
		break;
	case ERiveAlignment::BottomLeft:
		NewAlignment = FRiveAlignment::BottomLeft;
		break;
	case ERiveAlignment::BottomCenter:
		NewAlignment = FRiveAlignment::BottomCenter;
		break;
	case ERiveAlignment::BottomRight:
		NewAlignment = FRiveAlignment::BottomRight;
		break;
	}

	return NewAlignment;
}

ESimpleElementBlendMode URiveFile::GetSimpleElementBlendMode() const
{
	ESimpleElementBlendMode NewBlendMode = ESimpleElementBlendMode::SE_BLEND_Opaque;

	switch (RiveBlendMode)
	{
	case ERiveBlendMode::SE_BLEND_Opaque:
		break;
	case ERiveBlendMode::SE_BLEND_Masked:
		NewBlendMode = SE_BLEND_Masked;
		break;
	case ERiveBlendMode::SE_BLEND_Translucent:
		NewBlendMode = SE_BLEND_Translucent;
		break;
	case ERiveBlendMode::SE_BLEND_Additive:
		NewBlendMode = SE_BLEND_Additive;
		break;
	case ERiveBlendMode::SE_BLEND_Modulate:
		NewBlendMode = SE_BLEND_Modulate;
		break;
	case ERiveBlendMode::SE_BLEND_MaskedDistanceField:
		NewBlendMode = SE_BLEND_MaskedDistanceField;
		break;
	case ERiveBlendMode::SE_BLEND_MaskedDistanceFieldShadowed:
		NewBlendMode = SE_BLEND_MaskedDistanceFieldShadowed;
		break;
	case ERiveBlendMode::SE_BLEND_TranslucentDistanceField:
		NewBlendMode = SE_BLEND_TranslucentDistanceField;
		break;
	case ERiveBlendMode::SE_BLEND_TranslucentDistanceFieldShadowed:
		NewBlendMode = SE_BLEND_TranslucentDistanceFieldShadowed;
		break;
	case ERiveBlendMode::SE_BLEND_AlphaComposite:
		NewBlendMode = SE_BLEND_AlphaComposite;
		break;
	case ERiveBlendMode::SE_BLEND_AlphaHoldout:
		NewBlendMode = SE_BLEND_AlphaHoldout;
		break;
	}

	return NewBlendMode;
}

#if WITH_EDITOR

bool URiveFile::EditorImport(const FString& InRiveFilePath, TArray<uint8>& InRiveFileBuffer)
{
	if (!UE::Rive::Renderer::IRiveRendererModule::IsAvailable())
	{
		UE_LOG(LogRive, Error, TEXT("Could not load rive file as the required Rive Renderer Module is either missing or not loaded properly."));
		return false;
	}

	UE::Rive::Renderer::IRiveRenderer* RiveRenderer = UE::Rive::Renderer::IRiveRendererModule::Get().GetRenderer();

	if (!RiveRenderer)
	{
		UE_LOG(LogRive, Error, TEXT("Failed to import rive file as we do not have a valid renderer."));
		return false;
	}

	if (!RiveRenderer->IsInitialized())
	{
		UE_LOG(LogRive, Error, TEXT("Could not load rive file as the required Rive Renderer is not initialized."));
		return false;
	}

#if WITH_RIVE

	rive::pls::PLSRenderContext* PLSRenderContext = RiveRenderer->GetPLSRenderContextPtr();
	if (!PLSRenderContext)
	{
		UE_LOG(LogRive, Error, TEXT("Failed to import rive file as we do not have a valid context."));
		return false;
	}

	RiveFilePath = InRiveFilePath;
	RiveFileData = MoveTemp(InRiveFileBuffer);
	RiveNativeFileSpan = rive::make_span(RiveFileData.GetData(), RiveFileData.Num());

	TUniquePtr<UE::Rive::Assets::FURAssetImporter> AssetImporter = MakeUnique<UE::Rive::Assets::FURAssetImporter>(this);

	rive::ImportResult ImportResult;
	RiveNativeFilePtr = rive::File::import(RiveNativeFileSpan, PLSRenderContext,
										   &ImportResult, AssetImporter.Get());
	if (ImportResult != rive::ImportResult::success)
	{
		UE_LOG(LogRive, Error, TEXT("Failed to import rive file."));

		return false;
	}

	return true;
#endif // WITH_RIVE
}

#endif // WITH_EDITOR

void URiveFile::Initialize()
{
	if (!UE::Rive::Renderer::IRiveRendererModule::IsAvailable())
	{
		UE_LOG(LogRive, Error, TEXT("Could not load rive file as the required Rive Renderer Module is either missing or not loaded properly."));
		return;
	}

	UE::Rive::Renderer::IRiveRenderer* RiveRenderer = UE::Rive::Renderer::IRiveRendererModule::Get().GetRenderer();

	if (!RiveRenderer)
	{
		UE_LOG(LogRive, Error, TEXT("Failed to import rive file as we do not have a valid renderer."));
		return;
	}

	if (!RiveRenderer->IsInitialized())
	{
		UE_LOG(LogRive, Error, TEXT("Could not load rive file as the required Rive Renderer is not initialized."));
		return;
	}

#if WITH_RIVE

	if (ParentRiveFile && !ParentRiveFile->bIsFileImported)
	{
		ParentRiveFile->Initialize();

		// TODO: We might have to wait for the parent to finalize initializing before we can continue here.
	}

	if (!ParentRiveFile)
	{
		if (RiveNativeFileSpan.empty())
		{
			if (RiveFileData.IsEmpty())
			{
				UE_LOG(LogRive, Error, TEXT("Could not load an empty Rive File Data."));
				return;
			}
			RiveNativeFileSpan = rive::make_span(RiveFileData.GetData(), RiveFileData.Num());
		}
	}

	if (!RenderTarget) // fallback if we arrive here without passing by PostLoad, like when creating the RiveFile in Editor
	{
		CreateRenderTargets();
	}

	bIsFileImported = false;
	Artboard = NewObject<URiveArtboard>(this); // Should be created in Game Thread
	
	ENQUEUE_RENDER_COMMAND(URiveFileInitialize)(
	[this, RiveRenderer](FRHICommandListImmediate& RHICmdList)
	{
#if PLATFORM_ANDROID
	   RHICmdList.EnqueueLambda(TEXT("URiveFile::Initialize"), [this, RiveRenderer](FRHICommandListImmediate& RHICmdList)
	   {
#endif // PLATFORM_ANDROID
			if (rive::pls::PLSRenderContext* PLSRenderContext = RiveRenderer->GetPLSRenderContextPtr())
			{
				if (!ParentRiveFile)
				{
					const TUniquePtr<UE::Rive::Assets::FURFileAssetLoader> FileAssetLoader = MakeUnique<
						UE::Rive::Assets::FURFileAssetLoader>(this);
					rive::ImportResult ImportResult;

					FScopeLock Lock(&RiveRenderer->GetThreadDataCS());
					RiveNativeFilePtr = rive::File::import(RiveNativeFileSpan, PLSRenderContext, &ImportResult,
														   FileAssetLoader.Get());

					if (ImportResult != rive::ImportResult::success)
					{
						UE_LOG(LogRive, Error, TEXT("Failed to import rive file."));
						return;
					}
				}
				
				InstantiateArtboard_Internal();
			}
			else
			{
				UE_LOG(LogRive, Error, TEXT("Failed to import rive file as we do not have a valid context."));
			}
#if PLATFORM_ANDROID
	   });
#endif // PLATFORM_ANDROID
	});
#endif // WITH_RIVE
}

void URiveFile::InstantiateArtboard()
{
	if (!UE::Rive::Renderer::IRiveRendererModule::IsAvailable())
	{
		UE_LOG(LogRive, Error, TEXT("Could not load rive file as the required Rive Renderer Module is either missing or not loaded properly."));
		return;
	}

	UE::Rive::Renderer::IRiveRenderer* RiveRenderer = UE::Rive::Renderer::IRiveRendererModule::Get().GetRenderer();

	if (!RiveRenderer)
	{
		UE_LOG(LogRive, Error, TEXT("Failed to import rive file as we do not have a valid renderer."));
		return;
	}

	if (!RiveRenderer->IsInitialized())
	{
		UE_LOG(LogRive, Error, TEXT("Could not load rive file as the required Rive Renderer is not initialized."));
		return;
	}

	if (!GetNativeFile())
	{
		UE_LOG(LogRive, Error, TEXT("Could not instance artboard as our native rive file is invalid."));
		return;
	}

	bIsInitialized = false;
	bIsFileImported = false;

	Artboard = NewObject<URiveArtboard>(this); // Should be created in Game Thread
	
	ENQUEUE_RENDER_COMMAND(URiveFileInitialize)(
	[this, RiveRenderer](FRHICommandListImmediate& RHICmdList)
	{
#if PLATFORM_ANDROID
	   RHICmdList.EnqueueLambda(TEXT("URiveFile::Initialize"), [this, RiveRenderer](FRHICommandListImmediate& RHICmdList)
	   {
#endif // PLATFORM_ANDROID
			InstantiateArtboard_Internal();
#if PLATFORM_ANDROID
		});
#endif // PLATFORM_ANDROID
	});
}

void URiveFile::SetWidgetClass(TSubclassOf<UUserWidget> InWidgetClass)
{
	WidgetClass = InWidgetClass;
}

const URiveArtboard* URiveFile::GetArtboard() const
{
#if WITH_RIVE
	if (Artboard && Artboard->IsInitialized())
	{
		return Artboard;
	}
#endif // WITH_RIVE
	return nullptr;
}

void URiveFile::PopulateReportedEvents()
{
#if WITH_RIVE

	if (!GetArtboard())
	{
		return;
	}

	if (UE::Rive::Core::FURStateMachine* StateMachine = Artboard->GetStateMachine())
	{
		const int32 NumReportedEvents = StateMachine->GetReportedEventsCount();

		TickRiveReportedEvents.Reserve(NumReportedEvents);

		for (int32 EventIndex = 0; EventIndex < NumReportedEvents; EventIndex++)
		{
			const rive::EventReport ReportedEvent = StateMachine->GetReportedEvent(EventIndex);
			if (ReportedEvent.event() != nullptr)
			{
				FRiveEvent RiveEvent;
				RiveEvent.Initialize(ReportedEvent);
				TickRiveReportedEvents.Add(MoveTemp(RiveEvent));
			}
		}

		if (!TickRiveReportedEvents.IsEmpty())
		{
			RiveEventDelegate.Broadcast(TickRiveReportedEvents.Num());
		}
	}
	else
	{
		UE_LOG(LogRive, Error,
			   TEXT("Failed to populate reported event(s) as we could not retrieve native state machine."));
	}

#endif // WITH_RIVE
}

void URiveFile::CreateRenderTargets()
{
#if PLATFORM_ANDROID
	constexpr bool bInForceLinearGamma = true; // needed to be true for Android todo: check if really needed
#else
	constexpr bool bInForceLinearGamma = false; // default false for the rest of the platforms
#endif

#if WITH_RIVE
	UE::Rive::Renderer::IRiveRenderer* RiveRenderer = UE::Rive::Renderer::IRiveRendererModule::Get().GetRenderer();

	if (!CurrentResource)
	{
		UpdateResource();
	}

	// Initialize copy texture
	RenderTarget = RiveRenderer->CreateDefaultRenderTarget(FIntPoint(Size.X, Size.Y));

	// Flush resources
	FlushRenderingCommands();
#endif // WITH_RIVE
}

URiveArtboard* URiveFile::InstantiateArtboard_Internal()
{
	RiveRenderTarget.Reset();

	if (GetNativeFile())
	{
		if (ArtboardName.IsEmpty())
		{
			Artboard->Initialize(GetNativeFile(), ArtboardIndex, StateMachineName);
		}
		else
		{
			Artboard->Initialize(GetNativeFile(), ArtboardName, StateMachineName);
		}

		PrintStats();
		bIsFileImported = true;
		return Artboard;
	}
	return nullptr;
}

void URiveFile::ResizeRenderTargets(const FVector2f InNewSize)
{
	if (!CurrentResource)
	{
		// Create Resource
		UpdateResource();
	}
	else
	{
		// Create new TextureRHI wiht new size
		InitializeResources();
	}

	if (RenderTarget)
	{
		RenderTarget->ResizeTarget(InNewSize.X, InNewSize.Y);
		RenderTarget->UpdateResourceImmediate(true);
	}

	FlushRenderingCommands();
}

void URiveFile::PrintStats() const
{
	rive::File* NativeFile = GetNativeFile();
	if (!NativeFile)
	{
		UE_LOG(LogRive, Error, TEXT("Could not print statistics as we have detected an empty rive file."));
		return;
	}

	FFormatNamedArguments RiveFileLoadArgs;
	RiveFileLoadArgs.Add(TEXT("Major"), FText::AsNumber(static_cast<int>(NativeFile->majorVersion)));
	RiveFileLoadArgs.Add(TEXT("Minor"), FText::AsNumber(static_cast<int>(NativeFile->minorVersion)));
	RiveFileLoadArgs.Add(TEXT("NumArtboards"), FText::AsNumber(static_cast<uint32>(NativeFile->artboardCount())));
	RiveFileLoadArgs.Add(TEXT("NumAssets"), FText::AsNumber(static_cast<uint32>(NativeFile->assets().size())));

	if (const rive::Artboard* NativeArtboard = NativeFile->artboard())
	{
		RiveFileLoadArgs.Add(
			TEXT("NumAnimations"), FText::AsNumber(static_cast<uint32>(NativeArtboard->animationCount())));
	}
	else
	{
		RiveFileLoadArgs.Add(TEXT("NumAnimations"), FText::AsNumber(0));
	}

	const FText RiveFileLoadMsg = FText::Format(NSLOCTEXT("FURFile", "RiveFileLoadMsg",
														  "Using Rive Runtime : {Major}.{Minor}; Artboard(s) Count : {NumArtboards}; Asset(s) Count : {NumAssets}; Animation(s) Count : {NumAnimations}"), RiveFileLoadArgs);

	UE_LOG(LogRive, Display, TEXT("%s"), *RiveFileLoadMsg.ToString());
}

void URiveFile::InitializeResources()
{
	ENQUEUE_RENDER_COMMAND(FRiveTextureResourceeUpdateTextureReference)
	([this](FRHICommandListImmediate& RHICmdList) {
		FTextureRHIRef RenderableTexture;
	
		FRHITextureCreateDesc RenderTargetTextureDesc =
			FRHITextureCreateDesc::Create2D(TEXT(""), Size.X, Size.Y, Format)
				.SetClearValue(FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f)))
				.SetFlags(ETextureCreateFlags::Dynamic | ETextureCreateFlags::ShaderResource | TexCreate_RenderTargetable)
				.SetInitialState(ERHIAccess::SRVMask);

		if (SRGB)
		{
			RenderTargetTextureDesc.AddFlags(ETextureCreateFlags::SRGB);
		}

		if (bNoTiling)
		{
			RenderTargetTextureDesc.AddFlags(ETextureCreateFlags::NoTiling);
		}
	
		RenderableTexture = RHICreateTexture(RenderTargetTextureDesc);
		RenderableTexture->SetName(GetFName());
		CurrentResource->TextureRHI = RenderableTexture;

		RHIUpdateTextureReference(TextureReference.TextureReferenceRHI, CurrentResource->TextureRHI);
	});
}

UE_ENABLE_OPTIMIZATION
