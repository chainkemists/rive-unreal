// Copyright Rive, Inc. All rights reserved.

#include "RiveArtboard.h"

#include "IRiveRenderer.h"
#include "IRiveRendererModule.h"
#include "RiveEvent.h"
#include "Logs/RiveCoreLog.h"
#include "URStateMachine.h"

#if WITH_RIVE
#include "PreRiveHeaders.h"
THIRD_PARTY_INCLUDES_START
#include "rive/animation/state_machine_input.hpp"
#include "rive/animation/state_machine_input_instance.hpp"
#include "rive/generated/animation/state_machine_bool_base.hpp"
#include "rive/generated/animation/state_machine_number_base.hpp"
#include "rive/generated/animation/state_machine_trigger_base.hpp"
THIRD_PARTY_INCLUDES_END
#endif // WITH_RIVE

#if WITH_RIVE

void URiveArtboard::BeginDestroy()
{
	bIsInitialized = false;

	DefaultStateMachinePtr.Reset();
	if (NativeArtboardPtr != nullptr)
	{
		NativeArtboardPtr.release();
	}
	NativeArtboardPtr.reset();

	UObject::BeginDestroy();
}

void URiveArtboard::AdvanceStateMachine(float InDeltaSeconds)
{
	UE::Rive::Core::FURStateMachine* StateMachine = GetStateMachine();
	if (StateMachine && StateMachine->IsValid() && ensure(RiveRenderTarget))
	{
		if (!bIsReceivingInput)
		{
			auto LocalAdvanceStateMachine = [this, StateMachine, InDeltaSeconds]()
			{
				if (IsValid(this))
				{
					if (StateMachine->HasAnyReportedEvents())
					{
						PopulateReportedEvents();
					}
#if PLATFORM_ANDROID
					UE_LOG(LogRive, Verbose, TEXT("[%s] StateMachine->Advance"), IsInRHIThread() ? TEXT("RHIThread") : (IsInRenderingThread() ? TEXT("RenderThread") : TEXT("OtherThread")));
#endif
					StateMachine->Advance(InDeltaSeconds);
				}
			};

			LocalAdvanceStateMachine();
		}
	}
}

void URiveArtboard::Align(ERiveFitType InFitType, ERiveAlignment InAlignment)
{
	if (!RiveRenderTarget)
	{
		return;
	}
	RiveRenderTarget->Align(InFitType, FRiveAlignment::GetAlignment(InAlignment), GetNativeArtboard());
}

void URiveArtboard::Draw()
{
	if (!RiveRenderTarget)
	{
		return;
	}
	RiveRenderTarget->Draw(GetNativeArtboard());
}

bool URiveArtboard::BindNamedRiveEvent(const FString& EventName, const FRiveNamedEventDelegate& Event)
{
	if (EventNames.Contains(EventName))
	{
		NamedRiveEventsDelegates.FindOrAdd(EventName, {}).AddUnique(Event);
		return true;
	}
	UE_LOG(LogRiveCore, Error, TEXT("Unable to bind event '%s' to Artboard '%s' as the event does not exist"), *EventName, *GetArtboardName())
	return false;
}

bool URiveArtboard::UnbindNamedRiveEvent(const FString& EventName, const FRiveNamedEventDelegate& Event)
{
	if (EventNames.Contains(EventName))
	{
		if (FRiveNamedEventsDelegate* NamedRiveDelegate = NamedRiveEventsDelegates.Find(EventName))
		{
			NamedRiveDelegate->Remove(Event);
			if (!NamedRiveDelegate->IsBound())
			{
				NamedRiveEventsDelegates.Remove(EventName);
			}
		}
		return true;
	}
	UE_LOG(LogRiveCore, Error, TEXT("Unable to bind event '%s' to Artboard '%s' as the event does not exist"), *EventName, *GetArtboardName())
	return false;
}

bool URiveArtboard::TriggerNamedRiveEvent(const FString& EventName, float ReportedDelaySeconds)
{
	if (NativeArtboardPtr && GetStateMachine())
	{
		if (rive::Component* Component = NativeArtboardPtr->find(TCHAR_TO_UTF8(*EventName)))
		{
			if(Component->is<rive::Event>())
			{
				rive::Event* Event = Component->as<rive::Event>();
				const rive::CallbackData CallbackData(GetStateMachine()->GetNativeStateMachinePtr().get(), ReportedDelaySeconds);
				Event->trigger(CallbackData);
				UE_LOG(LogRiveCore, Warning, TEXT("TRIGGERED event '%s' for Artboard '%s'"), *EventName, *GetArtboardName())
				return true;
			}
		}
		UE_LOG(LogRiveCore, Error, TEXT("Unable to trigger event '%s' for Artboard '%s' as the Artboard is not ready or it doesn't have a state machine"), *EventName, *GetArtboardName())
	}
	else
	{
		UE_LOG(LogRiveCore, Error, TEXT("Unable to trigger event '%s' for Artboard '%s' as the event does not exist"), *EventName, *GetArtboardName())
	}
	
	return false;
}

