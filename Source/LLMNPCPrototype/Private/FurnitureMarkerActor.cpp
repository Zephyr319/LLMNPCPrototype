// Copyright Epic Games, Inc. All Rights Reserved.

#include "FurnitureMarkerActor.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "PlayerSeatComponent.h"

AFurnitureMarkerActor::AFurnitureMarkerActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBox"));
	InteractionBox->SetupAttachment(SceneRoot);
	InteractionBox->SetBoxExtent(FVector(50.0f, 50.0f, 80.0f));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBox->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionBox->SetGenerateOverlapEvents(false);

	FurnitureID = TEXT("Chair_01");
	RelatedTableID = TEXT("Table_01");
	bCanSit = true;
	bIsOccupied = false;
}

bool AFurnitureMarkerActor::CanInteract_Implementation(AActor* Interactor) const
{
	return bCanSit && !bIsOccupied;
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
	return SeatComponent ? SeatComponent->TrySitAtFurniture(this) : false;
}
