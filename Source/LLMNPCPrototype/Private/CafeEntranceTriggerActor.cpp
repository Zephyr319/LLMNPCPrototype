// Copyright Epic Games, Inc. All Rights Reserved.

#include "CafeEntranceTriggerActor.h"

#include "CafeServiceManagerActor.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"

ACafeEntranceTriggerActor::ACafeEntranceTriggerActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->SetupAttachment(SceneRoot);
	TriggerBox->SetBoxExtent(FVector(200.0f, 200.0f, 120.0f));
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionObjectType(ECC_WorldDynamic);
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerBox->SetGenerateOverlapEvents(true);

	CafeServiceManager = nullptr;
	bTriggerOnlyOnce = true;
	bTriggered = false;

	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ACafeEntranceTriggerActor::OnTriggerBoxBeginOverlap);
}

void ACafeEntranceTriggerActor::OnTriggerBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!IsLocalPlayerPawn(OtherActor))
	{
		return;
	}

	if (bTriggerOnlyOnce && bTriggered)
	{
		return;
	}

	bTriggered = true;

	if (CafeServiceManager)
	{
		CafeServiceManager->StartServiceForPlayer(OtherActor);
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green, TEXT("Cafe entrance triggered"));
	}
}

bool ACafeEntranceTriggerActor::IsLocalPlayerPawn(AActor* OtherActor) const
{
	const APawn* OtherPawn = Cast<APawn>(OtherActor);
	return OtherPawn && OtherPawn->IsPlayerControlled() && OtherPawn->IsLocallyControlled();
}
