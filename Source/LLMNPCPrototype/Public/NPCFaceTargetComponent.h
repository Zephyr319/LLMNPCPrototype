// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCFaceTargetComponent.generated.h"

class AActor;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UNPCFaceTargetComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCFaceTargetComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Face Target")
	AActor* FocusTarget;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Face Target")
	bool bIsFacingTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Target")
	bool bYawOnly;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Target")
	float TurnSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Target")
	float FacingYawOffsetDegrees;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Target")
	bool bSkipWhileMoving;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Target")
	float MovingSpeedThreshold;

	UFUNCTION(BlueprintCallable, Category = "Face Target")
	void StartFacingActor(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category = "Face Target")
	void StopFacingActor();

	UFUNCTION(BlueprintCallable, Category = "Face Target")
	void FaceActorOnce(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Face Target")
	bool IsFacingActor() const;

private:
	bool ShouldSkipFacingForMovement() const;

	void FaceActor(AActor* TargetActor, float DeltaTime, bool bInterpolate, bool bForceYawOnly) const;
};
