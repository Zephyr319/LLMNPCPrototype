// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteractableInterface.h"
#include "MenuInteractableActor.generated.h"

class UBoxComponent;
class USceneComponent;

UCLASS()
class AMenuInteractableActor : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AMenuInteractableActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Menu")
	USceneComponent* SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Menu")
	UBoxComponent* InteractionBox;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
	FString MenuID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
	FString RelatedTableID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu")
	bool bRequiresSeated;

	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual FText GetInteractionText_Implementation(AActor* Interactor) const override;
	virtual bool Interact_Implementation(AActor* Interactor) override;

private:
	void PrintDebugMessage(const FString& Message, const FColor& Color) const;
};
