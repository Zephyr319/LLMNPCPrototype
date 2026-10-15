// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CafeServiceManagerActor.h"
#include "Components/ActorComponent.h"
#include "NPCAutonomyTypes.h"
#include "NPCMemoryTypes.h"
#include "NPCAutonomyComponent.generated.h"

class ULLMClient;
class FJsonObject;
class UNPCActionQueueComponent;
class UNPCAutonomyActionSet;
class UNPCEmotionComponent;
class UNPCMemoryComponent;
class UNPCResearchLoggerComponent;
class UAnimSequenceBase;

enum class ENPCAutonomyRuntimePhase : uint8
{
	None,
	EmotionReaction,
	BuiltInQueue,
	BlueprintPreMove,
	BlueprintExecuting,
	BlueprintReturnMove
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FNPCAutonomyDecisionMadeSignature,
	const FString&,
	ActivityID,
	const FString&,
	Reason);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FNPCAutonomyActivityFinishedSignature,
	const FString&,
	ActivityID,
	bool,
	bInterrupted);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FNPCAutonomyBlueprintActivityRequestedSignature,
	const FString&,
	ActivityID,
	const FString&,
	TargetSceneTag);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FNPCAutonomyBlueprintActivityAbortedSignature,
	const FString&,
	ActivityID,
	const FString&,
	Reason);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FNPCExpressionIntentChangedSignature,
	const FString&,
	ExpressionIntent,
	const FString&,
	EmotionLabel);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FNPCAutonomyEmbodiedGestureRequestedSignature,
	const FString&,
	BodyChannel,
	const FString&,
	GestureTag);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class LLMNPCPROTOTYPE_API UNPCAutonomyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCAutonomyComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	bool bAutonomyEnabled;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Decision")
	ENPCAutonomyDecisionMode DecisionMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Validation")
	bool bFastValidationMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Validation", meta = (ClampMin = "1.0"))
	float FastValidationMotiveTimeScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Validation")
	bool bShowFastValidationDebug;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Validation")
	bool bForceFirstPhoneActivityForValidation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Validation")
	bool bEnableMultiStepPlanValidationScenario;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Validation")
	FName MultiStepPlanValidationWorldTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Validation")
	bool bAutoInterruptMultiStepValidationPlan;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Validation", meta = (ClampMin = "0.1"))
	float MultiStepValidationInterruptDelaySeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Timing", meta = (ClampMin = "0.1"))
	float EvaluationIntervalSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Timing", meta = (ClampMin = "0.0"))
	float InitialIdleDelaySeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Timing", meta = (ClampMin = "0.0"))
	float DelayAfterActivitySeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Timing", meta = (ClampMin = "0.0"))
	float PostPlanFreeActivitySeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Timing", meta = (ClampMin = "0.0"))
	float DelayAfterPlayerInteractionSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Timing", meta = (ClampMin = "0.0"))
	float DecisionErrorRetrySeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Timing", meta = (ClampMin = "0.0"))
	float MinimumSecondsBetweenLLMDecisions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Timing", meta = (ClampMin = "0.0"))
	float DelayBetweenPlanStepsSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Timing", meta = (ClampMin = "0.0"))
	float WaitDecisionCooldownSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|State")
	FNPCAutonomyMotiveState MotiveState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|State")
	FNPCAutonomyResourceState ResourceState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|State", meta = (ClampMin = "0.0"))
	float BoredomIncreasePerIdleSecond;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|State", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float BoredomDecisionThresholdMin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|State", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float BoredomDecisionThresholdMax;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|State", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float DutyDecisionThreshold;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|State", meta = (ClampMin = "0.0"))
	float DutyIncreasePerSecondWithTask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|State", meta = (ClampMin = "0.0"))
	float DutyDecayPerSecond;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|State", meta = (ClampMin = "0.0"))
	float ServiceTransitionEnergyCost;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|State", meta = (ClampMin = "0.0"))
	float IdleEnergyRecoveryPerSecond;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Plan", meta = (ClampMin = "1", ClampMax = "6"))
	int32 MaxPlanSteps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Plan")
	bool bResumeTaskPlansAfterService;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Ambient")
	bool bEnableAmbientBehaviorLayer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Ambient", meta = (ClampMin = "0.0"))
	float AmbientOpportunitiesPerMinute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Ambient", meta = (ClampMin = "0.0"))
	float MinimumSecondsBetweenAmbientActivities;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Ambient", meta = (ClampMin = "1.0"))
	float MaximumQuietIdleSecondsBeforeAmbient;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|LLM")
	FString AutonomyModelName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Memory")
	bool bEnableMemoryEmotionCycle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Memory|Spontaneous Recall")
	bool bEnableSpontaneousRecall;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Memory|Spontaneous Recall", meta = (ClampMin = "0.0"))
	float BaseSpontaneousRecallOpportunitiesPerMinute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Memory|Spontaneous Recall", meta = (ClampMin = "0.0"))
	float MinimumSecondsBetweenRecallAppraisals;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Memory|Spontaneous Recall", meta = (ClampMin = "0.0"))
	float PostPlanRecallBoostDurationSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Memory|Spontaneous Recall", meta = (ClampMin = "1.0"))
	float PostPlanRecallOpportunityMultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Memory|Validation")
	bool bSeedPositiveMemoryForValidation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Memory|Validation")
	bool bForceFirstValidationRecall;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Memory|Validation")
	bool bWaitForPlayerEncounterBeforeValidation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Emotion|Performance")
	bool bPlayVisibleMemoryReaction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Emotion|Performance")
	bool bEnableConcurrentExpressionOverlay;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Emotion|Performance")
	bool bAllowBlockingStrongMemoryReactions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Emotion|Performance")
	TSoftObjectPtr<UAnimSequenceBase> PositiveMemoryReactionAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Emotion|Performance")
	TSoftObjectPtr<UAnimSequenceBase> NegativeMemoryReactionAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Emotion|Performance")
	TSoftObjectPtr<UAnimSequenceBase> HighArousalNegativeMemoryReactionAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Emotion|Performance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HighArousalNegativeReactionThreshold;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Emotion|Performance")
	TSoftObjectPtr<UAnimSequenceBase> NeutralMemoryReactionAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Emotion|Performance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExpressionMorphStrength;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Emotion|Performance", meta = (ClampMin = "0.1"))
	float ExpressionMorphDurationSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Emotion|Performance", meta = (ClampMin = "0.1"))
	float ConcurrentExpressionLeadDurationSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Emotion|Performance", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ExpressionFadeInDurationSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Emotion|Performance", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float ExpressionFadeOutDurationSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Emotion|Performance")
	bool bShowPlanSurfaceThoughts;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Emotion|Performance", meta = (ClampMin = "0.0", ClampMax = "15.0"))
	float PlanThoughtDelayAfterMemorySeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Expression|Blink")
	bool bEnableAutomaticBlinking;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Expression|Blink", meta = (ClampMin = "0.1"))
	float BlinkIntervalMinSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Expression|Blink", meta = (ClampMin = "0.1"))
	float BlinkIntervalMaxSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Expression|Blink", meta = (ClampMin = "0.02", ClampMax = "0.5"))
	float BlinkClosedDurationSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Expression|Blink", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BlinkStrength;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Expression|Blink", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DoubleBlinkChance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response")
	bool bEnableEmbodiedResponseCoordination;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response")
	bool bPreservePhoneActivityUntilLLMResponse;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response")
	bool bPreserveAutonomyActivityUntilLLMResponse;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float EmbodiedResponseResumeFallbackSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float InteractiveFacingFallbackSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response")
	bool bEnableInternalEmbodiedGestureAnimations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response")
	TSoftObjectPtr<UAnimSequenceBase> StartledReactionAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response")
	TSoftObjectPtr<UAnimSequenceBase> ShushGestureAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response|Startle")
	bool bEnablePlayerSpeechStartle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response|Startle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PlayerSpeechStartleBaseChance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response|Startle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PlayerSpeechStartleFocusWeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response|Startle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PlayerSpeechStartleUnexpectednessWeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response|Startle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PlayerSpeechStartleRearWeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response|Startle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PlayerSpeechStartleExpectedConversationPenalty;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response|Startle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PlayerSpeechStartleMaximumChance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response|Startle", meta = (ClampMin = "0.0"))
	float PlayerSpeechStartleCooldownSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response|Startle", meta = (ClampMin = "1.0"))
	float PlayerSpeechStartleMaximumDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response|Startle", meta = (ClampMin = "0.1"))
	float PlayerSpeechStartleFullUnexpectednessSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Embodied Response|Startle", meta = (ClampMin = "0.1"))
	float PlayerSpeechStartleExpressionSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Validation")
	bool bForceNextPlayerSpeechStartle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Memory")
	FString CharacterDescription;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Activities")
	TSoftObjectPtr<UNPCAutonomyActionSet> ActionSet;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Activities|Fallback")
	TArray<FNPCAutonomyActivityDefinition> Activities;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Context")
	FNPCAutonomyPlayerContext PlayerContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Context")
	TMap<FName, bool> WorldStateTags;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Runtime")
	FString CurrentActivityID;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Runtime")
	FString LastDecisionReason;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Runtime")
	FString LastDecisionContextJson;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Runtime")
	FString LastDecisionTrigger;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Runtime")
	float CurrentBoredomDecisionThreshold;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Runtime")
	FNPCAutonomyPlan ActivePlan;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Memory")
	FString LastAppraisedMemorySummary;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Memory")
	FString LastAppraisalInterpretation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Memory")
	float LastSpontaneousRecallOpportunityProbability;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Emotion")
	FString CurrentEmotionLabel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Emotion")
	FString CurrentExpressionIntent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Metrics")
	int32 TotalDecisionRequests;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Metrics")
	int32 TotalContextCharactersSent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Metrics")
	int32 TotalPlansCreated;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Metrics")
	int32 TotalPlanStepsStarted;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Metrics")
	int32 TotalPlanInterruptions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Metrics")
	int32 TotalPlansResumed;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Metrics")
	int32 TotalAmbientActivitiesStarted;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Metrics")
	int64 TotalAutonomyPromptTokens;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Metrics")
	int64 TotalAutonomyCompletionTokens;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Metrics")
	int64 TotalAutonomyCachedPromptTokens;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Metrics")
	int32 TotalAppraisalRequests;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Metrics")
	int64 TotalAppraisalPromptTokens;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy|Metrics")
	int64 TotalAppraisalCompletionTokens;

	UPROPERTY(BlueprintAssignable, Category = "NPC Autonomy")
	FNPCAutonomyDecisionMadeSignature OnAutonomyDecisionMade;

	UPROPERTY(BlueprintAssignable, Category = "NPC Autonomy")
	FNPCAutonomyDecisionMadeSignature OnAutonomyActivityStarted;

	UPROPERTY(BlueprintAssignable, Category = "NPC Autonomy")
	FNPCAutonomyActivityFinishedSignature OnAutonomyActivityFinished;

	UPROPERTY(BlueprintAssignable, Category = "NPC Autonomy|Blueprint Activities")
	FNPCAutonomyBlueprintActivityRequestedSignature OnBlueprintActivityRequested;

	UPROPERTY(BlueprintAssignable, Category = "NPC Autonomy|Blueprint Activities")
	FNPCAutonomyBlueprintActivityAbortedSignature OnBlueprintActivityAborted;

	UPROPERTY(BlueprintAssignable, Category = "NPC Autonomy|Emotion")
	FNPCExpressionIntentChangedSignature OnNPCExpressionIntentChanged;

	UPROPERTY(BlueprintAssignable, Category = "NPC Autonomy|Embodied Response")
	FNPCAutonomyEmbodiedGestureRequestedSignature OnEmbodiedGestureRequested;

	UFUNCTION(BlueprintCallable, Category = "NPC Autonomy")
	void SetAutonomyEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "NPC Autonomy")
	void EvaluateAutonomyNow();

	UFUNCTION(BlueprintCallable, Category = "NPC Autonomy")
	void ForceAutonomyDecisionNow(const FString& Trigger = TEXT("manual_debug"));

	UFUNCTION(BlueprintCallable, Category = "NPC Autonomy|Validation")
	void ActivateMultiStepPlanValidationScenario();

	UFUNCTION(BlueprintCallable, Category = "NPC Autonomy")
	void InterruptAutonomy(const FString& Reason = TEXT("external_interrupt"));

	UFUNCTION(BlueprintCallable, Category = "NPC Autonomy|Context")
	void SetPlayerContext(const FNPCAutonomyPlayerContext& NewContext, bool bMeaningfulChange = true);

	UFUNCTION(BlueprintCallable, Category = "NPC Autonomy|Context")
	void SetWorldStateTag(FName Tag, bool bValue, bool bMeaningfulChange = true);

	UFUNCTION(BlueprintCallable, Category = "NPC Autonomy|Plan")
	bool RequestDiningAreaCleanup(const FString& TableID = TEXT("current_table"));

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Autonomy|Context")
	bool GetWorldStateTag(FName Tag) const;

	UFUNCTION(BlueprintCallable, Category = "NPC Autonomy")
	void NotifyPlayerInteraction();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Autonomy")
	bool IsAutonomyActivityRunning() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Autonomy")
	bool HasProtectedTaskInProgress() const;

	FString BuildInteractiveDialogueContext();

	static UNPCAutonomyComponent* GetPrimaryAutonomyComponent();

	UFUNCTION(BlueprintCallable, Category = "NPC Autonomy|Blueprint Activities")
	void CompleteBlueprintActivity(const FString& ActivityID, bool bSuccess = true);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	ACafeServiceManagerActor* ServiceManager;

	UPROPERTY(Transient)
	AActor* MaidNPCActor;

	UPROPERTY(Transient)
	UNPCActionQueueComponent* ActionQueueComponent;

	UPROPERTY(Transient)
	ULLMClient* AutonomyLLMClient;

	UPROPERTY(Transient)
	UAnimSequenceBase* ActiveActivityAnimation;

	UPROPERTY(Transient)
	UNPCAutonomyActionSet* LoadedActionSet;

	UPROPERTY(Transient)
	TArray<FNPCAutonomyActivityDefinition> RuntimeActivities;

	UPROPERTY(Transient)
	UNPCMemoryComponent* MemoryComponent;

	UPROPERTY(Transient)
	UNPCEmotionComponent* EmotionComponent;

	UPROPERTY(Transient)
	UNPCResearchLoggerComponent* ResearchLoggerComponent;

	FTimerHandle EvaluationTimerHandle;
	FTimerHandle BlueprintActivityTimeoutTimerHandle;
	FTimerHandle MultiStepValidationInterruptTimerHandle;
	FTimerHandle ExpressionMorphResetTimerHandle;
	FTimerHandle ExpressionMorphBlendTimerHandle;
	FTimerHandle PlanThoughtDisplayTimerHandle;
	FTimerHandle BlinkStartTimerHandle;
	FTimerHandle BlinkFinishTimerHandle;
	FTimerHandle EmbodiedResponseResumeTimerHandle;
	FTimerHandle InteractiveFacingFallbackTimerHandle;
	bool bDecisionPending;
	bool bAppraisalPending;
	bool bActivityRunning;
	bool bInterruptingActivity;
	bool bMeaningfulContextChangePending;
	bool bMultiStepValidationInterruptConsumed;
	bool bValidationRecallConsumed;
	bool bPhoneValidationActivityConsumed;
	bool bHasEncounteredPlayer;
	bool bCurrentActivityIsAmbient;
	bool bExpressionOverlayActive;
	bool bExpressionOverlayHeldByPlan;
	bool bAwaitingEmbodiedResponse;
	bool bResumePausedActivityAfterReply;
	bool bInteractiveFacingPlayer;
	bool bStartlePreparedForCurrentInteraction;
	bool bPendingPlayerSpeechStartle;
	FString PendingInteractiveActivityID;
	bool bBlinkFollowUpPending;
	int32 PendingDecisionRequestId;
	int32 PendingAppraisalRequestId;
	double NextDecisionEarliestWorldTime;
	double LastStateUpdateWorldTime;
	double LastDecisionRequestWorldTime;
	double LastRecallAppraisalWorldTime;
	double PostPlanRecallBoostUntilWorldTime;
	double NextIdleStatusLogWorldTime;
	double LastActivityEndWorldTime;
	double PlanningSuppressedUntilWorldTime;
	double LastPlayerSpeechWorldTime;
	double LastPlayerSpeechStartleWorldTime;
	double ExpressionBlendStartWorldTime;
	float ExpressionBlendDurationSeconds;
	float PendingStartleFocus;
	float PendingStartleUnexpectedness;
	float PendingStartleRearFactor;
	float PendingStartleExpectedConversation;
	float PendingStartleProbability;
	float PendingStartleRoll;
	float PendingStartleIntensity;
	float PendingStartlePlayerDistance;
	FString LastKnownNPCZone;
	FString PendingPlanSurfaceThought;
	FString PendingDecisionTrigger;
	FString PendingPlanningTriggerAfterAppraisal;
	FNPCMemoryEntry PendingAppraisalMemory;
	FNPCRecallScore PendingRecallScore;
	TArray<FString> LastRequestedAvailableActions;
	TArray<FString> RecentActions;
	TArray<double> RecentActionCompletedWorldTimes;
	TMap<FString, double> ActivityCooldownUntil;
	TArray<FName> AppliedExpressionMorphs;
	TMap<FName, float> CurrentExpressionMorphWeights;
	TMap<FName, float> ExpressionBlendStartWeights;
	TMap<FName, float> ExpressionBlendTargetWeights;
	TArray<FName> BlinkMorphNames;
	ENPCAutonomyRuntimePhase RuntimePhase;

	void ResolveReferences();
	void BindActionQueue(UNPCActionQueueComponent* NewActionQueue);
	void BuildRuntimeActivityLibrary();
	const TArray<FNPCAutonomyActivityDefinition>& GetConfiguredActivities() const;
	void UpdateInternalState(float DeltaSeconds);
	bool HasOutstandingTask() const;
	bool ShouldRequestNewDecision(FString& OutTrigger) const;
	void RequestNewDecision(const FString& Trigger);
	void MarkMeaningfulContextChange(const FString& Trigger);
	void RollNextBoredomThreshold();
	void UpdateFastValidationDebugMessage();
	bool CanRunAutonomy() const;
	bool IsServiceStateAutonomyAllowed(ECafeServiceState State) const;
	TArray<FString> BuildAvailableActivityIDs() const;
	bool IsActivityAvailable(const FNPCAutonomyActivityDefinition& Activity) const;
	bool HasSceneTargetTag(const FString& TargetTag) const;
	bool IsMaidNearSceneTargetTag(const FString& TargetTag, float AcceptanceRadius = 180.0f) const;
	FString BuildDecisionContextJson(const TArray<FString>& AvailableActivityIDs) const;
	FString BuildMemoryAppraisalContextJson(const FNPCMemoryEntry& Memory, const FNPCRecallScore& Score) const;
	TArray<FName> BuildRecallCueTags() const;
	float CalculateRecallAttentionAvailability() const;
	bool TryBeginSpontaneousRecall(float DeltaSeconds);
	bool TryStartAmbientBehavior(float DeltaSeconds);
	const FNPCAutonomyActivityDefinition* SelectAmbientActivity(FString& OutScoreSummary) const;
	bool TryBeginMemoryAppraisal(const FString& PlanningTrigger);
	void SeedPositiveValidationMemory();
	bool TryStartVisibleMemoryReaction();
	bool IsAnimationCompatibleWithMaid(const UAnimSequenceBase* Animation) const;
	void ContinuePlanningAfterAppraisal();
	void SchedulePlanSurfaceThought(const FString& SurfaceThought, float DelaySeconds);
	void DisplayPendingPlanSurfaceThought();
	FString BuildFallbackPlanSurfaceThought(const TArray<FString>& Steps) const;
	void ApplyExpressionMorphs(
		const FString& ExpressionIntent,
		bool bHoldUntilPlanEnds = false,
		float DurationSecondsOverride = -1.0f);
	void ResetExpressionMorphs();
	void StartExpressionMorphBlend(const TMap<FName, float>& TargetWeights, float BlendDurationSeconds);
	void UpdateExpressionMorphBlend();
	void SetExpressionMorphWeight(FName MorphName, float Weight);
	void ClearExpressionMorphsImmediately();
	void InitializeAutomaticBlinking();
	void ScheduleNextAutomaticBlink(float DelayOverrideSeconds = -1.0f);
	void StartAutomaticBlink();
	void FinishAutomaticBlink();
	void SetBlinkMorphWeight(float Weight);
	FString ResolvePlayerActivity() const;
	FString ResolvePlayerZone() const;
	FString GetServiceStateName() const;
	FString ResolveCurrentPerformanceTone() const;
	const FNPCAutonomyActivityDefinition* FindActivityDefinition(const FString& ActivityID) const;
	bool IsActionOffered(const FString& ActivityID) const;
	bool ValidateTaskPlanStructure(const TArray<FString>& Steps, FString& OutError) const;
	bool ParsePlanDecision(
		const FString& SelectedAction,
		const FString& RawJson,
		FString& OutDecision,
		FString& OutGoal,
		TArray<FString>& OutSteps,
		FString& OutCommitment,
		FString& OutSurfaceThought) const;
	bool TryStartCurrentPlanStep(bool bIsResume = false);
	void AdvanceOrFinishPlan();
	void ClearActivePlan(const FString& Reason);
	bool ShouldSuspendActivePlan(const FString& InterruptionReason) const;
	void SuspendOrDiscardPlan(const FString& InterruptionReason);
	bool StartActivity(const FNPCAutonomyActivityDefinition& Activity, const FString& Reason, bool bIsAmbient = false);
	void BeginBlueprintActivityExecution(const FNPCAutonomyActivityDefinition& Activity);
	void HandleBlueprintActivityTimeout();
	void ApplyActivitySuccess(const FNPCAutonomyActivityDefinition& Activity);
	void FinishActivity(bool bInterrupted);
	void ScheduleNextDecision(float DelaySeconds);
	void RememberActivity(const FString& ActivityID);
	static FString GetCategoryName(ENPCAutonomyActivityCategory Category);
	void HandleInteractiveLLMRequestStarted();
	void HandleEmbodiedResponseRequested(const FNPCEmbodiedResponseSignal& Signal);
	void HandleInteractiveReplyFinished();
	void ResumePausedActivityAfterInteraction();
	void StopInteractiveFacingPlayer();
	bool CanPreserveActivityForEmbodiedResponse() const;
	void PreparePlayerSpeechStartle();
	float CalculateCurrentActivityFocus() const;
	float CalculateExpectedConversationLevel() const;
	float CalculatePlayerRearFactor(AActor*& OutPlayerActor, float& OutDistance) const;
	void ApplyEmbodiedGazeIntent(const FString& GazeIntent, bool bAllowBodyRotation);
	bool TryExecuteStandaloneStartleReaction();
	bool HasInternalEmbodiedGestureExecutor() const;
	bool TryExecuteInternalEmbodiedGesture(
		const FNPCEmbodiedResponseSignal& Signal,
		const FString& AppliedPolicy);
	void HandleInteractionMemoryExtracted(const FNPCInteractionMemorySignal& Signal);
	void HandleSocialExpressionRequested(
		const FString& ExpressionIntent,
		const FString& ExpressionDescription,
		float DurationSeconds);
	void HandleMultiStepValidationAutoInterrupt();
	void RecordResearchEvent(const FString& EventType, const TSharedRef<FJsonObject>& Payload) const;

	UFUNCTION()
	void HandleServiceStateChanged(ECafeServiceState NewState);

	UFUNCTION()
	void HandleActionQueueFinished(bool bSuccess);

	UFUNCTION()
	void HandleAutonomyDecisionReceived(int32 RequestId, const FString& SelectedAction, const FString& Reason, const FString& RawJson);

	UFUNCTION()
	void HandleAutonomyDecisionError(int32 RequestId, const FString& ErrorMessage);

	UFUNCTION()
	void HandleMemoryAppraisalReceived(int32 RequestId, const FString& RawJson);

	UFUNCTION()
	void HandleMemoryAppraisalError(int32 RequestId, const FString& ErrorMessage);
};
