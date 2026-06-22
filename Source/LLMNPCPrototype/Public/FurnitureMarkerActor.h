// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteractableInterface.h"
#include "FurnitureMarkerActor.generated.h"

class UBoxComponent;
class USceneComponent;

UCLASS()
class AFurnitureMarkerActor : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AFurnitureMarkerActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Furniture")
	USceneComponent* SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Furniture")
	UBoxComponent* InteractionBox;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	FString FurnitureID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	FString RelatedTableID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	bool bCanSit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	bool bIsOccupied;

	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual FText GetInteractionText_Implementation(AActor* Interactor) const override;
	virtual bool Interact_Implementation(AActor* Interactor) override;
};
