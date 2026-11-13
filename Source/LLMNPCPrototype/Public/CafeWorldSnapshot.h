#pragma once

#include "CoreMinimal.h"
#include "CafeServiceManagerActor.h"
#include "NPCAutonomyTypes.h"
#include "NPCEmotionTypes.h"
#include "CafeWorldSnapshot.generated.h"

// Value-only observations. Editing a returned copy never writes back to an Actor or Component.
// No UObject references, setters, timers, random draws or decision callbacks belong in these types.
UENUM(BlueprintType)
enum class ECafeSnapshotPurpose : uint8
{
	Planning,
	PlayerDialogue,
	TaskInterruption
};

USTRUCT(BlueprintType)
struct LLMNPCPROTOTYPE_API FCafePlayerSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bExists = false;
	// True only when this actor is the customer tracked by the current cafe service flow.
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bInServiceScene = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bSeatStateKnown = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString ActorId;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bSeated = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bInTableArea = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString SeatId;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString TableId;
	// Orthogonal facts: Pose is physical, Zone is spatial, Activity describes what the player is doing.
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString Pose = TEXT("absent");
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString Zone = TEXT("outside");
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString Activity = TEXT("absent");
	// Geometry relative to the NPC (2D). Distance < 0 means unknown. AngleFromNPCForwardDeg: 0 = directly in
	// front of the NPC, 180 = directly behind. RearFactor uses the same (1 - dot) / 2 form as the startle context.
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") float DistanceToNPC = -1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") float AngleFromNPCForwardDeg = -1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") float RearFactor = 0.0f;
};

USTRUCT(BlueprintType)
struct LLMNPCPROTOTYPE_API FCafeNPCSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bExists = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString ActorId;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bAutonomyAvailable = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString ActivityId;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString ActivityTarget;
	// What the movement step last selected by tag/ID (e.g. the plant walked to). It is NOT proof that the later
	// activity step acts on the same instance; compare with the [Watering] bound/released logs.
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString SelectedTargetId;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString SelectedTargetReason;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") float SelectedTargetAgeSeconds = -1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bActivityRunning = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bBusy = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString RuntimePhase;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString Zone = TEXT("unknown");
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bDecisionPending = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bAppraisalPending = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bAwaitingEmbodiedResponse = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bResumeAfterReply = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FNPCAutonomyPlan Plan;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString CurrentPlanStep;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FNPCAutonomyMotiveState Motives;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FNPCAutonomyResourceState Resources;
};

USTRUCT(BlueprintType)
struct LLMNPCPROTOTYPE_API FCafeActionQueueSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bAvailable = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bRunning = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString CurrentAction = TEXT("None");
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString Target;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") int32 PendingActionCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bWaitingForMove = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bAnimationActive = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bAnimationPaused = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString AnimationAsset;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bSeated = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString ReservedSeatId;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString HeldPropAsset;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString HeldPropSocket;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString ServiceGesture;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bMovementLocked = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bPausedForServiceGesture = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bDeferredActionPending = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString DeferredAction;
};

USTRUCT(BlueprintType)
struct LLMNPCPROTOTYPE_API FCafeHeldPropSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString Source;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString Item;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString AssetOrActorId;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString Socket;
};

USTRUCT(BlueprintType)
struct LLMNPCPROTOTYPE_API FCafeBodySnapshot
{
	GENERATED_BODY()
	// Conservative resource interpretation of execution facts, not an authoritative scheduler.
	// Speech output is not observable from existing components, so it is intentionally not reported here.
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bExecutionStateKnown = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bMoving = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bFacingTarget = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString Locomotion = TEXT("unknown");
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString UpperBody = TEXT("unknown");
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString LeftHand = TEXT("unknown");
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString RightHand = TEXT("unknown");
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") TArray<FCafeHeldPropSnapshot> HeldProps;
};

USTRUCT(BlueprintType)
struct LLMNPCPROTOTYPE_API FCafeMemorySnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bAvailable = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") int32 Count = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString LastRecalledId;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString LastRecalledSummary;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString LastAppraisedSummary;
};

USTRUCT(BlueprintType)
struct LLMNPCPROTOTYPE_API FCafeTableSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString ActorId;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bMarkerOccupied = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bOccupied = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") int32 Capacity = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") int32 OccupiedOrReservedSeats = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bCleanupStateKnown = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bNeedsCleanup = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString CleanupEvidence = TEXT("unknown");
};

USTRUCT(BlueprintType)
struct LLMNPCPROTOTYPE_API FCafeSeatSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString ActorId;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString TableId;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bCanSit = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bAllowNPCSit = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bOccupied = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bReserved = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bAvailable = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bAvailableForNPC = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString OccupantId;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bOccupantSeated = false;
};

USTRUCT(BlueprintType)
struct LLMNPCPROTOTYPE_API FCafePlantSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString ActorId;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") float WaterNeed = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") float NeedsWaterThreshold = 0.0f;
	// Continuous moisture model (moisture = 100 - WaterNeed). NeedsWater is a hysteresis state, not a threshold test.
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") float Moisture = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString Tier;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") float Urgency = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") float EnterMoisture = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") float ClearMoisture = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString ReservedBy;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bEnabled = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bNeedsWater = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bCachedNeedsWater = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bReserved = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bAvailable = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") int32 Priority = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bSyncWorldTag = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FName WorldTag;
};

USTRUCT(BlueprintType)
struct LLMNPCPROTOTYPE_API FCafeWorldSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") int32 SchemaVersion = 4;
	// Observed-state revision: changes between captures, not an event counter or replay sequence.
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") int64 Revision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString CapturedAtUtc;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") double WorldTimeSeconds = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString WorldId;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FString ServiceManagerId;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bServiceAvailable = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FCafeServiceSnapshot Service;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FCafePlayerSnapshot Player;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FCafeNPCSnapshot NPC;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FCafeActionQueueSnapshot Execution;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FCafeBodySnapshot Body;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bEmotionAvailable = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FNPCEmotionState Emotion;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") FCafeMemorySnapshot Memory;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") TArray<FCafeTableSnapshot> Tables;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") TArray<FCafeSeatSnapshot> Seats;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") TArray<FCafePlantSnapshot> Plants;
	// Raw source tags and effective tags stay distinguishable; plant aggregates are derived on demand.
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") TArray<FName> ReportedWorldStateTags;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") TArray<FName> ActiveWorldStateTags;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") bool bAnyPlantNeedsWater = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cafe World Snapshot") TArray<FString> Diagnostics;
};

// Offline projections: consume an existing value snapshot, never recapture or call the LLM.
struct LLMNPCPROTOTYPE_API FCafeWorldSnapshotJson
{
	static FString ToJson(const FCafeWorldSnapshot& Snapshot, bool bPretty = false);
	static FString BuildContext(const FCafeWorldSnapshot& Snapshot, ECafeSnapshotPurpose Purpose,
		const FString& FocusEntityId = FString(), int32 MaxEntities = 6);
	static FString DebugSummary(const FCafeWorldSnapshot& Snapshot);
	// Stable content digest excludes capture time/revision and volatile emotion update timestamps.
	static FString StateDigest(const FCafeWorldSnapshot& Snapshot);
};
