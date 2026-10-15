// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/StaticMesh.h"
#include "NPCAutonomyTypes.generated.h"

UENUM(BlueprintType)
enum class ENPCAutonomyActivityCategory : uint8
{
	Leisure,
	Task,
	Preference,
	Expression
};

UENUM(BlueprintType)
enum class ENPCAutonomyAmbientDrive : uint8
{
	Neutral,
	BoredomRelief,
	EnergyRecovery,
	Curiosity,
	PositiveEmotion,
	NegativeEmotion
};

UENUM(BlueprintType)
enum class ENPCAutonomyExecutionMode : uint8
{
	BuiltInSequence,
	BlueprintEvent
};

UENUM(BlueprintType)
enum class ENPCAutonomyDecisionMode : uint8
{
	EventTriggeredShortPlan UMETA(DisplayName = "Event Triggered Short Plan"),
	FixedIntervalSingleAction UMETA(DisplayName = "Fixed Interval Single Action (Baseline)")
};

UENUM(BlueprintType)
enum class ENPCAutonomyPlanStatus : uint8
{
	None,
	Active,
	Suspended
};

USTRUCT(BlueprintType)
struct FNPCAutonomyMotiveState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float Boredom = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float Duty = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float SocialAttention = 20.0f;
};

USTRUCT(BlueprintType)
struct FNPCAutonomyResourceState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float Energy = 85.0f;
};

USTRUCT(BlueprintType)
struct FNPCAutonomyPlan
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy")
	FString Goal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy")
	TArray<FString> Steps;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy")
	int32 CurrentStepIndex = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy")
	FString Commitment = TEXT("low");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy")
	FString Trigger;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Autonomy")
	ENPCAutonomyPlanStatus Status = ENPCAutonomyPlanStatus::None;
};

USTRUCT(BlueprintType)
struct FNPCAutonomyActivityDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	FString ActivityID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	FString LLMDescription;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	ENPCAutonomyActivityCategory Category = ENPCAutonomyActivityCategory::Leisure;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Ambient")
	ENPCAutonomyAmbientDrive AmbientDrive = ENPCAutonomyAmbientDrive::Neutral;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Ambient", meta = (ClampMin = "0.1", ClampMax = "100.0"))
	float AmbientBaseWeight = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Planning")
	FName TaskGroup = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Planning", meta = (ClampMin = "0"))
	int32 TaskOrder = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Planning")
	bool bRequiredTaskStep = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	ENPCAutonomyExecutionMode ExecutionMode = ENPCAutonomyExecutionMode::BuiltInSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	FString TargetSceneTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	TSoftObjectPtr<UAnimSequenceBase> Animation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	FName AnimationSlotName = FName(TEXT("DefaultSlot"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy", meta = (ClampMin = "0.01"))
	float AnimationPlayRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy", meta = (ClampMin = "0.1"))
	float AnimationFallbackSeconds = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Held Prop")
	TSoftObjectPtr<UStaticMesh> HeldPropMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Held Prop")
	FName HeldPropSocketName = FName(TEXT("RightHand"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Held Prop")
	FTransform HeldPropRelativeTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy", meta = (ClampMin = "0.0"))
	float CooldownSeconds = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Effects", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float StimulationGain = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Effects", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float DutyRelief = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Effects", meta = (ClampMin = "-100.0", ClampMax = "100.0"))
	float EnergyDelta = -2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy|Requirements", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float MinimumEnergy = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	FName RequiredWorldStateTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	FName WorldStateTagToClearOnSuccess = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy", meta = (EditCondition = "ExecutionMode == ENPCAutonomyExecutionMode::BlueprintEvent"))
	bool bMoveToTargetBeforeBlueprintEvent = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy", meta = (ClampMin = "1.0", EditCondition = "ExecutionMode == ENPCAutonomyExecutionMode::BlueprintEvent"))
	float BlueprintExecutionTimeoutSeconds = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	bool bReturnToCounterAfterCompletion = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	bool bEnabled = true;
};

USTRUCT(BlueprintType)
struct FNPCAutonomyPlayerContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	FString Activity = TEXT("unknown");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	FString Zone = TEXT("unknown");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	FString AttentionTarget = TEXT("unknown");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	FString Emotion = TEXT("unknown");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	FString RecentEvent = TEXT("none");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Autonomy")
	TArray<FString> RecentEvents;
};

USTRUCT(BlueprintType)
struct FNPCEmbodiedResponseSignal
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Embodied Response")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Embodied Response")
	FString ReactionStyle = TEXT("neutral");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Embodied Response")
	FString BaseActionPolicy = TEXT("stop");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Embodied Response")
	FString FaceIntent = TEXT("neutral");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Embodied Response")
	FString GazeIntent = TEXT("none");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Embodied Response")
	FString LeftHandIntent = TEXT("keep_current");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Embodied Response")
	FString RightHandIntent = TEXT("keep_current");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Embodied Response")
	FString VoiceIntent = TEXT("normal");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Embodied Response")
	FString PropIntent = TEXT("keep_current");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Embodied Response")
	FString DisplayNote;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Embodied Response")
	bool bResumeAfterReply = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Embodied Response")
	bool bHasSpokenReply = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Embodied Response")
	FString Reason;
};
