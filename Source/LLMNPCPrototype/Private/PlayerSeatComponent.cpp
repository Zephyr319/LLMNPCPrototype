// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerSeatComponent.h"

#include "FurnitureMarkerActor.h"

UPlayerSeatComponent::UPlayerSeatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	bIsSeated = false;
	CurrentSeat = nullptr;
	CurrentSeatID = TEXT("");
	CurrentTableID = TEXT("");
}

bool UPlayerSeatComponent::TrySitAtFurniture(AFurnitureMarkerActor* Seat)
{
	if (!Seat || !Seat->bCanSit)
	{
		return false;
	}

	if (bIsSeated && CurrentSeat == Seat)
	{
		return true;
	}

	if (Seat->bIsOccupied)
	{
		return false;
	}

	if (CurrentSeat)
	{
		CurrentSeat->bIsOccupied = false;
	}

	AActor* Owner = GetOwner();
	if (Owner)
	{
		FHitResult SweepHit;
		Owner->SetActorLocation(Seat->GetActorLocation(), true, &SweepHit);
		Owner->SetActorRotation(Seat->GetActorRotation());
	}

	bIsSeated = true;
	CurrentSeat = Seat;
	CurrentSeatID = Seat->FurnitureID;
	CurrentTableID = Seat->RelatedTableID;
	Seat->bIsOccupied = true;
	OnPlayerSeated.Broadcast(CurrentSeatID, CurrentTableID);
	return true;
}

bool UPlayerSeatComponent::LeaveSeat()
{
	if (!bIsSeated)
	{
		return false;
	}

	const FString LeavingSeatID = CurrentSeatID;
	const FString LeavingTableID = CurrentTableID;

	if (CurrentSeat)
	{
		CurrentSeat->bIsOccupied = false;
	}

	bIsSeated = false;
	CurrentSeat = nullptr;
	CurrentSeatID = TEXT("");
	CurrentTableID = TEXT("");
	OnPlayerLeftSeat.Broadcast(LeavingSeatID, LeavingTableID);
	return true;
}

FString UPlayerSeatComponent::GetPlayerPostureState() const
{
	return bIsSeated ? TEXT("seated") : TEXT("standing");
}
