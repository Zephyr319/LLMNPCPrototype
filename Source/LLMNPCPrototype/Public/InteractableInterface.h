// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InteractableInterface.generated.h"

UINTERFACE(Blueprintable)
class UInteractableInterface : public UInterface
{
	GENERATED_BODY()
};

class IInteractableInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interaction")
	bool CanInteract(AActor* Interactor) const;
	virtual bool CanInteract_Implementation(AActor* Interactor) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interaction")
	FText GetInteractionText(AActor* Interactor) const;
	virtual FText GetInteractionText_Implementation(AActor* Interactor) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interaction")
	bool Interact(AActor* Interactor);
	virtual bool Interact_Implementation(AActor* Interactor);
};
