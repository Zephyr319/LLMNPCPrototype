// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NPCEmotionTypes.generated.h"

USTRUCT(BlueprintType)
struct FNPCEmotionState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Emotion")
	FString EmotionLabel = TEXT("neutral");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Emotion", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float ShortTermValence = 0.1f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Emotion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Arousal = 0.35f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Emotion", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float MoodValence = 0.1f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Emotion")
	FString ExpressionIntent = TEXT("neutral");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Emotion")
	FString LastInterpretation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Emotion")
	FDateTime UpdatedAtUtc;
};
