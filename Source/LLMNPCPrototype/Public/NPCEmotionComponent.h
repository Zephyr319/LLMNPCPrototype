// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCEmotionTypes.h"
#include "NPCEmotionComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FNPCEmotionStateChangedSignature,
	const FNPCEmotionState&,
	EmotionState);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class LLMNPCPROTOTYPE_API UNPCEmotionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCEmotionComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Emotion|Baseline", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float BaselineValence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Emotion|Baseline", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BaselineArousal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Emotion|Timing", meta = (ClampMin = "1.0"))
	float ShortTermHalfLifeSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Emotion|Timing", meta = (ClampMin = "0.01"))
	float MoodHalfLifeHours;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Emotion")
	FNPCEmotionState CurrentState;

	UPROPERTY(BlueprintAssignable, Category = "NPC Emotion")
	FNPCEmotionStateChangedSignature OnEmotionStateChanged;

	UFUNCTION(BlueprintCallable, Category = "NPC Emotion")
	void RefreshStateFromRealTime();

	UFUNCTION(BlueprintCallable, Category = "NPC Emotion")
	void ApplyAppraisal(
		const FString& EmotionLabel,
		float ValenceDelta,
		float ArousalDelta,
		float MoodDelta,
		const FString& ExpressionIntent,
		const FString& Interpretation);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Emotion")
	FNPCEmotionState GetCurrentState() const;

protected:
	virtual void BeginPlay() override;
};
