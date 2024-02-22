﻿// Copyright Rive, Inc. All rights reserved.

#pragma once

#include "RiveTypes.h"
#include "PreRiveHeaders.h"
THIRD_PARTY_INCLUDES_START
#include "rive/math/mat2d.hpp"
THIRD_PARTY_INCLUDES_END

#include "RiveRenderCommand.generated.h"

namespace rive
{
	class Artboard;
}

UENUM(BlueprintType)
enum class ERiveRenderCommandType : uint8
{
	Save = 0,
	Restore,
	Transform,
	DrawArtboard,
	DrawPath,
	ClipPath,
	AlignArtboard
};

USTRUCT(BlueprintType)
struct FRiveRenderCommand
{
public:
	GENERATED_BODY()

	FRiveRenderCommand(): Type(), FitType(), X(0), Y(0), X2(0), Y2(0), TX(0), TY(0)
	{
	}

	explicit FRiveRenderCommand(ERiveRenderCommandType InType) : Type(InType), FitType(), X(0), Y(0), X2(0), Y2(0),
	                                                             TX(0), TY(0)
	{
	};
	explicit FRiveRenderCommand(rive::Mat2D Matrix) : Type(ERiveRenderCommandType::Transform), FitType(),
		X(Matrix.xx()), Y(Matrix.xy()), X2(Matrix.yx()), Y2(Matrix.yy()), TX(Matrix.tx()), TY(Matrix.ty())
	{
	};
	
	UPROPERTY(BlueprintReadWrite)
	ERiveRenderCommandType Type;

	UPROPERTY(BlueprintReadWrite)
	ERiveFitType FitType;

	// UPROPERTY(BlueprintReadWrite)
	rive::Artboard* NativeArtboard = nullptr;

	UPROPERTY(BlueprintReadWrite)
	float X;

	UPROPERTY(BlueprintReadWrite)
	float Y;

	UPROPERTY(BlueprintReadWrite)
	float X2;

	UPROPERTY(BlueprintReadWrite)
	float Y2;

	UPROPERTY(BlueprintReadWrite)
	float TX;

	UPROPERTY(BlueprintReadWrite)
	float TY;
};