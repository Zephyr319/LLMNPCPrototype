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
class AWaterablePlantActor;
struct FCafeWorldSnapshot;

enum class ENPCAutonomyRuntimePhase : uint8
{
	None,
	EmotionReaction,
	BuiltInQueue,
	SocialSeatEntering,
	SocialSeatSession,
	SocialSeatExiting,
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

// One thing the NPC "has on her mind", with a 0-100 strength. Computed on demand from the real state
// (plants, motives, unfinished plan); it is never stored as a second source of truth.
struct FNPCConcern
{
	FString Id;
	FString Kind;
	FString Detail;
	float Score = 0.0f;
	bool bUnfinished = false;
};

// Read-only view of the plan state for the hotkey UI panel (see UCafePlanPanelWidget).
struct FNPCPlanPanelData
{
	ENPCAutonomyPlanStatus Status = ENPCAutonomyPlanStatus::None;
	FString Goal;
	FString Trigger;
	FString Commitment;
	TArray<FString> Steps;
	int32 CurrentStepIndex = 0;
	FString CurrentActivity;
	bool bDecisionPending = false;
	FString ServiceState;
	bool bWaitingForRelease = false;
	double OrderWaitSilenceSeconds = 0.0;
	bool bHasPlanConcern = false;
	float PlanConcern = 0.0f;
	float PlanKeepThreshold = 0.0f;
	FString PlanConcernBreakdown;
	TArray<FNPCConcern> Concerns;
	float Pressure = 0.0f;
	float TriggerPressure = 0.0f;
	FString LastFailureReason;
	FString Condition;
	TArray<FString> History;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class LLMNPCPROTOTYPE_API UNPCAutonomyComponent : public UActorComponent
{
	GENERATED_BODY()

	// Read-only access for the on-demand world snapshot; it must never write component state.
	friend class UCafeWorldSnapshotSubsystem;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Seat", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float SofaRestEnergyThreshold;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Seat", meta = (ClampMin = "1", ClampMax = "10"))
	int32 SeatedIdleLoopCount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Seat", meta = (ClampMin = "1", ClampMax = "20"))
	int32 SocialSeatedIdleLoopCount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Seat")
	bool bRequireServedStateForNaturalSocialSit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Seat", meta = (ClampMin = "5.0", ClampMax = "120.0"))
	float SeatInvitationTimeoutSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Seat", meta = (ClampMin = "0.0"))
	float SeatInvitationDeclineCooldownSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Seat")
	FString SeatInvitationText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Seat")
	TSoftObjectPtr<UAnimSequenceBase> SitDownAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Seat")
	TSoftObjectPtr<UAnimSequenceBase> SeatedIdleAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Seat")
	TSoftObjectPtr<UAnimSequenceBase> SeatedConversationAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Seat")
	TSoftObjectPtr<UAnimSequenceBase> StandUpAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Seat", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float SeatedConversationOpeningDelaySeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Seat", meta = (ClampMin = "15.0", ClampMax = "600.0"))
	float SeatedConversationInactivityTimeoutSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Seat", meta = (ClampMin = "1", ClampMax = "4"))
	int32 SeatedConversationIdleLoopsPerCycle;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Memory|Validation",
		meta = (DisplayName = "Seed Demo Memories For Validation",
			ToolTip = "Adds several context-sensitive demo memories once, without duplicating existing entries."))
	bool bSeedPositiveMemoryForValidation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Memory|Validation",
		meta = (ToolTip = "Forces the first eligible demo recall to pass its probability check."))
	bool bForceFirstValidationRecall;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Memory|Validation",
		meta = (ToolTip = "Prevents demo memories from appearing before the NPC has encountered the player."))
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

	UFUNCTION(BlueprintCallable, Category = "NPC Autonomy|Seat|Validation")
	bool ForceSeatActivityForValidation(const FString& FurnitureID = TEXT("sofa01"));

	UFUNCTION(BlueprintCallable, Category = "NPC Autonomy|Seat|Validation")
	bool ForceSitOppositePlayerForValidation();

	// Moisture added to the bound plant per second while the watering animation actually plays.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Watering", meta = (ClampMin = "0.1"))
	float WateringMoisturePerSecond = 5.0f;

	// Upper bound on animation loops queued for one watering pass.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Watering", meta = (ClampMin = "1"))
	int32 MaxWateringAnimationLoops = 8;

	// --- Suspended plan while the customer is choosing (service state WaitingForOrder) ---
	// A suspended plan stays parked in WaitingForOrder until the player releases the NPC ("go ahead, I'll
	// call you") or the player has been silent long enough for her to excuse herself.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Order Wait")
	bool bEnablePlayerReleaseResume = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Order Wait")
	bool bEnableOrderWaitSilencePrompt = true;

	// Upper cap: after this much player silence she excuses herself even if her concern is weak.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Order Wait", meta = (ClampMin = "5.0"))
	float OrderWaitSelfReleaseSilenceSeconds = 90.0f;

	// She excuses herself when (strongest plant concern + silence seconds * this rate) reaches ReleasePressure.
	// A drier plant, or one she already started on, therefore makes her ask to leave sooner.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Order Wait", meta = (ClampMin = "0.0"))
	float OrderWaitSilencePressurePerSecond = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Order Wait", meta = (ClampMin = "1.0"))
	float OrderWaitReleasePressure = 110.0f;

	// Never excuse herself within this many seconds of the customer's last words or her own last reply.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Order Wait", meta = (ClampMin = "0.0"))
	float OrderWaitMinSilenceSeconds = 8.0f;

	// Seconds of player silence before she gently asks about the order when she has no suspended plan.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Order Wait", meta = (ClampMin = "5.0"))
	float OrderWaitNudgeSilenceSeconds = 60.0f;

	// Delay between the release (player's or her own line) and the plan actually resuming.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Order Wait", meta = (ClampMin = "0.0"))
	float ReleaseResumeDelaySeconds = 5.0f;

	// When true the two spontaneous order-wait utterances (excuse herself / gentle nudge) are worded by the
	// model from an intent description. The fixed lines below are only a fallback when the request fails.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Order Wait")
	bool bGenerateOrderWaitLinesWithLLM = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Order Wait")
	FString OrderWaitSelfReleaseLine;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Order Wait")
	FString OrderWaitNudgeLine;

	// --- Experiment condition ---
	// Baseline / Full overwrite the individual mechanism switches at BeginPlay (and via Cafe.Condition /
	// Cafe.Trial.Begin). Custom leaves them as set in the editor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Experiment")
	ENPCExperimentCondition ExperimentCondition = ENPCExperimentCondition::Custom;

	// While the customer's utterance is being answered, do not start a new plan step or planning request.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Experiment")
	bool bHoldNewStepsWhileReplyPending = true;

	// A "let me show you to your seat" reply that arrives after the customer already sat is replaced.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Experiment")
	bool bReplaceStaleGuideReply = true;

	UFUNCTION(BlueprintCallable, Category = "NPC Autonomy|Experiment")
	void ApplyExperimentCondition(ENPCExperimentCondition Condition);

	FString GetExperimentConditionName() const;

	// Fills the read-only data behind the P-key plan panel. Never writes component state, never logs.
	void GetPlanPanelData(FNPCPlanPanelData& OutData) const;
	FString DescribeExperimentSwitchesJson() const;

	// --- Concerns: things on her mind, each with a 0-100 strength ---
	// Plants no longer trigger planning the moment they cross a line. Their strength grows as they dry out,
	// and planning is requested only when the strongest plant concern plus weighted boredom is high enough.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Concerns")
	bool bEnableConcernPressureTrigger = true;

	// Baseline switch: true restores the old behaviour (a moisture reminder marks the context as changed and
	// requests planning on the next evaluation).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Concerns")
	bool bPlantReminderTriggersPlanning = false;

	// Strength added per moisture point below the needs-water line (60 -> 0, 40 -> 40, 20 -> 80 at 2.0).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Concerns", meta = (ClampMin = "0.1"))
	float PlantConcernScorePerMoisturePoint = 2.0f;

	// Planning is requested when the strongest plant concern + ConcernBoredomWeight * boredom reaches this.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Concerns", meta = (ClampMin = "1.0"))
	float ConcernTriggerPressure = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Concerns", meta = (ClampMin = "0.0"))
	float ConcernBoredomWeight = 0.5f;

	// After a concern-pressure planning request, wait this long before another one (she may decline to act).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Concerns", meta = (ClampMin = "0.0"))
	float ConcernTriggerCooldownSeconds = 60.0f;

	// An interrupted watering counts for more than a fresh dry plant; the bonus halves every HalfLife seconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Concerns", meta = (ClampMin = "0.0"))
	float UnfinishedConcernBonus = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Concerns", meta = (ClampMin = "1.0"))
	float UnfinishedConcernHalfLifeSeconds = 180.0f;

	// --- Concern competition (step 2): an unfinished plan competes with other concerns by score ---
	// On: whether an interrupted plan is kept, and whether it is resumed, depends on its concern score.
	// Off: the fixed rules (commitment level / remaining task steps; always resume when allowed).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Concern Competition")
	bool bEnableConcernCompetition = true;

	// A plan whose score is below this is dropped (at interruption, or when it has faded before resuming).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Concern Competition", meta = (ClampMin = "0.0"))
	float PlanKeepThreshold = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Concern Competition", meta = (ClampMin = "0.0"))
	float PlanBaseConcernMedium = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Concern Competition", meta = (ClampMin = "0.0"))
	float PlanBaseConcernLow = 20.0f;

	// Added when a Task-category step remains (only if bResumeTaskPlansAfterService).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Concern Competition", meta = (ClampMin = "0.0"))
	float PlanTaskStepBonus = 15.0f;

	// Share of the target plant's own concern that adds to the score of a watering plan.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Concern Competition", meta = (ClampMin = "0.0"))
	float PlanWaterConcernWeight = 0.5f;

	// The commitment part of the score halves every this many seconds spent suspended.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Concern Competition", meta = (ClampMin = "1.0"))
	float PlanConcernHalfLifeSeconds = 300.0f;

	// Another concern must beat the plan by this much to replace it (prevents flip-flopping).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Concern Competition", meta = (ClampMin = "0.0"))
	float ConcernCompetitionMargin = 10.0f;

	// Human-readable dump of the current concerns and pressure (also logged). Used by Cafe.Concerns.
	FString DescribeConcerns() const;

	// Off by default: the model alone decides whether the player let her go. When on, a clear release phrase that
	// the model missed is treated as a release (a local safety net, for experiment conditions that want it).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Order Wait")
	bool bInferReleaseLocally = false;

	// True while an interrupted plan is parked and waiting for the player to let her go (or for her own excuse).
	bool IsSuspendedPlanWaitingForRelease() const
	{
		return ActivePlan.Status == ENPCAutonomyPlanStatus::Suspended && !bSuspendedPlanReleased;
	}

	// Called by plants when moisture crosses a reminder line (60/40/20). Only marks the context as changed so
	// the next planning round can see it; it never interrupts the current activity.
	void NotifyPlantMoistureReminder(const FString& PlantID, float Moisture, int32 Threshold);

	// Debug: start the water_flower activity bound to the given plant right now.
	bool ForceWaterPlantForValidation(const FString& PlantID);

	UFUNCTION(BlueprintCallable, Category = "NPC Autonomy|Seat")
	bool RequestSitOppositePlayer();

	UFUNCTION(BlueprintCallable, Category = "NPC Autonomy|Seat|Validation")
	bool ForceSeatInvitationForValidation();

	UFUNCTION(BlueprintCallable, Category = "NPC Autonomy|Seat")
	bool ResolveSeatInvitation(bool bAccepted);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Autonomy|Seat")
	bool IsSeatInvitationPending() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Autonomy|Seat")
	bool IsSeatedConversationActive() const;

	UFUNCTION(BlueprintCallable, Category = "NPC Autonomy|Seat")
	bool RequestEndSeatedConversation(const FString& Reason = TEXT("player_requested_end"));

	void NotifyNPCEnteredSeat(const FString& SeatID);

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

	// Empty means that speech may turn the standing actor without disrupting its activity.
	FString GetSpeechBodyRotationBlockReason() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Autonomy")
	bool HasProtectedTaskInProgress() const;

	FString BuildInteractiveDialogueContext(const FCafeWorldSnapshot* ExistingSnapshot = nullptr);

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
	FTimerHandle SeatInvitationTimeoutTimerHandle;
	FTimerHandle SeatedConversationOpeningTimerHandle;
	FTimerHandle SeatedConversationInactivityTimerHandle;
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
	bool bSeatInvitationApproachInProgress;
	bool bSeatInvitationPending;
	bool bSeatInvitationAcceptedForStart;
	bool bSeatedConversationPerformancePending;
	bool bSeatedConversationExitWasInterrupted;
	int32 SeatedConversationOpeningAttempts;
	FString PendingInteractiveActivityID;
	FString PendingSeatInvitationSeatID;
	FString SeatedConversationExitReason;
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
	double SeatedConversationStartedWorldTime;
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
	FString ForcedSeatID;
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

	void RecordPlanEvent(const TCHAR* EventType, const FString& Reason) const;

	// Last plan lifecycle events, oldest first, for the plan panel (capped).
	mutable TArray<FString> PlanHistoryLog;

	// --- Concerns ---
	void BuildConcerns(TArray<FNPCConcern>& OutConcerns) const;
	float ComputeConcernPressure(const TArray<FNPCConcern>& Concerns, FString* OutTopPlantId, float* OutPlantScore) const;
	void AppendConcernsToDecisionContext(const TSharedPtr<FJsonObject>& Root) const;
	double LastConcernTriggerWorldTime = -1000000.0;
	double UnfinishedWaterSinceWorldTime = -1.0;
	double PlanSuspendedWorldTime = -1.0;
	FString LastPlanStepFailureReason;
	float ComputePlanConcern(FString* OutBreakdown) const;
	FString GetPlanPlantID() const;
	bool ResolvePlanCompetition(FString& OutDropReason) const;
	FString GetActivityUnavailableReason(const FNPCAutonomyActivityDefinition& Activity) const;

	// --- Suspended plan release during WaitingForOrder ---
	bool ReleaseSuspendedPlan(const TCHAR* Source, float ResumeDelaySeconds);
	void UpdateOrderWaitSilence(double Now, bool bPlanSuspended);
	double GetOrderWaitSilenceSeconds(double Now) const;
	void SpeakAutonomyLine(const FString& Line) const;
	void SpeakAutonomyIntent(const FString& Intent, const FString& FallbackLine, double SilenceSeconds);

	bool bSuspendedPlanReleased = false;
	bool bOrderWaitActive = false;
	bool bOrderWaitNudgeSpoken = false;
	double OrderWaitStartWorldTime = -1.0;
	// Player speech time reuses the existing LastPlayerSpeechWorldTime (set by PreparePlayerSpeechStartle).
	double LastInteractiveReplyFinishedWorldTime = -1.0;
	// While the customer's utterance is being answered, no new plan step or planning request is started.
	double InteractiveRequestPendingUntilWorldTime = -1.0;

	// --- Watering (continuous plant moisture) ---
	bool IsWaterActivity(const FNPCAutonomyActivityDefinition& Activity) const;
	AWaterablePlantActor* BindWaterTarget(const FNPCAutonomyActivityDefinition& Activity);
	void ReleaseWaterBinding(const TCHAR* Reason, bool bCompleted);
	void HandleWateringProgressTick();
	bool IsMaidNearPlant(const AWaterablePlantActor& Plant) const;
	bool ValidateWaterStepForResume(FString& OutReason) const;
	void AppendPlantsToDecisionContext(const TSharedPtr<FJsonObject>& Root) const;

	TWeakObjectPtr<AWaterablePlantActor> BoundWaterPlant;
	FString UnfinishedWaterPlantID;
	FString ForcedWaterPlantID;
	float WateringStartMoisture = 0.0f;
	float WateringAddedMoisture = 0.0f;
	double LastWateringProgressWorldTime = -1.0;
	FTimerHandle WateringProgressTimerHandle;
	void RollNextBoredomThreshold();
	void UpdateFastValidationDebugMessage();
	bool CanRunAutonomy() const;
	bool IsServiceStateAutonomyAllowed(ECafeServiceState State) const;
	TArray<FString> BuildAvailableActivityIDs() const;
	bool IsActivityAvailable(const FNPCAutonomyActivityDefinition& Activity) const;
	FString FindAvailableRestSeatID() const;
	FString FindAvailableSeatOppositePlayerID() const;
	bool StartSeatActivitySequence(const FNPCAutonomyActivityDefinition& Activity, const FString& SeatID);
	bool BeginSeatInvitation(
		const FNPCAutonomyActivityDefinition& Activity,
		const FString& SeatID,
		const FString& Reason);
	void PresentSeatInvitation();
	void HandleSeatInvitationTimeout();
	void BeginSeatedConversationOpening();
	void BeginSeatedConversationSession();
	void QueueSeatedConversationAnimation(bool bConversationPerformance);
	void PlaySeatedConversationPerformance();
	void RefreshSeatedConversationInactivityTimeout();
	void HandleSeatedConversationInactivityTimeout();
	void BeginSeatedConversationExit(const FString& Reason, bool bInterrupted);
	void CompleteSeatedConversationExit(bool bSuccess);
	void ResetSeatedConversationState();
	void CancelSeatInvitation(
		const FString& Reason,
		bool bClearPlan,
		bool bReturnToCounter);
	void ReturnToCounterAfterSeatInvitation();
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
