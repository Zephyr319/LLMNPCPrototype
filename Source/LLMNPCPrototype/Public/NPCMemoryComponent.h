// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCMemoryTypes.h"
#include "NPCMemoryComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FNPCMemoryRecordedSignature,
	const FNPCMemoryEntry&,
	Memory);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FNPCMemoryRecalledSignature,
	const FNPCMemoryEntry&,
	Memory,
	const FNPCRecallScore&,
	Score);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FNPCMemoryRecallEvaluatedSignature,
	const FNPCMemoryEntry&,
	Memory,
	const FNPCRecallScore&,
	Score,
	bool,
	bTriggered);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class LLMNPCPROTOTYPE_API UNPCMemoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCMemoryComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Memory", meta = (ClampMin = "1"))
	int32 MaxMemoryEntries;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Memory|Debug")
	bool bLogMemoryEvents;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Memory|Persistence")
	bool bPersistMemories;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Memory|Persistence")
	FString MemoryFileName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Persistence")
	FString MemoryFilePath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Memory|Recall", meta = (ClampMin = "0.01"))
	float RecallHalfLifeHours;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Memory|Recall", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NoCueBaselineRelevance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Memory|Recall", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ImportanceStrengthWeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Memory|Recall|Validation")
	bool bRunRecallValidationWhenPlayerSits;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory")
	TArray<FNPCMemoryEntry> Memories;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Recall")
	FString LastRecalledMemoryID;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Memory|Recall")
	FNPCRecallScore LastRecallScore;

	UPROPERTY(BlueprintAssignable, Category = "NPC Memory")
	FNPCMemoryRecordedSignature OnMemoryRecorded;

	UPROPERTY(BlueprintAssignable, Category = "NPC Memory|Recall")
	FNPCMemoryRecalledSignature OnMemoryRecalled;

	UPROPERTY(BlueprintAssignable, Category = "NPC Memory|Recall")
	FNPCMemoryRecallEvaluatedSignature OnMemoryRecallEvaluated;

	UFUNCTION(BlueprintCallable, Category = "NPC Memory")
	FString RecordMemoryEvent(
		FName EventTag,
		const FString& Summary,
		FName LocationTag,
		const TArray<FName>& CueTags,
		float Valence,
		float Arousal,
		float Importance,
		bool bProtected);

	UFUNCTION(BlueprintCallable, Category = "NPC Memory")
	bool MarkMemoryRecalled(const FString& MemoryID);

	UFUNCTION(BlueprintCallable, Category = "NPC Memory|Recall")
	bool TryRecallBestMemory(
		const TArray<FName>& CurrentCueTags,
		float AttentionAvailability,
		bool bForceRecall,
		FNPCMemoryEntry& OutMemory,
		FNPCRecallScore& OutScore);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Memory|Recall")
	FNPCRecallScore CalculateRecallScore(
		const FNPCMemoryEntry& Memory,
		const TArray<FName>& CurrentCueTags,
		float AttentionAvailability) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Memory")
	bool FindMemoryByID(const FString& MemoryID, FNPCMemoryEntry& OutMemory) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Memory")
	TArray<FNPCMemoryEntry> GetRecentMemories(int32 MaximumCount) const;

	UFUNCTION(BlueprintCallable, Category = "NPC Memory|Persistence")
	bool SaveMemoriesToDisk();

	UFUNCTION(BlueprintCallable, Category = "NPC Memory|Persistence")
	bool LoadMemoriesFromDisk();

protected:
	virtual void BeginPlay() override;

private:
	void PruneToCapacity();
	float CalculateCueRelevance(const TArray<FName>& MemoryCueTags, const TArray<FName>& CurrentCueTags) const;
};
