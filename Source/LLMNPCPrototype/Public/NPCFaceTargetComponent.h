// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCFaceTargetComponent.generated.h"

class AActor;
class UNPCAutonomyComponent;
class UVoicevoxTTSClient;

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

	// Speech owns a separate request: stopping an action must not cancel an ongoing reply.
	void StartSpeechFacing(AActor* TargetActor, UVoicevoxTTSClient* VoiceSource, float SubtitleSeconds);

	UFUNCTION(BlueprintCallable, Category = "Face Target")
	void FaceActorOnce(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Face Target")
	bool IsFacingActor() const;

private:
	TWeakObjectPtr<AActor> SpeechFacingTarget;
	TWeakObjectPtr<UVoicevoxTTSClient> SpeechVoiceSource;
	TWeakObjectPtr<UNPCAutonomyComponent> SpeechAutonomyComponent;
	double SpeechFacingFallbackUntil = 0.0;
	bool bSpeechFacingActive = false;
	FString LastSpeechFacingState;

	FString GetSpeechFacingBlockReason() const;
	void StopSpeechFacing(const TCHAR* Reason);

	bool ShouldSkipFacingForMovement() const;

	void FaceActor(AActor* TargetActor, float DeltaTime, bool bInterpolate, bool bForceYawOnly) const;
};
