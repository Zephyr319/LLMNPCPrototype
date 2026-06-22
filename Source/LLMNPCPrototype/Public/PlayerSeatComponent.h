// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlayerSeatComponent.generated.h"

class AFurnitureMarkerActor;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UPlayerSeatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerSeatComponent();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
	bool bIsSeated;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
	AFurnitureMarkerActor* CurrentSeat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
	FString CurrentSeatID;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
	FString CurrentTableID;

	UFUNCTION(BlueprintCallable, Category = "Seat")
	bool TrySitAtFurniture(AFurnitureMarkerActor* Seat);

	UFUNCTION(BlueprintCallable, Category = "Seat")
	bool LeaveSeat();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Seat")
	FString GetPlayerPostureState() const;
};
