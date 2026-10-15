// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlayerSeatComponent.generated.h"

class AFurnitureMarkerActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPlayerSeatChangedSignature, FString, SeatID, FString, TableID);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UPlayerSeatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerSeatComponent();

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seat", meta = (ClampMin = "30.0"))
	float AutoLeaveSeatDistance;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
	bool bIsSeated;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
	AFurnitureMarkerActor* CurrentSeat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
	FString CurrentSeatID;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
	FString CurrentTableID;

	UPROPERTY(BlueprintAssignable, Category = "Seat")
	FPlayerSeatChangedSignature OnPlayerSeated;

	UPROPERTY(BlueprintAssignable, Category = "Seat")
	FPlayerSeatChangedSignature OnPlayerLeftSeat;

	UFUNCTION(BlueprintCallable, Category = "Seat")
	bool TrySitAtFurniture(AFurnitureMarkerActor* Seat);

	UFUNCTION(BlueprintCallable, Category = "Seat")
	bool LeaveSeat();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Seat")
	FString GetPlayerPostureState() const;

private:
	FTransform PreSeatTransform;

	bool bHasPreSeatTransform;
};
