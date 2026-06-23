// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CafeEntranceTriggerActor.generated.h"

class ACafeServiceManagerActor;
class UBoxComponent;
class UPrimitiveComponent;
class USceneComponent;

UCLASS()
class ACafeEntranceTriggerActor : public AActor
{
	GENERATED_BODY()

public:
	ACafeEntranceTriggerActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UBoxComponent* TriggerBox;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Entrance")
	ACafeServiceManagerActor* CafeServiceManager;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Entrance")
	bool bTriggerOnlyOnce;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cafe Entrance")
	bool bTriggered;

private:
	UFUNCTION()
	void OnTriggerBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	bool IsLocalPlayerPawn(AActor* OtherActor) const;
};
