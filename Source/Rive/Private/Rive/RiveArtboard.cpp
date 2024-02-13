// Copyright Rive, Inc. All rights reserved.

#include "Rive/RiveArtboard.h"
#include "Logs/RiveLog.h"
#include "Rive/Core/URStateMachine.h"

#if WITH_RIVE

void URiveArtboard::Initialize(rive::File* InNativeFilePtr)
{
	Initialize(InNativeFilePtr, 0);
}

void URiveArtboard::Initialize(rive::File* InNativeFilePtr, int32 InIndex, const FString& InStateMachineName)
{
	if (!InNativeFilePtr)
	{
		return;
	}

	StateMachineName = InStateMachineName;

	if (InIndex >= InNativeFilePtr->artboardCount())
	{
		InIndex = InNativeFilePtr->artboardCount() - 1;
		UE_LOG(LogRive, Warning,
		       TEXT(
			       "Artboard index specified is out of bounds, using the last available artboard index instead, which is %d"
		       ), InIndex);
	}

	if (rive::Artboard* NativeArtboard = InNativeFilePtr->artboard(InIndex))
	{
		NativeArtboardPtr = NativeArtboard->instance();

		NativeArtboardPtr->advance(0);

		DefaultStateMachinePtr = MakeUnique<UE::Rive::Core::FURStateMachine>(NativeArtboardPtr.get(), StateMachineName);
	}

	bIsInitialized = true;
}

void URiveArtboard::Initialize(rive::File* InNativeFilePtr, const FString& InName, const FString& InStateMachineName)
{
	if (!InNativeFilePtr) return;

	StateMachineName = InStateMachineName;
	rive::Artboard* NativeArtboard = InNativeFilePtr->artboard(std::string(TCHAR_TO_UTF8(*InName)));

	if (!NativeArtboard)
	{
		UE_LOG(LogRive, Error,
		       TEXT("Could not initialize the artboard by the name '%s'. Initializing with default artboard instead"),
		       *InName);
		NativeArtboard = InNativeFilePtr->artboard();
	}

	NativeArtboardPtr = NativeArtboard->instance();

	NativeArtboardPtr->advance(0);

	DefaultStateMachinePtr = MakeUnique<UE::Rive::Core::FURStateMachine>(NativeArtboardPtr.get(), StateMachineName);

	bIsInitialized = true;
}

rive::Artboard* URiveArtboard::GetNativeArtboard() const
{
	if (!NativeArtboardPtr)
	{
		UE_LOG(LogRive, Error, TEXT("Could not retrieve artboard as we have detected an empty rive atrboard."));

		return nullptr;
	}

	return NativeArtboardPtr.get();
}

rive::AABB URiveArtboard::GetBounds() const
{
	if (!NativeArtboardPtr)
	{
		UE_LOG(LogRive, Error, TEXT("Could not retrieve artboard bounds as we have detected an empty rive atrboard."));

		return {0, 0, 0, 0};
	}

	return NativeArtboardPtr->bounds();
}

FVector2f URiveArtboard::GetSize() const
{
	if (!NativeArtboardPtr)
	{
		UE_LOG(LogRive, Error, TEXT("Could not retrieve artboard size as we have detected an empty rive atrboard."));

		return FVector2f::ZeroVector;
	}

	return {NativeArtboardPtr->width(), NativeArtboardPtr->height()};
}

UE::Rive::Core::FURStateMachine* URiveArtboard::GetStateMachine() const
{
	if (!DefaultStateMachinePtr)
	{
		// Not all artboards have state machines, so let's not error it out
		return nullptr;
	}

	return DefaultStateMachinePtr.Get();
}

#endif // WITH_RIVE
