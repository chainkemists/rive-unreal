// Copyright Rive, Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RiveActor.generated.h"

class URiveFile;
class URiveFullScreenUserWidget;

// Idea from Engine\Plugins\Experimental\VirtualProductionUtilities\Source\VPUtilities\Public\Actors\VPFullScreenUserWidgetActor.h

UCLASS()
class RIVE_API ARiveActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ARiveActor();

public:
	UPROPERTY(EditAnywhere, Category="Rive")
	TObjectPtr<URiveFile> RiveFile;

	virtual void PostInitializeComponents() override;
	virtual void PostLoad() override;
	virtual void PostActorCreated() override;
	virtual void Destroyed() override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

#if WITH_EDITOR
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
#endif //WITH_EDITOR
	//~ End AActor Interface

	/* Get a pointer to the inner user widget  */
	UFUNCTION(BlueprintCallable, Category = "User Interface")
	UUserWidget* GetUserWidget() const;

private:
	void RequestEditorDisplay();
	void RequestGameDisplay();

protected:
	/** */
	UPROPERTY(VisibleAnywhere, Instanced, NoClear, Category = "User Interface", meta = (ShowOnlyInnerProperties))
	TObjectPtr<URiveFullScreenUserWidget> ScreenUserWidget;

#if WITH_EDITORONLY_DATA
	/** Display requested and will be executed on the first frame because we can't call BP function in the loading phase */
	bool bEditorDisplayRequested;
#endif
};
