// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerSeatComponent.h"

#include "FurnitureMarkerActor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"

UPlayerSeatComponent::UPlayerSeatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	AutoLeaveSeatDistance = 140.0f;
	bIsSeated = false;
	CurrentSeat = nullptr;
	CurrentSeatID = TEXT("");
	CurrentTableID = TEXT("");
	bHasPreSeatTransform = false;
}

void UPlayerSeatComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bIsSeated || !CurrentSeat)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const FTransform SitTransform = CurrentSeat->GetSitTransform();
	Owner->SetActorLocationAndRotation(
		SitTransform.GetLocation(),
		SitTransform.Rotator(),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
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
		if (!bIsSeated)
		{
			PreSeatTransform = Owner->GetActorTransform();
			bHasPreSeatTransform = true;
		}

		const FTransform SitTransform = Seat->GetSitTransform();
		if (ACharacter* CharacterOwner = Cast<ACharacter>(Owner))
		{
			if (UCharacterMovementComponent* MovementComponent = CharacterOwner->GetCharacterMovement())
			{
				MovementComponent->StopMovementImmediately();
				MovementComponent->DisableMovement();
			}

			if (AController* Controller = CharacterOwner->GetController())
			{
				Controller->SetControlRotation(SitTransform.Rotator());
			}
		}

		Owner->SetActorLocationAndRotation(
			SitTransform.GetLocation(),
			SitTransform.Rotator(),
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
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
	AFurnitureMarkerActor* LeavingSeat = CurrentSeat;

	if (LeavingSeat)
	{
		LeavingSeat->bIsOccupied = false;
	}

	bIsSeated = false;
	CurrentSeat = nullptr;
	CurrentSeatID = TEXT("");
	CurrentTableID = TEXT("");
	const bool bUsedPreSeatTransform = bHasPreSeatTransform;

	if (ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner()))
	{
		const bool bUsePreSeatTransform = bUsedPreSeatTransform;
		if (bUsePreSeatTransform || LeavingSeat)
		{
			const FTransform ExitTransform = bUsePreSeatTransform
				? PreSeatTransform
				: LeavingSeat->GetExitTransform();
			const bool bTeleported = CharacterOwner->TeleportTo(
				ExitTransform.GetLocation(),
				ExitTransform.Rotator(),
				false,
				false);
			if (!bTeleported)
			{
				CharacterOwner->SetActorLocationAndRotation(
					ExitTransform.GetLocation(),
					ExitTransform.Rotator(),
					false,
					nullptr,
					ETeleportType::TeleportPhysics);
			}

			if (AController* Controller = CharacterOwner->GetController())
			{
				Controller->SetControlRotation(ExitTransform.Rotator());
			}
		}

		if (UCharacterMovementComponent* MovementComponent = CharacterOwner->GetCharacterMovement())
		{
			MovementComponent->SetMovementMode(MOVE_Walking);
			MovementComponent->bForceNextFloorCheck = true;
		}
	}
	bHasPreSeatTransform = false;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[Seat] player left fixed seat view. Seat=%s Table=%s ExitSource=%s"),
		*LeavingSeatID,
		*LeavingTableID,
		bUsedPreSeatTransform ? TEXT("pre_seat") : TEXT("seat_exit"));
	OnPlayerLeftSeat.Broadcast(LeavingSeatID, LeavingTableID);
	return true;
}

FString UPlayerSeatComponent::GetPlayerPostureState() const
{
	return bIsSeated ? TEXT("seated") : TEXT("standing");
}
