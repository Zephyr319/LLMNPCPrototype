// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterablePlantActor.h"

#include "CafeServiceManagerActor.h"
#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "NPCAutonomyComponent.h"

AWaterablePlantActor::AWaterablePlantActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	PlantMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlantMesh"));
	PlantMesh->SetupAttachment(SceneRoot);
	PlantMesh->SetCollisionProfileName(TEXT("BlockAll"));
	PlantMesh->SetCanEverAffectNavigation(true);

	FacingArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("FacingArrow"));
	FacingArrow->SetupAttachment(SceneRoot);
	FacingArrow->SetArrowColor(FColor(60, 190, 90));
	FacingArrow->SetArrowSize(1.25f);
	FacingArrow->SetHiddenInGame(true);

	ObservedPlantActor = nullptr;
	PlantID = TEXT("Plant_01");
	DisplayName = FText::FromString(TEXT("Waterable Plant 01"));
	PlantTags = { TEXT("watering"), TEXT("waterable_plant") };
	WaterNeed = 75.0f;
	NeedsWaterThreshold = 50.0f;
	bNeedsWater = true;
	bEnabled = true;
	bReserved = false;
	Priority = 0;
	ApproachDistance = 95.0f;
	bOnlyTargetWhenNeedsWater = true;
	bSyncAutonomyWorldState = true;
	AutonomyWorldStateTag = FName(TEXT("flower_needs_water"));
}

void AWaterablePlantActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshDerivedState();

	if (PlantTags.Num() == 0)
	{
		PlantTags = { TEXT("watering"), TEXT("waterable_plant") };
	}
}

void AWaterablePlantActor::BeginPlay()
{
	Super::BeginPlay();
	RefreshDerivedState();
	RefreshAutonomyWorldState(false);

	AActor* TargetActor = GetPlantTargetActor();
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[WaterablePlant] Registered ID=%s Marker=%s Target=%s Location=%s Need=%.1f NeedsWater=%s Priority=%d"),
		*PlantID,
		*GetName(),
		TargetActor ? *TargetActor->GetName() : TEXT("None"),
		*GetPlantTargetLocation().ToCompactString(),
		WaterNeed,
		bNeedsWater ? TEXT("true") : TEXT("false"),
		Priority);
}

AActor* AWaterablePlantActor::GetPlantTargetActor() const
{
	return IsValid(ObservedPlantActor) && ObservedPlantActor != this
		? ObservedPlantActor
		: const_cast<AWaterablePlantActor*>(this);
}

FVector AWaterablePlantActor::GetPlantTargetLocation() const
{
	AActor* TargetActor = GetPlantTargetActor();
	if (!TargetActor)
	{
		return GetActorLocation();
	}

	FVector BoundsCenter = TargetActor->GetActorLocation();
	FVector BoundsExtent = FVector::ZeroVector;
	TargetActor->GetActorBounds(false, BoundsCenter, BoundsExtent);
	return BoundsCenter;
}

bool AWaterablePlantActor::HasTagString(const FString& Tag) const
{
	const FString TrimmedTag = Tag.TrimStartAndEnd();
	if (TrimmedTag.IsEmpty())
	{
		return false;
	}

	return PlantTags.ContainsByPredicate([&TrimmedTag](const FString& PlantTag)
	{
		return PlantTag.TrimStartAndEnd().Equals(TrimmedTag, ESearchCase::IgnoreCase);
	});
}

bool AWaterablePlantActor::NeedsWater() const
{
	return bEnabled && bNeedsWater;
}

bool AWaterablePlantActor::IsAvailableForTag(const FString& Tag) const
{
	if (!bEnabled || bReserved || !HasTagString(Tag))
	{
		return false;
	}

	return !bOnlyTargetWhenNeedsWater || NeedsWater();
}

void AWaterablePlantActor::SetWaterNeed(float NewWaterNeed, bool bMeaningfulChange)
{
	const bool bPreviouslyNeededWater = bNeedsWater;
	const float PreviousWaterNeed = WaterNeed;
	WaterNeed = FMath::Clamp(NewWaterNeed, 0.0f, 100.0f);
	RefreshDerivedState();

	if (!FMath::IsNearlyEqual(PreviousWaterNeed, WaterNeed) || bPreviouslyNeededWater != bNeedsWater)
	{
		OnPlantStateChanged.Broadcast(this, WaterNeed, bNeedsWater);
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[WaterablePlant] state changed. ID=%s WaterNeed=%.1f NeedsWater=%d"),
			*PlantID,
			WaterNeed,
			bNeedsWater ? 1 : 0);
	}

	if (bPreviouslyNeededWater != bNeedsWater)
	{
		RefreshAutonomyWorldState(bMeaningfulChange);
	}
}

void AWaterablePlantActor::AddWaterNeed(float Amount, bool bMeaningfulChange)
{
	SetWaterNeed(WaterNeed + Amount, bMeaningfulChange);
}

void AWaterablePlantActor::WaterPlant(float Amount, bool bMeaningfulChange)
{
	SetWaterNeed(WaterNeed - FMath::Max(Amount, 0.0f), bMeaningfulChange);
}

void AWaterablePlantActor::RefreshAutonomyWorldState(bool bMeaningfulChange) const
{
	if (!bSyncAutonomyWorldState || AutonomyWorldStateTag.IsNone())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bool bAnyPlantNeedsWater = false;
	for (TActorIterator<AWaterablePlantActor> It(World); It; ++It)
	{
		const AWaterablePlantActor* Plant = *It;
		if (Plant
			&& Plant->bSyncAutonomyWorldState
			&& Plant->AutonomyWorldStateTag == AutonomyWorldStateTag
			&& Plant->NeedsWater())
		{
			bAnyPlantNeedsWater = true;
			break;
		}
	}

	for (TActorIterator<ACafeServiceManagerActor> It(World); It; ++It)
	{
		ACafeServiceManagerActor* ServiceManager = *It;
		if (ServiceManager)
		{
			if (UNPCAutonomyComponent* Autonomy = ServiceManager->FindComponentByClass<UNPCAutonomyComponent>())
			{
				Autonomy->SetWorldStateTag(AutonomyWorldStateTag, bAnyPlantNeedsWater, bMeaningfulChange);
			}
		}
	}
}

void AWaterablePlantActor::RefreshDerivedState()
{
	WaterNeed = FMath::Clamp(WaterNeed, 0.0f, 100.0f);
	NeedsWaterThreshold = FMath::Clamp(NeedsWaterThreshold, 0.0f, 100.0f);
	bNeedsWater = WaterNeed + KINDA_SMALL_NUMBER >= NeedsWaterThreshold;
}
