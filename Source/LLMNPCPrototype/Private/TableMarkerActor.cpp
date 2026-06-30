// Copyright Epic Games, Inc. All Rights Reserved.

#include "TableMarkerActor.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "NPCActionQueueComponent.h"

ATableMarkerActor::ATableMarkerActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->SetupAttachment(SceneRoot);
	TriggerBox->SetBoxExtent(FVector(120.0f, 120.0f, 80.0f));
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionObjectType(ECC_WorldDynamic);
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerBox->SetGenerateOverlapEvents(true);

	ObstacleBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ObstacleBox"));
	ObstacleBox->SetupAttachment(SceneRoot);
	ObstacleBox->SetBoxExtent(FVector(80.0f, 80.0f, 60.0f));
	ObstacleBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ObstacleBox->SetCollisionObjectType(ECC_WorldStatic);
	ObstacleBox->SetCollisionResponseToAllChannels(ECR_Block);
	ObstacleBox->SetGenerateOverlapEvents(false);
	ObstacleBox->SetCanEverAffectNavigation(true);

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(SceneRoot);

	TableID = TEXT("Table_01");
	TableName = FText::FromString(TEXT("Table 01"));
	bIsOccupied = false;
	SeatCount = 2;
	Priority = 0;
	NPCActorForTableArea = nullptr;
	bShowTableAreaOnScreenDebug = false;

	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ATableMarkerActor::OnTriggerBoxBeginOverlap);
	TriggerBox->OnComponentEndOverlap.AddDynamic(this, &ATableMarkerActor::OnTriggerBoxEndOverlap);
}

const FString& ATableMarkerActor::GetTableID() const
{
	return TableID;
}

const FText& ATableMarkerActor::GetTableName() const
{
	return TableName;
}

bool ATableMarkerActor::IsTableOccupied() const
{
	return bIsOccupied;
}

const TArray<FString>& ATableMarkerActor::GetTableTags() const
{
	return TableTags;
}

int32 ATableMarkerActor::GetSeatCount() const
{
	return SeatCount;
}

int32 ATableMarkerActor::GetPriority() const
{
	return Priority;
}

void ATableMarkerActor::NotifyPlayerEnteredThisTableArea(AActor* OtherActor)
{
	if (!IsLocalPlayerPawn(OtherActor))
	{
		return;
	}

	UNPCActionQueueComponent* ActionQueueComponent = ResolveActionQueueComponent();
	if (!ActionQueueComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TableArea] NPCActionQueueComponent not found for table %s"), *TableID);
		return;
	}

	ActionQueueComponent->NotifyPlayerEnteredTableArea(TableID);
}

void ATableMarkerActor::NotifyPlayerLeftThisTableArea(AActor* OtherActor)
{
	if (!IsLocalPlayerPawn(OtherActor))
	{
		return;
	}

	UNPCActionQueueComponent* ActionQueueComponent = ResolveActionQueueComponent();
	if (!ActionQueueComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TableArea] NPCActionQueueComponent not found for table %s"), *TableID);
		return;
	}

	ActionQueueComponent->NotifyPlayerLeftTableArea(TableID);
}

void ATableMarkerActor::OnTriggerBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!IsLocalPlayerPawn(OtherActor))
	{
		return;
	}

	if (bShowTableAreaOnScreenDebug && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green, FString::Printf(TEXT("Enter %s"), *TableID));
	}

	NotifyPlayerEnteredThisTableArea(OtherActor);
}

void ATableMarkerActor::OnTriggerBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!IsLocalPlayerPawn(OtherActor))
	{
		return;
	}

	if (bShowTableAreaOnScreenDebug && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow, FString::Printf(TEXT("Leave %s"), *TableID));
	}

	NotifyPlayerLeftThisTableArea(OtherActor);
}

bool ATableMarkerActor::IsLocalPlayerPawn(AActor* OtherActor) const
{
	const APawn* OtherPawn = Cast<APawn>(OtherActor);
	return OtherPawn && OtherPawn->IsPlayerControlled() && OtherPawn->IsLocallyControlled();
}

UNPCActionQueueComponent* ATableMarkerActor::ResolveActionQueueComponent() const
{
	if (NPCActorForTableArea)
	{
		if (UNPCActionQueueComponent* ActionQueueComponent = NPCActorForTableArea->FindComponentByClass<UNPCActionQueueComponent>())
		{
			return ActionQueueComponent;
		}
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* CandidateActor = *It;
		if (!CandidateActor)
		{
			continue;
		}

		if (UNPCActionQueueComponent* ActionQueueComponent = CandidateActor->FindComponentByClass<UNPCActionQueueComponent>())
		{
			return ActionQueueComponent;
		}
	}

	return nullptr;
}
