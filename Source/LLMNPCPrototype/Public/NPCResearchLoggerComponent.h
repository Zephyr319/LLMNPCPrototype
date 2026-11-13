// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCEmotionTypes.h"
#include "NPCMemoryTypes.h"
#include "NPCResearchLoggerComponent.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class LLMNPCPROTOTYPE_API UNPCResearchLoggerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCResearchLoggerComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Research Log")
	bool bLoggingEnabled;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Research Log")
	FString OutputDirectoryName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Research Log")
	FString SessionID;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Research Log")
	FString LogFilePath;

	UFUNCTION(BlueprintCallable, Category = "NPC Research Log")
	void RecordStructuredEvent(const FString& EventType, const FString& PayloadJson = TEXT("{}"));

	// --- Experiment harness: every event carries seq / world_time / trial_id / condition / interaction_id ---
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Research Log|Trial")
	FString TrialID;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Research Log|Trial")
	FString TrialCondition;

	// Incremented each time the player speaks; 0 until the first utterance of the session.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Research Log|Trial")
	int32 InteractionID = 0;

	void BeginTrial(const FString& InTrialID, const FString& InCondition);
	void EndTrial(const FString& Note);
	int32 BeginInteraction();

	// The logger of the running session, for code that has no direct reference to it (LLM client, service manager).
	static UNPCResearchLoggerComponent* GetActiveLogger();
	static void RecordGlobalEvent(const FString& EventType, const FString& PayloadJson);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleMemoryRecorded(const FNPCMemoryEntry& Memory);

	UFUNCTION()
	void HandleMemoryRecallEvaluated(
		const FNPCMemoryEntry& Memory,
		const FNPCRecallScore& Score,
		bool bTriggered);

	UFUNCTION()
	void HandleEmotionStateChanged(const FNPCEmotionState& EmotionState);

	UFUNCTION()
	void HandleDecisionMade(const FString& ActivityID, const FString& Reason);

	UFUNCTION()
	void HandleActivityStarted(const FString& ActivityID, const FString& Reason);

	UFUNCTION()
	void HandleActivityFinished(const FString& ActivityID, bool bInterrupted);

	int64 EventSequence = 0;
};
