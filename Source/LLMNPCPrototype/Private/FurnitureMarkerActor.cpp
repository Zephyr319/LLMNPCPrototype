// Copyright Epic Games, Inc. All Rights Reserved.

#include "FurnitureMarkerActor.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "PlayerSeatComponent.h"

AFurnitureMarkerActor::AFurnitureMarkerActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBox"));
	InteractionBox->SetupAttachment(SceneRoot);
	InteractionBox->SetBoxExtent(FVector(80.0f, 80.0f, 100.0f));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBox->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InteractionBox->SetGenerateOverlapEvents(true);

	SeatTriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("SeatTriggerBox"));
	SeatTriggerBox->SetupAttachment(SceneRoot);
	SeatTriggerBox->SetBoxExtent(FVector(50.0f, 50.0f, 80.0f));
	SeatTriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SeatTriggerBox->SetCollisionObjectType(ECC_WorldDynamic);
	SeatTriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	SeatTriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SeatTriggerBox->SetGenerateOverlapEvents(true);

	ObstacleBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ObstacleBox"));
	ObstacleBox->SetupAttachment(SceneRoot);
	ObstacleBox->SetBoxExtent(FVector(50.0f, 50.0f, 60.0f));
	ObstacleBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ObstacleBox->SetCollisionObjectType(ECC_WorldStatic);
	ObstacleBox->SetCollisionResponseToAllChannels(ECR_Block);
	ObstacleBox->SetGenerateOverlapEvents(false);
	ObstacleBox->SetCanEverAffectNavigation(true);

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(SceneRoot);

	SitPoint = CreateDefaultSubobject<USceneComponent>(TEXT("SitPoint"));
	SitPoint->SetupAttachment(SceneRoot);
	SitPoint->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f));

	ExitPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ExitPoint"));
	ExitPoint->SetupAttachment(SceneRoot);
	ExitPoint->SetRelativeLocation(FVector(100.0f, 0.0f, 0.0f));

	NPCStandPoint = CreateDefaultSubobject<USceneComponent>(TEXT("NPCStandPoint"));
	NPCStandPoint->SetupAttachment(SceneRoot);
	NPCStandPoint->SetRelativeLocation(FVector(120.0f, 0.0f, 0.0f));

	FurnitureID = TEXT("Furniture_01");
	FurnitureType = TEXT("chair");
	DisplayName = FText::FromString(TEXT("Furniture 01"));
	RelatedTableID = TEXT("");
	FurnitureTags.Reset();
	bIsOccupied = false;
	bCanSit = true;
	Priority = 0;
	bAllowNPCSit = true;
	NPCSitLocationOffset = FVector::ZeroVector;
	NPCSitRotationOffset = FRotator::ZeroRotator;
	SeatOccupant = nullptr;
}

FTransform AFurnitureMarkerActor::GetSitTransform() const
{
	return SitPoint ? SitPoint->GetComponentTransform() : GetActorTransform();
}

FTransform AFurnitureMarkerActor::GetExitTransform() const
{
	return ExitPoint ? ExitPoint->GetComponentTransform() : GetActorTransform();
}

FTransform AFurnitureMarkerActor::GetNPCStandTransform() const
{
	return NPCStandPoint ? NPCStandPoint->GetComponentTransform() : GetActorTransform();
}

FTransform AFurnitureMarkerActor::GetNPCSitTransform() const
{
	FTransform Result = GetSitTransform();
	Result.AddToTranslation(Result.TransformVectorNoScale(NPCSitLocationOffset));
	Result.ConcatenateRotation(NPCSitRotationOffset.Quaternion());
	Result.NormalizeRotation();
	return Result;
}

bool AFurnitureMarkerActor::IsSeatAvailableFor(AActor* Actor) const
{
	if (!bCanSit || !Actor)
	{
		return false;
	}

	if (SeatOccupant)
	{
		return SeatOccupant == Actor;
	}

	return !bIsOccupied;
}

bool AFurnitureMarkerActor::TryReserveSeat(AActor* Actor)
{
	if (!IsSeatAvailableFor(Actor))
	{
		return false;
	}

	SeatOccupant = Actor;
	bIsOccupied = true;
	UE_LOG(LogTemp, Log, TEXT("[Seat] reserved. Seat=%s Occupant=%s"), *FurnitureID, *Actor->GetName());
	return true;
}

void AFurnitureMarkerActor::ReleaseSeat(AActor* Actor)
{
	if (SeatOccupant && Actor && SeatOccupant != Actor)
	{
		return;
	}

	const FString OccupantName = SeatOccupant ? SeatOccupant->GetName() : TEXT("legacy_or_unknown");
	SeatOccupant = nullptr;
	bIsOccupied = false;
	UE_LOG(LogTemp, Log, TEXT("[Seat] released. Seat=%s Occupant=%s"), *FurnitureID, *OccupantName);
}

bool AFurnitureMarkerActor::IsReservedBy(AActor* Actor) const
{
	return Actor && SeatOccupant == Actor;
}

bool AFurnitureMarkerActor::IsActorInInteractionRange(AActor* Actor) const
{
	return Actor && InteractionBox && InteractionBox->IsOverlappingActor(Actor);
}

bool AFurnitureMarkerActor::IsActorInSeatRange(AActor* Actor) const
{
	return Actor && SeatTriggerBox && SeatTriggerBox->IsOverlappingActor(Actor);
}

bool AFurnitureMarkerActor::CanInteract_Implementation(AActor* Interactor) const
{
	return IsSeatAvailableFor(Interactor);
}

FText AFurnitureMarkerActor::GetInteractionText_Implementation(AActor* Interactor) const
{
	return CanInteract_Implementation(Interactor)
		? FText::FromString(TEXT("F 座る"))
		: FText::GetEmpty();
}

bool AFurnitureMarkerActor::Interact_Implementation(AActor* Interactor)
{
	if (!CanInteract_Implementation(Interactor) || !Interactor)
	{
		return false;
	}

	UPlayerSeatComponent* SeatComponent = Interactor->FindComponentByClass<UPlayerSeatComponent>();
	if (!SeatComponent)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red, TEXT("PlayerSeatComponent not found"));
		}
		return false;
	}

	return SeatComponent->TrySitAtFurniture(this);
}
