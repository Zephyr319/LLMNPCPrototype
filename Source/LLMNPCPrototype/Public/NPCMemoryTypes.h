// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NPCMemoryTypes.generated.h"

USTRUCT(BlueprintType)
struct FNPCInteractionMemorySignal
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Interaction")
	bool bShouldRemember = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Interaction")
	FName EventTag = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Interaction")
	FString Summary;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Interaction")
	FString PlayerUtterance;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Interaction")
	TArray<FName> CueTags;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Interaction")
	float Valence = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Interaction")
	float Arousal = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Interaction")
	float Importance = 0.0f;
};

USTRUCT(BlueprintType)
struct FNPCMemoryEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory")
	FString MemoryID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Memory")
	FName EventTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Memory")
	FString Summary;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Memory")
	FName LocationTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Memory")
	TArray<FName> CueTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Memory", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Valence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Memory", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Arousal = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Memory", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Importance = 0.5f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory")
	FDateTime CreatedAtUtc;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory")
	FDateTime LastRecalledAtUtc;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory")
	int32 RecallCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Memory")
	bool bProtected = false;
};

USTRUCT(BlueprintType)
struct FNPCRecallScore
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Recall")
	float AttentionAvailability = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Recall")
	float CueRelevance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Recall")
	float MemoryStrength = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Recall")
	float TimeFactor = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Recall")
	float RecallProbability = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Recall")
	float RandomRoll = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Recall")
	bool bTriggered = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Recall")
	bool bForced = false;
};
