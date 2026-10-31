// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteractableInterface.h"
#include "FurnitureMarkerActor.generated.h"

class UBoxComponent;
class UArrowComponent;
class USceneComponent;

UCLASS()
class AFurnitureMarkerActor : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AFurnitureMarkerActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Furniture Marker")
	USceneComponent* SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Furniture Marker")
	UBoxComponent* InteractionBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Furniture Marker")
	UBoxComponent* SeatTriggerBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Furniture Marker")
	UBoxComponent* ObstacleBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Furniture Marker")
	UArrowComponent* Arrow;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Furniture Marker")
	USceneComponent* SitPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Furniture Marker")
	USceneComponent* ExitPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Furniture Marker")
	USceneComponent* NPCStandPoint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture Marker")
	FString FurnitureID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture Marker")
	FString FurnitureType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture Marker")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture Marker")
	FString RelatedTableID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture Marker")
	TArray<FString> FurnitureTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture Marker")
	bool bIsOccupied;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture Marker")
	bool bCanSit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture Marker")
	int32 Priority;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture Marker|NPC Seat")
	bool bAllowNPCSit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture Marker|NPC Seat")
	FVector NPCSitLocationOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture Marker|NPC Seat")
	FRotator NPCSitRotationOffset;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Furniture Marker")
	FTransform GetSitTransform() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Furniture Marker")
	FTransform GetExitTransform() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Furniture Marker")
	FTransform GetNPCStandTransform() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Furniture Marker")
	FTransform GetNPCSitTransform() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Furniture Marker|Occupancy")
	bool IsSeatAvailableFor(AActor* Actor) const;

	UFUNCTION(BlueprintCallable, Category = "Furniture Marker|Occupancy")
	bool TryReserveSeat(AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Furniture Marker|Occupancy")
	void ReleaseSeat(AActor* Actor);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Furniture Marker|Occupancy")
	bool IsReservedBy(AActor* Actor) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Furniture Marker")
	bool IsActorInInteractionRange(AActor* Actor) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Furniture Marker")
	bool IsActorInSeatRange(AActor* Actor) const;

	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual FText GetInteractionText_Implementation(AActor* Interactor) const override;
	virtual bool Interact_Implementation(AActor* Interactor) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<AActor> SeatOccupant;
};