void URiveArtboard::Initialize(rive::File* InNativeFilePtr, const UE::Rive::Renderer::IRiveRenderTargetPtr& InRiveRenderTarget)
{
	Initialize(InNativeFilePtr, InRiveRenderTarget, 0);
}

void URiveArtboard::Initialize(rive::File* InNativeFilePtr, UE::Rive::Renderer::IRiveRenderTargetPtr InRiveRenderTarget,
                               int32 InIndex, const FString& InStateMachineName, ERiveFitType InFitType,
                               ERiveAlignment InAlignment)
{
	RiveFitType = InFitType;
	RiveAlignment = InAlignment;
	RiveRenderTarget = InRiveRenderTarget;
	StateMachineName = InStateMachineName;

	ENQUEUE_RENDER_COMMAND(URiveFileInitialize)(
		[this, InRiveRenderTarget, InNativeFilePtr, InFitType, InAlignment, InStateMachineName, InIndex](
		FRHICommandListImmediate& RHICmdList)
		{
#if PLATFORM_ANDROID
	   RHICmdList.EnqueueLambda(TEXT("URiveFile::Initialize"), [this, InRiveRenderTarget, InNativeFilePtr, InFitType, InAlignment, InStateMachineName, InIndex](FRHICommandListImmediate& RHICmdList)
	   {
#endif // PLATFORM_ANDROID
	   		UE::Rive::Renderer::IRiveRenderer* RiveRenderer = UE::Rive::Renderer::IRiveRendererModule::Get().GetRenderer();
			FScopeLock Lock(&RiveRenderer->GetThreadDataCS());

			if (!InNativeFilePtr)
			{
				return;
			}

			int32 Index = InIndex;
			if (Index >= InNativeFilePtr->artboardCount())
			{
				Index = InNativeFilePtr->artboardCount() - 1;
				UE_LOG(LogRiveCore, Warning,
				       TEXT(
					       "Artboard index specified is out of bounds, using the last available artboard index instead, which is %d"
				       ), Index);
			}

			if (const rive::Artboard* NativeArtboard = InNativeFilePtr->artboard(Index))
			{
				Initialize_Internal(NativeArtboard);
			}
#if PLATFORM_ANDROID
		});
#endif // PLATFORM_ANDROID
		});
}

void URiveArtboard::Initialize(rive::File* InNativeFilePtr, UE::Rive::Renderer::IRiveRenderTargetPtr InRiveRenderTarget,
                               const FString& InName, const FString& InStateMachineName, ERiveFitType InFitType,
                               ERiveAlignment InAlignment)
{
	RiveFitType = InFitType;
	RiveAlignment = InAlignment;
	RiveRenderTarget = InRiveRenderTarget;
	StateMachineName = InStateMachineName;

	ENQUEUE_RENDER_COMMAND(URiveFileInitialize)(
		[InName, InStateMachineName, this, InNativeFilePtr](
		FRHICommandListImmediate& RHICmdList)
		{
#if PLATFORM_ANDROID
	   RHICmdList.EnqueueLambda(TEXT("URiveFile::Initialize"), [this, RiveRenderer](FRHICommandListImmediate& RHICmdList)
	   {
#endif // PLATFORM_ANDROID
	   	UE::Rive::Renderer::IRiveRenderer* RiveRenderer = UE::Rive::Renderer::IRiveRendererModule::Get().GetRenderer();
			FScopeLock Lock(&RiveRenderer->GetThreadDataCS());

			rive::Artboard* NativeArtboard = InNativeFilePtr->artboard(TCHAR_TO_UTF8(*InName));

			if (!NativeArtboard)
			{
				UE_LOG(LogRiveCore, Error,
				       TEXT("Could not initialize the artboard by the name '%s'. Initializing with default artboard instead"),
				       *InName);
				NativeArtboard = InNativeFilePtr->artboard();
			}
			Initialize_Internal(NativeArtboard);
#if PLATFORM_ANDROID
		});
#endif // PLATFORM_ANDROID
		});
}

void URiveArtboard::Tick_Render(float InDeltaSeconds)
{
	if (OnArtboardTick_Render.IsBound())
	{
		OnArtboardTick_Render.Broadcast(InDeltaSeconds);
	}
	else
	{
		Align(RiveFitType, RiveAlignment);
		Draw();
	}
}

void URiveArtboard::Tick_StateMachine(float InDeltaSeconds)
{
	if (OnArtboardTick_StateMachine.IsBound())
	{
		OnArtboardTick_StateMachine.Broadcast(InDeltaSeconds);
	}
	else
	{
		AdvanceStateMachine(InDeltaSeconds);
	}
}

void URiveArtboard::Tick(float InDeltaSeconds)
{
	if (!RiveRenderTarget || !bIsInitialized)
	{
		return;
	}

	Tick_StateMachine(InDeltaSeconds);
	Tick_Render(InDeltaSeconds);
}

rive::Artboard* URiveArtboard::GetNativeArtboard() const
{
	UE::Rive::Renderer::IRiveRenderer* RiveRenderer = UE::Rive::Renderer::IRiveRendererModule::Get().GetRenderer();
	FScopeLock Lock(&RiveRenderer->GetThreadDataCS());

	if (!NativeArtboardPtr)
	{
		UE_LOG(LogRiveCore, Error, TEXT("Could not retrieve artboard as we have detected an empty rive atrboard."));

		return nullptr;
	}

	return NativeArtboardPtr.get();
}

rive::AABB URiveArtboard::GetBounds() const
{
	UE::Rive::Renderer::IRiveRenderer* RiveRenderer = UE::Rive::Renderer::IRiveRendererModule::Get().GetRenderer();
	FScopeLock Lock(&RiveRenderer->GetThreadDataCS());

	if (!NativeArtboardPtr)
	{
		UE_LOG(LogRiveCore, Error,
		       TEXT("Could not retrieve artboard bounds as we have detected an empty rive atrboard."));

		return {0, 0, 0, 0};
	}

	return NativeArtboardPtr->bounds();
}

FVector2f URiveArtboard::GetSize() const
{
	UE::Rive::Renderer::IRiveRenderer* RiveRenderer = UE::Rive::Renderer::IRiveRendererModule::Get().GetRenderer();
	FScopeLock Lock(&RiveRenderer->GetThreadDataCS());

	if (!NativeArtboardPtr)
	{
		UE_LOG(LogRiveCore, Error,
		       TEXT("Could not retrieve artboard size as we have detected an empty rive atrboard."));

		return FVector2f::ZeroVector;
	}

	return {NativeArtboardPtr->width(), NativeArtboardPtr->height()};
}

UE::Rive::Core::FURStateMachine* URiveArtboard::GetStateMachine() const
{
	UE::Rive::Renderer::IRiveRenderer* RiveRenderer = UE::Rive::Renderer::IRiveRendererModule::Get().GetRenderer();
	FScopeLock Lock(&RiveRenderer->GetThreadDataCS());

	if (!DefaultStateMachinePtr)
	{
		// Not all artboards have state machines, so let's not error it out
		return nullptr;
	}

	return DefaultStateMachinePtr.Get();
}

void URiveArtboard::PopulateReportedEvents()
{
#if WITH_RIVE
	TickRiveReportedEvents.Empty();
	
	if (const UE::Rive::Core::FURStateMachine* StateMachine = GetStateMachine())
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
				if (const FRiveNamedEventsDelegate* NamedEventDelegate = NamedRiveEventsDelegates.Find(RiveEvent.Name))
				{
					NamedEventDelegate->Broadcast(this, RiveEvent);
				}
				TickRiveReportedEvents.Add(MoveTemp(RiveEvent));
			}
		}

		if (!TickRiveReportedEvents.IsEmpty())
		{
			RiveEventDelegate.Broadcast(this, TickRiveReportedEvents);
		}
	}
	else
	{
		UE_LOG(LogRiveCore, Error, TEXT("Failed to populate reported event(s) as we could not retrieve native state machine."));
	}

#endif // WITH_RIVE
}

void URiveArtboard::Initialize_Internal(const rive::Artboard* InNativeArtboard)
{
	NativeArtboardPtr = InNativeArtboard->instance();
	ArtboardName = FString{NativeArtboardPtr->name().c_str()};
	NativeArtboardPtr->advance(0);

	DefaultStateMachinePtr = MakeUnique<UE::Rive::Core::FURStateMachine>(
		NativeArtboardPtr.get(), StateMachineName);

	// UI Helpers
	StateMachineNames.Empty();
	for (int i = 0; i < NativeArtboardPtr->stateMachineCount(); ++i)
	{
		const rive::StateMachine* NativeStateMachine = NativeArtboardPtr->stateMachine(i);
		StateMachineNames.Add(NativeStateMachine->name().c_str());
	}
	
	EventNames.Empty();
	const std::vector<rive::Event*> Events = NativeArtboardPtr->find<rive::Event>();
	for (const rive::Event* Event : Events)
	{
		EventNames.Add(Event->name().c_str());
	}

	BoolInputNames.Empty();
	NumberInputNames.Empty();
	TriggerInputNames.Empty();
	if (DefaultStateMachinePtr && DefaultStateMachinePtr.IsValid())
	{
		for (uint32 i = 0; i < DefaultStateMachinePtr->GetInputCount(); ++i)
		{
			const rive::SMIInput* Input = DefaultStateMachinePtr->GetInput(i);
			if (Input->input()->is<rive::StateMachineBoolBase>())
			{
				BoolInputNames.Add(Input->name().c_str());
			}
			else if (Input->input()->is<rive::StateMachineNumberBase>())
			{
				NumberInputNames.Add(Input->name().c_str());
			}
			else if (Input->input()->is<rive::StateMachineTriggerBase>())
			{
				TriggerInputNames.Add(Input->name().c_str());
			}
			else
			{
				UE_LOG(LogRiveCore, Warning, TEXT("Found input of unknown type '%d' when getting inputs from StateMachine '%s' from Artboard '%hs'"),
					Input->inputCoreType(), *DefaultStateMachinePtr->GetStateMachineName(), InNativeArtboard->name().c_str())
			}
		}
	}
	
	bIsInitialized = true;
}

#endif // WITH_RIVE
