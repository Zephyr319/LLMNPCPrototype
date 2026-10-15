// Copyright Epic Games, Inc. All Rights Reserved.

#include "NPCActionQueueComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "CafeServiceManagerActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "LLMClient.h"
#include "NPCAutonomyComponent.h"
#include "NPCFaceTargetComponent.h"
#include "NPCFoodCarryComponent.h"
#include "NPCMoveToTableComponent.h"
#include "PlayerSeatComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TableMarkerActor.h"

bool UNPCActionQueueComponent::bLastKnownSeatedForService = false;
bool UNPCActionQueueComponent::bLastKnownPlayerInTableArea = false;
bool UNPCActionQueueComponent::bLastKnownGuidePlayerInProgress = false;
bool UNPCActionQueueComponent::bLastKnownSeatingGuidanceCompleted = false;
FString UNPCActionQueueComponent::LastKnownCurrentServiceTableID = TEXT("");
FString UNPCActionQueueComponent::LastKnownGuideTargetID = TEXT("");
bool UNPCActionQueueComponent::bLastKnownMealCompletionConfirmationPending = false;
FString UNPCActionQueueComponent::LastKnownMealCompletionConfirmationTableID = TEXT("");
bool UNPCActionQueueComponent::bLastKnownDiningServiceCompleted = false;
FString UNPCActionQueueComponent::LastKnownCompletedServiceTableID = TEXT("");
bool UNPCActionQueueComponent::bSuppressNextOrderConfirmSpeechForStrategyReply = false;
bool UNPCActionQueueComponent::bSuppressNextGuideStartedSpeechForStrategyReply = false;

UNPCActionQueueComponent::UNPCActionQueueComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	bIsRunningQueue = false;
	bStopQueueOnActionFailure = false;
	bPrintActionQueueDebug = true;
	CurrentServiceTableID = TEXT("");
	LastVisitedTableID = TEXT("");
	PostSeatingMenuGuidanceDelay = 1.8f;
	GuideArrivedSeatGestureDuration = 1.5f;
	DuplicateGuideLineSuppressSeconds = 1.0f;
	GuideArrivedSeatGestureName = FName(TEXT("guide_seat_player"));
	PostSeatingMenuHelpEnableDelay = 1.0f;
	PostSeatingMenuGuidanceLine = TEXT("先生、メニューはテーブルの上です。注文が決まったら、いつでもアリスを呼んでください。");
	OrderPreparationSeconds = 3.0f;
	OrderConfirmLine = TEXT("注文を受注しました。少々お待ちください、先生。");
	OrderServedLine = TEXT("お待たせしました、先生。料理をお届けしました！");
	bReturnToCounterAfterServing = true;
	PostServePauseSeconds = 0.8f;
	BillPresentationPauseSeconds = 0.8f;
	ClosingBowPauseSeconds = 0.8f;
	ReturnToCounterAfterClosingDelaySeconds = 1.5f;
	BowBeforeReturnDuration = 2.5f;
	BlockingServiceGesturePauseDuration = 1.5f;
	BowGestureFallbackTimeout = 10.0f;
	BowMinimumLockDurationSeconds = 2.8f;
	BowMovementReleaseSafetyDelay = 0.3f;
	PutBillGestureFallbackTimeout = 3.0f;
	ServeFoodGestureFallbackTimeout = 3.0f;
	GuideSeatGestureFallbackTimeout = 2.0f;
	DefaultBlockingGestureFallbackTimeout = 3.0f;
	DefaultOrderPrice = 800;
	BillLineFormat = TEXT("今回のクエスト報酬は{Price}円です、先生。");
	ServiceClosingLine = TEXT("次のクエストがあれば、いつでもアリスを呼んでください。");
	BillFarewellLine = TEXT("何かあれば、アリスを呼んでくださいね、先生。");
	bWaitingForMove = false;
	bCurrentQueueHadFailure = false;
	bQueuedAnimationInProgress = false;
	bQueuedAnimationPaused = false;
	bPlayerInTableArea = false;
	bSeatingGuidanceCompleted = false;
	bPostSeatingMenuGuidanceDone = false;
	bPendingOrderFulfillment = false;
	bFarewellBowInProgress = false;
	bActionQueuePausedForFarewellBow = false;
	bBowMovementLockActive = false;
	bBowFinishNotifyReceived = false;
	bBowMinimumDurationElapsed = false;
	bBowSafetyDelayElapsed = false;
	bPendingReturnToCounterAfterBow = false;
	bBlockingGestureInProgress = false;
	CurrentBlockingGestureName = TEXT("");
	bBlockingGestureFinishHandled = false;
	bPendingFarewellBowAfterBillGesture = false;
	bPendingPostServeFlowAfterServeGesture = false;
	bSuppressNextOrderConfirmSpeech = false;
	PendingOrderReturnTableID = TEXT("");
	PendingOrderItem = TEXT("");
	PendingPreSeatingOrderItem = TEXT("");
	LastGuideLineText = TEXT("");
	LastGuideLineTime = 0.0;
	PendingOrderPrice = 0;
	ActiveQueuedAnimationMontage = nullptr;
	ActiveQueuedAnimationInstance = nullptr;
	ActiveHeldPropComponent = nullptr;
	BlockingGestureMontageBeforeRequest = nullptr;
	BlockingGestureMontagePositionBeforeRequest = 0.0f;
	PendingGestureAnimationTimingProbe = NAME_None;
	MoveComponent = nullptr;
	FaceTargetComponent = nullptr;
	FoodCarryComponent = nullptr;
}

void UNPCActionQueueComponent::BeginPlay()
{
	Super::BeginPlay();

	CacheOwnerComponents();
	BindMoveCompletedEvent();
	UpdateLastKnownServiceState();
}

void UNPCActionQueueComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearActionQueue();
	UnbindMoveCompletedEvent();

	Super::EndPlay(EndPlayReason);
}

void UNPCActionQueueComponent::ClearActionQueue()
{
	StopQueuedAnimation();
	RemoveActiveHeldProp();
	ActionQueue.Reset();
	CurrentAction = FNPCQueuedAction();
	bIsRunningQueue = false;
	bWaitingForMove = false;
	bCurrentQueueHadFailure = false;
	ClearPendingOrderState();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WaitTimerHandle);
	}
	ClearOrderFulfillmentTimers();
	ClearPostSeatingMenuGuidanceTimers();

	PrintDebugMessage(TEXT("NPC action queue cleared"), FColor::Yellow);
}

void UNPCActionQueueComponent::StopActionQueue()
{
	StopQueuedAnimation();
	RemoveActiveHeldProp();
	ActionQueue.Reset();
	CurrentAction = FNPCQueuedAction();
	bIsRunningQueue = false;
	bWaitingForMove = false;
	bCurrentQueueHadFailure = false;
	ClearPendingOrderState();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WaitTimerHandle);
	}
	ClearOrderFulfillmentTimers();
	ClearPostSeatingMenuGuidanceTimers();

	CacheOwnerComponents();
	if (MoveComponent && MoveComponent->bIsMovingToTable)
	{
		MoveComponent->StopMovingToTable();
	}

	if (FaceTargetComponent)
	{
		FaceTargetComponent->StopFacingActor();
	}

	PrintDebugMessage(TEXT("NPC action queue stopped"), FColor::Yellow);
}

bool UNPCActionQueueComponent::PauseActiveQueuedAnimation()
{
	if (!HasActiveQueuedAnimation() || bQueuedAnimationPaused)
	{
		return false;
	}

	ActiveQueuedAnimationInstance->Montage_Pause(ActiveQueuedAnimationMontage);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().PauseTimer(QueuedAnimationTimerHandle);
	}
	bQueuedAnimationPaused = true;
	UE_LOG(LogTemp, Log, TEXT("[EmbodiedResponse] base animation paused. Montage=%s"), *ActiveQueuedAnimationMontage->GetName());
	return true;
}

bool UNPCActionQueueComponent::ResumeActiveQueuedAnimation()
{
	if (!HasActiveQueuedAnimation() || !bQueuedAnimationPaused)
	{
		return false;
	}

	ActiveQueuedAnimationInstance->Montage_Resume(ActiveQueuedAnimationMontage);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().UnPauseTimer(QueuedAnimationTimerHandle);
	}
	bQueuedAnimationPaused = false;
	UE_LOG(LogTemp, Log, TEXT("[EmbodiedResponse] base animation resumed. Montage=%s"), *ActiveQueuedAnimationMontage->GetName());
	return true;
}

bool UNPCActionQueueComponent::HasActiveQueuedAnimation() const
{
	return bQueuedAnimationInProgress
		&& ActiveQueuedAnimationMontage
		&& ActiveQueuedAnimationInstance;
}

void UNPCActionQueueComponent::EnqueueMoveToTableID(const FString& TableID)
{
	FNPCQueuedAction Action;
	Action.ActionType = ENPCQueuedActionType::MoveToTableID;
	Action.Target = TableID;
	EnqueueAction(Action);
}

void UNPCActionQueueComponent::EnqueueMoveToSceneTag(const FString& TargetTag)
{
	FNPCQueuedAction Action;
	Action.ActionType = ENPCQueuedActionType::MoveToSceneTag;
	Action.Target = TargetTag;
	EnqueueAction(Action);
}

void UNPCActionQueueComponent::EnqueueMoveToFurnitureTag(const FString& FurnitureTag)
{
	FNPCQueuedAction Action;
	Action.ActionType = ENPCQueuedActionType::MoveToFurnitureTag;
	Action.Target = FurnitureTag;
	EnqueueAction(Action);
}

void UNPCActionQueueComponent::EnqueueMoveToFurnitureID(const FString& FurnitureID)
{
	FNPCQueuedAction Action;
	Action.ActionType = ENPCQueuedActionType::MoveToFurnitureID;
	Action.Target = FurnitureID;
	EnqueueAction(Action);
}

void UNPCActionQueueComponent::EnqueueMoveNearPlayer(float StopDistance)
{
	FNPCQueuedAction Action;
	Action.ActionType = ENPCQueuedActionType::MoveNearPlayer;
	Action.StopDistance = FMath::Max(StopDistance, 0.0f);
	EnqueueAction(Action);
}

void UNPCActionQueueComponent::EnqueueWait(float Seconds)
{
	FNPCQueuedAction Action;
	Action.ActionType = ENPCQueuedActionType::Wait;
	Action.WaitSeconds = FMath::Max(Seconds, 0.0f);
	EnqueueAction(Action);
}

void UNPCActionQueueComponent::EnqueueAnimation(UAnimSequenceBase* Animation, FName SlotName, float PlayRate, float FallbackSeconds)
{
	FNPCQueuedAction Action;
	Action.ActionType = ENPCQueuedActionType::Animation;
	Action.Animation = Animation;
	Action.AnimationSlotName = SlotName.IsNone() ? FName(TEXT("DefaultSlot")) : SlotName;
	Action.AnimationPlayRate = FMath::Max(FMath::Abs(PlayRate), KINDA_SMALL_NUMBER);
	Action.WaitSeconds = FMath::Max(FallbackSeconds, 0.1f);
	EnqueueAction(Action);
}

void UNPCActionQueueComponent::EnqueueAttachHeldProp(
	UStaticMesh* PropMesh,
	FName SocketName,
	const FTransform& RelativeTransform)
{
	FNPCQueuedAction Action;
	Action.ActionType = ENPCQueuedActionType::AttachHeldProp;
	Action.HeldPropMesh = PropMesh;
	Action.HeldPropSocketName = SocketName.IsNone() ? FName(TEXT("RightHand")) : SocketName;
	Action.HeldPropRelativeTransform = RelativeTransform;
	EnqueueAction(Action);
}

void UNPCActionQueueComponent::EnqueueRemoveHeldProp()
{
	FNPCQueuedAction Action;
	Action.ActionType = ENPCQueuedActionType::RemoveHeldProp;
	EnqueueAction(Action);
}

void UNPCActionQueueComponent::EnqueuePickUpPlacedFood(const FString& TableID)
{
	FNPCQueuedAction Action;
	Action.ActionType = ENPCQueuedActionType::PickUpPlacedFood;
	Action.Target = TableID.TrimStartAndEnd();
	EnqueueAction(Action);
}

void UNPCActionQueueComponent::EnqueuePlaceCarriedFoodAtCounter()
{
	FNPCQueuedAction Action;
	Action.ActionType = ENPCQueuedActionType::PlaceCarriedFoodAtCounter;
	EnqueueAction(Action);
}

void UNPCActionQueueComponent::EnqueueFacePlayer()
{
	FNPCQueuedAction Action;
	Action.ActionType = ENPCQueuedActionType::FacePlayer;
	EnqueueAction(Action);
}

void UNPCActionQueueComponent::StartActionQueue()
{
	const bool bQueueContainsCounterMove = DoesQueuedActionListContainCounterMove();
	if (BlockMovementIfBlockingGestureInProgress(TEXT("StartActionQueue"), bQueueContainsCounterMove ? TEXT("counter") : TEXT("action_queue"), bQueueContainsCounterMove))
	{
		return;
	}

	if (bActionQueuePausedForFarewellBow || bFarewellBowInProgress)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Return/move to counter blocked because farewell bow is in progress."));
		return;
	}

	if (bIsRunningQueue)
	{
		PrintDebugMessage(TEXT("NPC action queue already running"), FColor::Yellow);
		return;
	}

	if (ActionQueue.Num() <= 0)
	{
		PrintDebugMessage(TEXT("NPC action queue is empty"), FColor::Yellow);
		return;
	}

	CacheOwnerComponents();
	BindMoveCompletedEvent();

	const bool bQueueContainsMove = ActionQueue.ContainsByPredicate([](const FNPCQueuedAction& Action)
	{
		return Action.ActionType == ENPCQueuedActionType::MoveToTableID
			|| Action.ActionType == ENPCQueuedActionType::MoveToSceneTag
			|| Action.ActionType == ENPCQueuedActionType::MoveToFurnitureTag
			|| Action.ActionType == ENPCQueuedActionType::MoveToFurnitureID
			|| Action.ActionType == ENPCQueuedActionType::MoveNearPlayer;
	});
	if (bQueueContainsMove)
	{
		CancelFollowingForExplicitCommand();
	}

	bIsRunningQueue = true;
	bWaitingForMove = false;
	bCurrentQueueHadFailure = false;
	PrintDebugMessage(TEXT("NPC action queue started"), FColor::Green);
	ExecuteNextQueuedAction();
}

void UNPCActionQueueComponent::RunCounterAndReturnToTable(const FString& ReturnTableID)
{
	LogActionQueueMoveRequest(TEXT("RunCounterAndReturnToTable"), ReturnTableID);

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunCounterAndReturnToTable"), TEXT("counter"), true))
	{
		return;
	}

	if (BlockReturnToCounterIfFarewellBowInProgress())
	{
		return;
	}

	const FString TrimmedReturnTableID = ReturnTableID.TrimStartAndEnd();
	if (TrimmedReturnTableID.IsEmpty())
	{
		PrintDebugMessage(TEXT("Current table not known"), FColor::Red);
		return;
	}

	CancelFollowingForExplicitCommand();
	ClearActionQueue();
	EnqueueMoveToSceneTag(TEXT("counter"));
	EnqueueWait(1.0f);
	EnqueueMoveToTableID(TrimmedReturnTableID);
	StartActionQueue();
}

void UNPCActionQueueComponent::RunCounterAndReturnToPlayer()
{
	LogActionQueueMoveRequest(TEXT("RunCounterAndReturnToPlayer"), TEXT("player"));

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunCounterAndReturnToPlayer"), TEXT("counter"), true))
	{
		return;
	}

	if (BlockReturnToCounterIfFarewellBowInProgress())
	{
		return;
	}

	PrintDebugMessage(TEXT("Run counter and return to player"), FColor::Green);
	CancelFollowingForExplicitCommand();
	ClearActionQueue();
	EnqueueMoveToSceneTag(TEXT("counter"));
	EnqueueWait(1.0f);
	EnqueueMoveNearPlayer(140.0f);
	StartActionQueue();
}

void UNPCActionQueueComponent::RunMoveNearPlayer(float StopDistance)
{
	LogActionQueueMoveRequest(TEXT("RunMoveNearPlayer"), TEXT("player"));

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunMoveNearPlayer"), TEXT("player"), false))
	{
		return;
	}

	if (bFarewellBowInProgress)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Return/move to counter blocked because farewell bow is in progress."));
		return;
	}

	PrintDebugMessage(TEXT("Run move near player"), FColor::Green);
	CancelFollowingForExplicitCommand();
	ClearActionQueue();
	EnqueueMoveNearPlayer(StopDistance);
	StartActionQueue();
}

void UNPCActionQueueComponent::RunStartFollowPlayer(float DesiredDistance)
{
	LogActionQueueMoveRequest(TEXT("RunStartFollowPlayer"), TEXT("player"));

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunStartFollowPlayer"), TEXT("player"), false))
	{
		return;
	}

	if (bFarewellBowInProgress)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Return/move to counter blocked because farewell bow is in progress."));
		return;
	}

	PrintDebugMessage(TEXT("Start following player"), FColor::Green);
	ClearActionQueue();

	CacheOwnerComponents();
	if (!MoveComponent)
	{
		PrintDebugMessage(TEXT("NPCMoveToTableComponent not found"), FColor::Red);
		return;
	}

	MoveComponent->StartFollowingPlayer(DesiredDistance);
}

void UNPCActionQueueComponent::RunStopFollowPlayer()
{
	PrintDebugMessage(TEXT("Stop following player"), FColor::Yellow);
	ClearActionQueue();

	CacheOwnerComponents();
	if (!MoveComponent)
	{
		PrintDebugMessage(TEXT("NPCMoveToTableComponent not found"), FColor::Red);
		return;
	}

	MoveComponent->StopFollowingPlayer();
}

void UNPCActionQueueComponent::RunGuidePlayerToTable(const FString& TableID)
{
	LogActionQueueMoveRequest(TEXT("RunGuidePlayerToTable"), TableID);

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunGuidePlayerToTable"), TableID, false))
	{
		return;
	}

	if (bFarewellBowInProgress)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Return/move to counter blocked because farewell bow is in progress."));
		return;
	}

	if (IsGuidePlayerInProgress())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Ignored redundant guide request while already guiding."));
		UpdateLastKnownServiceState();
		return;
	}

	const FString TrimmedTableID = TableID.TrimStartAndEnd();
	if (IsSeatedForService())
	{
		const FString ServiceTableID = CurrentServiceTableID.TrimStartAndEnd();
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ActionQueue] Ignored guide request because customer is already in table area/seated. CurrentServiceTableID=%s"),
			*ServiceTableID);
		return;
	}

	PrintDebugMessage(FString::Printf(TEXT("Run guide player to table: %s"), *TrimmedTableID), FColor::Green);
	ClearActionQueue();
	bSeatingGuidanceCompleted = false;
	bPostSeatingMenuGuidanceDone = false;
	UpdateLastKnownServiceState();

	CacheOwnerComponents();
	if (!MoveComponent)
	{
		PrintDebugMessage(TEXT("NPCMoveToTableComponent not found"), FColor::Red);
		return;
	}

	MoveComponent->StartGuideToTable(TrimmedTableID);
	UpdateLastKnownServiceState();
}

void UNPCActionQueueComponent::RunGuidePlayerToSceneTag(const FString& TargetTag)
{
	LogActionQueueMoveRequest(TEXT("RunGuidePlayerToSceneTag"), TargetTag);

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunGuidePlayerToSceneTag"), TargetTag, IsCounterTarget(TargetTag)))
	{
		return;
	}

	if (bFarewellBowInProgress)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Return/move to counter blocked because farewell bow is in progress."));
		return;
	}

	const FString TrimmedTargetTag = TargetTag.TrimStartAndEnd();
	PrintDebugMessage(FString::Printf(TEXT("Run guide player to scene tag: %s"), *TrimmedTargetTag), FColor::Green);
	ClearActionQueue();

	CacheOwnerComponents();
	if (!MoveComponent)
	{
		PrintDebugMessage(TEXT("NPCMoveToTableComponent not found"), FColor::Red);
		return;
	}

	MoveComponent->StartGuideToSceneTag(TrimmedTargetTag);
	UpdateLastKnownServiceState();
}

void UNPCActionQueueComponent::RunStopGuidePlayer()
{
	PrintDebugMessage(TEXT("Stop guiding player"), FColor::Yellow);
	ClearActionQueue();

	CacheOwnerComponents();
	if (!MoveComponent)
	{
		PrintDebugMessage(TEXT("NPCMoveToTableComponent not found"), FColor::Red);
		return;
	}

	MoveComponent->StopGuidingPlayer();
	UpdateLastKnownServiceState();
}

void UNPCActionQueueComponent::HandleStrategyAction(const FString& ActionType, const FString& Item, int32 Quantity)
{
	FString NormalizedAction = ActionType;
	NormalizedAction.TrimStartAndEndInline();

	FString NormalizedItem = Item;
	NormalizedItem.TrimStartAndEndInline();

	UE_LOG(LogTemp, Warning, TEXT("HandleStrategyAction: ActionType=%s, Item=%s, Quantity=%d"), *NormalizedAction, *NormalizedItem, Quantity);
	LogActionQueueMoveRequest(TEXT("HandleStrategyAction"), FString::Printf(TEXT("Action=%s Item=%s"), *NormalizedAction, *NormalizedItem));

	const bool bIsCleanTableAction =
		NormalizedAction.Equals(TEXT("clean_table"), ESearchCase::IgnoreCase)
		|| NormalizedAction.Equals(TEXT("clear_table"), ESearchCase::IgnoreCase);
	const bool bIsContinueDiningAction =
		NormalizedAction.Equals(TEXT("continue_dining"), ESearchCase::IgnoreCase)
		|| NormalizedAction.Equals(TEXT("not_finished_dining"), ESearchCase::IgnoreCase);

	if (bFarewellBowInProgress && !bIsCleanTableAction && !bIsContinueDiningAction)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Strategy action ignored because farewell bow is in progress. Action=%s Item=%s"), *NormalizedAction, *NormalizedItem);
		return;
	}

	const bool bIsOrderAction =
		NormalizedAction.Equals(TEXT("place_order"), ESearchCase::IgnoreCase)
		|| NormalizedAction.Equals(TEXT("PlaceOrder"), ESearchCase::IgnoreCase)
		|| NormalizedAction.Equals(TEXT("order"), ESearchCase::IgnoreCase)
		|| NormalizedAction.Equals(TEXT("serve"), ESearchCase::IgnoreCase);
	const bool bIsReceptionPhase = IsReceptionPhaseForService();

	if (bIsCleanTableAction || bIsContinueDiningAction)
	{
		ACafeServiceManagerActor* MatchingServiceManager = nullptr;
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<ACafeServiceManagerActor> It(World); It; ++It)
			{
				ACafeServiceManagerActor* Candidate = *It;
				if (Candidate && Candidate->MaidNPCActor == GetOwner())
				{
					MatchingServiceManager = Candidate;
					break;
				}
			}
		}

		if (bIsContinueDiningAction)
		{
			if (!MatchingServiceManager
				|| !MatchingServiceManager->SubmitMealCompletionConfirmation(false))
			{
				UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] continue_dining response had no pending confirmation to cancel."));
			}
			return;
		}

		FString CleanupTable = NormalizedItem.IsEmpty()
			|| NormalizedItem.Equals(TEXT("none"), ESearchCase::IgnoreCase)
			|| NormalizedItem.Equals(TEXT("current_table"), ESearchCase::IgnoreCase)
			? CurrentServiceTableID.TrimStartAndEnd()
			: NormalizedItem;
		if (CleanupTable.IsEmpty())
		{
			CleanupTable = TEXT("current_table");
		}
		const bool bConfirmationAccepted = MatchingServiceManager
			&& MatchingServiceManager->SubmitMealCompletionConfirmation(true);
		if (!MatchingServiceManager
			|| !bConfirmationAccepted
			|| !MatchingServiceManager->AutonomyComponent
			|| !MatchingServiceManager->AutonomyComponent->RequestDiningAreaCleanup(CleanupTable))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ActionQueue] clean_table request could not start. Table=%s"),
				*CleanupTable);
		}
		return;
	}

	if (IsGuidePlayerInProgress())
	{
		if (NormalizedAction.Equals(TEXT("guide_player_to_table"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Ignored redundant guide request while already guiding."));
			UpdateLastKnownServiceState();
			return;
		}

		if (bIsOrderAction)
		{
			PendingPreSeatingOrderItem = NormalizedItem;
			UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Order requested while guiding. Deferring until seated. Item=%s"), *NormalizedItem);
			UpdateLastKnownServiceState();
			return;
		}

		if (NormalizedAction.Equals(TEXT("recommend"), ESearchCase::IgnoreCase)
			|| NormalizedAction.Equals(TEXT("explain"), ESearchCase::IgnoreCase)
			|| NormalizedAction.Equals(TEXT("speak"), ESearchCase::IgnoreCase)
			|| NormalizedAction.Equals(TEXT("greet"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Speech/social action allowed while guiding. Action=%s Item=%s"), *NormalizedAction, *NormalizedItem);
			UpdateLastKnownServiceState();
			return;
		}

		if (NormalizedAction.Equals(TEXT("guide_player_to_scene_tag"), ESearchCase::IgnoreCase)
			|| NormalizedAction.Equals(TEXT("counter_then_return"), ESearchCase::IgnoreCase)
			|| NormalizedAction.Equals(TEXT("table_route"), ESearchCase::IgnoreCase)
			|| NormalizedAction.Equals(TEXT("action_sequence"), ESearchCase::IgnoreCase)
			|| NormalizedAction.Equals(TEXT("move_near_player"), ESearchCase::IgnoreCase)
			|| NormalizedAction.Equals(TEXT("follow_player"), ESearchCase::IgnoreCase)
			|| NormalizedAction.Equals(TEXT("guide_to_table"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Movement action ignored while guiding. Keeping current guide. Action=%s Item=%s"), *NormalizedAction, *NormalizedItem);
			UpdateLastKnownServiceState();
			return;
		}
	}

	if (bIsReceptionPhase && IsReceptionSpeechAction(NormalizedAction))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ActionQueue] Reception menu inquiry/recommendation allowed without guiding. Action=%s Item=%s"),
			*NormalizedAction,
			*NormalizedItem);
		UpdateLastKnownServiceState();
		return;
	}

	if (bIsOrderAction && !IsSeatedForService())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ActionQueue] Pre-seating concrete order detected. Guiding player first. Item=%s"),
			*NormalizedItem);
		ClearPendingOrderState();
		RunGuidePlayerToTable(TEXT("nearest"));
		return;
	}

	if (bIsOrderAction)
	{
		if (bSuppressNextOrderConfirmSpeechForStrategyReply)
		{
			bSuppressNextOrderConfirmSpeech = true;
			bSuppressNextOrderConfirmSpeechForStrategyReply = false;
		}

		const bool bShouldSpeakOrderConfirm = !bSuppressNextOrderConfirmSpeech;
		bSuppressNextOrderConfirmSpeech = false;
		RunOrderFulfillmentToCurrentTableInternal(NormalizedItem, bShouldSpeakOrderConfirm);
		return;
	}

	if (NormalizedAction.Equals(TEXT("action_sequence"), ESearchCase::IgnoreCase))
	{
		RunActionSequenceFromJson(NormalizedItem);
		return;
	}

	if (NormalizedAction.Equals(TEXT("follow_player"), ESearchCase::IgnoreCase))
	{
		RunStartFollowPlayer(160.0f);
		return;
	}

	if (NormalizedAction.Equals(TEXT("stop_follow_player"), ESearchCase::IgnoreCase))
	{
		RunStopFollowPlayer();
		return;
	}

	if (NormalizedAction.Equals(TEXT("guide_player_to_table"), ESearchCase::IgnoreCase))
	{
		const FString GuideTarget = NormalizedItem.IsEmpty() ? TEXT("nearest") : NormalizedItem;
		if (bIsReceptionPhase)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ActionQueue] Reception dining intent confirmed. Starting guide to table. Item=%s"),
				*GuideTarget);
		}

		RunGuidePlayerToTable(GuideTarget);
		return;
	}

	if (NormalizedAction.Equals(TEXT("guide_player_to_scene_tag"), ESearchCase::IgnoreCase))
	{
		RunGuidePlayerToSceneTag(NormalizedItem);
		return;
	}

	if (NormalizedAction.Equals(TEXT("stop_guide_player"), ESearchCase::IgnoreCase))
	{
		RunStopGuidePlayer();
		return;
	}

	if (NormalizedAction.Equals(TEXT("move_near_player"), ESearchCase::IgnoreCase))
	{
		RunMoveNearPlayer(140.0f);
		return;
	}

	if (NormalizedAction.Equals(TEXT("counter_then_return"), ESearchCase::IgnoreCase))
	{
		if (NormalizedItem.IsEmpty() || NormalizedItem.Equals(TEXT("current_table"), ESearchCase::IgnoreCase))
		{
			RunCounterAndReturnToCurrentTable();
		}
		else
		{
			RunCounterAndReturnToTable(NormalizedItem);
		}
		return;
	}

	if (NormalizedAction.Equals(TEXT("table_route"), ESearchCase::IgnoreCase))
	{
		RunTableRouteFromString(NormalizedItem);
		return;
	}

	if (NormalizedAction.Equals(TEXT("guide_to_table"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogTemp, Warning, TEXT("guide_to_table is legacy/unsupported in HandleStrategyAction. Item=%s"), *NormalizedItem);
		return;
	}

	if (NormalizedAction.Equals(TEXT("greet"), ESearchCase::IgnoreCase)
		|| NormalizedAction.Equals(TEXT("recommend"), ESearchCase::IgnoreCase)
		|| NormalizedAction.Equals(TEXT("explain"), ESearchCase::IgnoreCase)
		|| NormalizedAction.Equals(TEXT("speak"), ESearchCase::IgnoreCase))
	{
		if (IsSeatedForService()
			&& (NormalizedAction.Equals(TEXT("recommend"), ESearchCase::IgnoreCase)
				|| NormalizedAction.Equals(TEXT("explain"), ESearchCase::IgnoreCase)
				|| NormalizedAction.Equals(TEXT("speak"), ESearchCase::IgnoreCase)))
		{
			ClearPostSeatingMenuGuidanceTimers();
		}

		UE_LOG(LogTemp, Warning, TEXT("Social legacy action received: %s, item=%s"), *NormalizedAction, *NormalizedItem);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("Unknown strategy action: %s, item=%s, quantity=%d"), *NormalizedAction, *NormalizedItem, Quantity);
}

void UNPCActionQueueComponent::RunTableRouteFromString(const FString& RouteString)
{
	LogActionQueueMoveRequest(TEXT("RunTableRouteFromString"), RouteString);

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunTableRouteFromString"), RouteString, IsCounterTarget(RouteString)))
	{
		return;
	}

	if (bFarewellBowInProgress)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Return/move to counter blocked because farewell bow is in progress."));
		return;
	}

	const FString TrimmedRouteString = RouteString.TrimStartAndEnd();
	PrintDebugMessage(FString::Printf(TEXT("Run table route: %s"), *TrimmedRouteString), FColor::Green);

	CancelFollowingForExplicitCommand();
	ClearActionQueue();

	TArray<FString> RouteTableIDs;
	TrimmedRouteString.ParseIntoArray(RouteTableIDs, TEXT(","), true);
	for (FString TableID : RouteTableIDs)
	{
		TableID = TableID.TrimStartAndEnd();
		if (TableID.IsEmpty())
		{
			continue;
		}

		PrintDebugMessage(FString::Printf(TEXT("Enqueue route table: %s"), *TableID), FColor::Green);
		EnqueueMoveToTableID(TableID);
	}

	if (ActionQueue.Num() <= 0)
	{
		PrintDebugMessage(TEXT("Table route is empty"), FColor::Red);
		return;
	}

	StartActionQueue();
}

void UNPCActionQueueComponent::RunActionSequenceFromJson(const FString& SequenceJson)
{
	LogActionQueueMoveRequest(TEXT("RunActionSequenceFromJson"), SequenceJson);

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunActionSequenceFromJson"), SequenceJson, IsCounterTarget(SequenceJson)))
	{
		return;
	}

	if (bFarewellBowInProgress)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Return/move to counter blocked because farewell bow is in progress."));
		return;
	}

	const FString TrimmedSequenceJson = SequenceJson.TrimStartAndEnd();
	PrintDebugMessage(FString::Printf(TEXT("Run action sequence: %s"), *TrimmedSequenceJson), FColor::Green);

	TArray<TSharedPtr<FJsonValue>> SequenceSteps;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TrimmedSequenceJson);
	if (!FJsonSerializer::Deserialize(Reader, SequenceSteps))
	{
		PrintDebugMessage(TEXT("Action sequence JSON parse failed"), FColor::Red);
		return;
	}

	bool bFoundFirstValidStep = false;
	bool bFirstValidStepIsDirectControl = false;
	bool bFirstValidStepReturnsNPCToCounter = false;
	for (const TSharedPtr<FJsonValue>& StepValue : SequenceSteps)
	{
		const TSharedPtr<FJsonObject> StepObject = StepValue.IsValid() ? StepValue->AsObject() : nullptr;
		if (!StepObject.IsValid())
		{
			continue;
		}

		FString StepType;
		if (!StepObject->TryGetStringField(TEXT("type"), StepType))
		{
			continue;
		}

		StepType = StepType.TrimStartAndEnd();
		if (StepType.IsEmpty())
		{
			continue;
		}

		bFoundFirstValidStep = true;
		bFirstValidStepIsDirectControl =
			StepType.Equals(TEXT("follow_player"), ESearchCase::IgnoreCase)
			|| StepType.Equals(TEXT("stop_follow_player"), ESearchCase::IgnoreCase)
			|| StepType.Equals(TEXT("guide_player_to_table"), ESearchCase::IgnoreCase)
			|| StepType.Equals(TEXT("guide_player_to_scene_tag"), ESearchCase::IgnoreCase)
			|| StepType.Equals(TEXT("stop_guide_player"), ESearchCase::IgnoreCase);

		FString FirstTarget;
		StepObject->TryGetStringField(TEXT("target"), FirstTarget);
		bFirstValidStepReturnsNPCToCounter =
			StepType.Equals(TEXT("move_to_scene_tag"), ESearchCase::IgnoreCase)
			&& IsCounterTarget(FirstTarget);
		break;
	}

	if (bFirstValidStepReturnsNPCToCounter)
	{
		CancelReceptionForExplicitNPCCommand(TEXT("explicit_return_to_counter"));
	}

	if (bFoundFirstValidStep && !bFirstValidStepIsDirectControl)
	{
		CancelFollowingForExplicitCommand();
	}

	ClearActionQueue();

	for (const TSharedPtr<FJsonValue>& StepValue : SequenceSteps)
	{
		const TSharedPtr<FJsonObject> StepObject = StepValue.IsValid() ? StepValue->AsObject() : nullptr;
		if (!StepObject.IsValid())
		{
			continue;
		}

		FString StepType;
		if (!StepObject->TryGetStringField(TEXT("type"), StepType))
		{
			continue;
		}
		StepType = StepType.TrimStartAndEnd();

		FString Target;
		StepObject->TryGetStringField(TEXT("target"), Target);
		Target = Target.TrimStartAndEnd();

		if (StepType.Equals(TEXT("move_to_table"), ESearchCase::IgnoreCase))
		{
			PrintDebugMessage(FString::Printf(TEXT("Enqueue sequence step: move_to_table / %s"), *Target), FColor::Green);
			EnqueueMoveToTableID(Target);
		}
		else if (StepType.Equals(TEXT("move_to_scene_tag"), ESearchCase::IgnoreCase))
		{
			PrintDebugMessage(FString::Printf(TEXT("Enqueue sequence step: move_to_scene_tag / %s"), *Target), FColor::Green);
			EnqueueMoveToSceneTag(Target);
		}
		else if (StepType.Equals(TEXT("move_to_furniture_tag"), ESearchCase::IgnoreCase))
		{
			PrintDebugMessage(FString::Printf(TEXT("Enqueue sequence step: move_to_furniture_tag / %s"), *Target), FColor::Green);
			EnqueueMoveToFurnitureTag(Target);
		}
		else if (StepType.Equals(TEXT("move_to_furniture_id"), ESearchCase::IgnoreCase))
		{
			PrintDebugMessage(FString::Printf(TEXT("Enqueue sequence step: move_to_furniture_id / %s"), *Target), FColor::Green);
			EnqueueMoveToFurnitureID(Target);
		}
		else if (StepType.Equals(TEXT("move_near_player"), ESearchCase::IgnoreCase))
		{
			double StopDistance = 140.0;
			StepObject->TryGetNumberField(TEXT("distance"), StopDistance);
			PrintDebugMessage(TEXT("Enqueue sequence step: move_near_player / player"), FColor::Green);
			EnqueueMoveNearPlayer(static_cast<float>(StopDistance));
		}
		else if (StepType.Equals(TEXT("follow_player"), ESearchCase::IgnoreCase))
		{
			double DesiredDistance = 160.0;
			StepObject->TryGetNumberField(TEXT("distance"), DesiredDistance);
			RunStartFollowPlayer(static_cast<float>(DesiredDistance));
			return;
		}
		else if (StepType.Equals(TEXT("stop_follow_player"), ESearchCase::IgnoreCase))
		{
			RunStopFollowPlayer();
			return;
		}
		else if (StepType.Equals(TEXT("guide_player_to_table"), ESearchCase::IgnoreCase))
		{
			RunGuidePlayerToTable(Target);
			return;
		}
		else if (StepType.Equals(TEXT("guide_player_to_scene_tag"), ESearchCase::IgnoreCase))
		{
			RunGuidePlayerToSceneTag(Target);
			return;
		}
		else if (StepType.Equals(TEXT("stop_guide_player"), ESearchCase::IgnoreCase))
		{
			RunStopGuidePlayer();
			return;
		}
		else if (StepType.Equals(TEXT("wait"), ESearchCase::IgnoreCase))
		{
			double Seconds = 0.0;
			StepObject->TryGetNumberField(TEXT("seconds"), Seconds);
			PrintDebugMessage(FString::Printf(TEXT("Enqueue sequence step: wait / %.1f"), Seconds), FColor::Green);
			EnqueueWait(static_cast<float>(Seconds));
		}
		else if (StepType.Equals(TEXT("face_player"), ESearchCase::IgnoreCase))
		{
			PrintDebugMessage(TEXT("Enqueue sequence step: face_player"), FColor::Green);
			EnqueueFacePlayer();
		}
		else
		{
			PrintDebugMessage(FString::Printf(TEXT("Unsupported action sequence step: %s"), *StepType), FColor::Yellow);
		}
	}

	if (ActionQueue.Num() <= 0)
	{
		PrintDebugMessage(TEXT("Action sequence is empty"), FColor::Red);
		return;
	}

	StartActionQueue();
}

void UNPCActionQueueComponent::RunCounterAndReturnToCurrentTable()
{
	LogActionQueueMoveRequest(TEXT("RunCounterAndReturnToCurrentTable"), ResolveCurrentServiceTableID());

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunCounterAndReturnToCurrentTable"), TEXT("counter"), true))
	{
		return;
	}

	if (BlockReturnToCounterIfFarewellBowInProgress())
	{
		return;
	}

	CancelFollowingForExplicitCommand();

	const FString ResolvedTableID = ResolveCurrentServiceTableID();
	if (ResolvedTableID.IsEmpty())
	{
		PrintDebugMessage(TEXT("Current table not known; returning to player instead"), FColor::Yellow);
		RunCounterAndReturnToPlayer();
		return;
	}

	CurrentServiceTableID = ResolvedTableID;
	RunCounterAndReturnToTable(ResolvedTableID);
}

void UNPCActionQueueComponent::RunOrderFulfillmentToCurrentTable(const FString& OrderItem)
{
	RunOrderFulfillmentToCurrentTableInternal(OrderItem, true);
}

void UNPCActionQueueComponent::RunOrderFulfillmentToCurrentTableInternal(const FString& OrderItem, bool bSpeakOrderConfirm)
{
	LogActionQueueMoveRequest(TEXT("RunOrderFulfillmentToCurrentTable"), OrderItem);

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunOrderFulfillmentToCurrentTable"), OrderItem, false))
	{
		return;
	}

	if (BlockReturnToCounterIfFarewellBowInProgress())
	{
		return;
	}

	const FString TrimmedOrderItem = OrderItem.TrimStartAndEnd();
	const int32 ResolvedOrderPrice = ResolveOrderPrice(TrimmedOrderItem);
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Order item: %s"), *TrimmedOrderItem);
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Order price: %d"), ResolvedOrderPrice);

	const FString ResolvedTableID = ResolveCurrentServiceTableID();
	if (ResolvedTableID.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Place order: %s"), *TrimmedOrderItem);
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Current service table not known; returning to player instead"));
		ClearPendingOrderState();
		RunCounterAndReturnToPlayer();
		return;
	}

	CancelFollowingForExplicitCommand();
	ClearActionQueue();

	CurrentServiceTableID = ResolvedTableID;
	PendingOrderReturnTableID = ResolvedTableID;
	PendingOrderItem = TrimmedOrderItem;
	PendingOrderPrice = ResolvedOrderPrice;
	bPendingOrderFulfillment = true;
	OnOrderStarted.Broadcast(PendingOrderReturnTableID, PendingOrderItem);

	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Place order: %s"), *TrimmedOrderItem);
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Order return table: %s"), *PendingOrderReturnTableID);
	if (bSpeakOrderConfirm)
	{
		BroadcastServiceSpeech(OrderConfirmLine);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Order confirm speech suppressed because strategy reply will speak."));
	}

	EnqueueMoveToSceneTag(TEXT("counter"));
	EnqueueWait(OrderPreparationSeconds);
	EnqueueMoveToTableID(PendingOrderReturnTableID);
	StartActionQueue();
}

void UNPCActionQueueComponent::RunPostSeatingMenuGuidance()
{
	if (bPostSeatingMenuGuidanceDone)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Post seating menu guidance already done; skipping"));
		return;
	}

	if (!IsSeatedForService())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Post seating menu guidance ignored: customer is not seated"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Post seating menu guidance requested"));
	bPostSeatingMenuGuidanceDone = true;
	ClearPostSeatingMenuGuidanceTimers();

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Post seating menu guidance failed: world not found"));
		return;
	}

	const float GuidanceDelay = FMath::Max(FMath::Max(PostSeatingMenuGuidanceDelay, GuideArrivedSeatGestureDuration), 0.0f);
	if (GuidanceDelay <= KINDA_SMALL_NUMBER)
	{
		HandlePostSeatingMenuGuidanceTimerFinished();
		return;
	}

	World->GetTimerManager().SetTimer(
		PostSeatingMenuGuidanceTimerHandle,
		this,
		&UNPCActionQueueComponent::HandlePostSeatingMenuGuidanceTimerFinished,
		GuidanceDelay,
		false);
}

void UNPCActionQueueComponent::GuideSeatPlayerOnce()
{
	const FName GestureName = GuideArrivedSeatGestureName.IsNone()
		? FName(TEXT("guide_seat_player"))
		: GuideArrivedSeatGestureName;

	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Guide arrived seat gesture requested: %s"), *GestureName.ToString());
	BroadcastQueueGesture(GestureName);
}

void UNPCActionQueueComponent::SetCurrentServiceTableID(const FString& TableID)
{
	CurrentServiceTableID = TableID.TrimStartAndEnd();
	if (CurrentServiceTableID.IsEmpty())
	{
		bSeatingGuidanceCompleted = false;
		bPostSeatingMenuGuidanceDone = false;
	}
	else
	{
		bSeatingGuidanceCompleted = true;
	}
	UpdateLastKnownServiceState();
}

void UNPCActionQueueComponent::PrepareForNewReception()
{
	const FString PreviousTableID = CurrentServiceTableID.TrimStartAndEnd();
	const bool bHadStaleTableState =
		bPlayerInTableArea
		|| bSeatingGuidanceCompleted
		|| !PreviousTableID.IsEmpty();

	bPlayerInTableArea = false;
	bSeatingGuidanceCompleted = false;
	bPostSeatingMenuGuidanceDone = false;
	CurrentServiceTableID = TEXT("");
	SetMealCompletionConfirmationPending(false, TEXT(""));
	SetDiningServiceCompleted(false, TEXT(""));
	UpdateLastKnownServiceState();

	if (bHadStaleTableState)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[ActionQueue] Cleared pre-reception table state. PreviousTableID=%s"),
			*PreviousTableID);
	}
}

void UNPCActionQueueComponent::SetPlayerInTableArea(bool bInArea, const FString& TableID)
{
	const FString TrimmedTableID = TableID.TrimStartAndEnd();
	if (bInArea)
	{
		const FString ActiveTableID = CurrentServiceTableID.TrimStartAndEnd();
		if (!ActiveTableID.IsEmpty()
			&& !TrimmedTableID.IsEmpty()
			&& !ActiveTableID.Equals(TrimmedTableID, ESearchCase::IgnoreCase))
		{
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[ActionQueue] Ignored unrelated table enter. ActiveTable=%s EnteredTable=%s"),
				*ActiveTableID,
				*TrimmedTableID);
			return;
		}
		const bool bSameTable =
			TrimmedTableID.IsEmpty()
			|| CurrentServiceTableID.TrimStartAndEnd().Equals(TrimmedTableID, ESearchCase::IgnoreCase);
		if (bPlayerInTableArea && bSameTable)
		{
			const FString LoggedTableID = TrimmedTableID.IsEmpty() ? CurrentServiceTableID : TrimmedTableID;
			UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Duplicate table enter ignored. TableID=%s"), *LoggedTableID);
			return;
		}

		bPlayerInTableArea = true;
		if (!TrimmedTableID.IsEmpty())
		{
			CurrentServiceTableID = TrimmedTableID;
		}

		UpdateLastKnownServiceState();
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ActionQueue] Player entered table area without completing seating guidance. CurrentServiceTableID=%s"),
			*CurrentServiceTableID);
		return;
	}

	const FString ActiveTableID = CurrentServiceTableID.TrimStartAndEnd();
	if (!ActiveTableID.IsEmpty()
		&& !TrimmedTableID.IsEmpty()
		&& !ActiveTableID.Equals(TrimmedTableID, ESearchCase::IgnoreCase))
	{
		UE_LOG(
			LogTemp,
			Verbose,
			TEXT("[ActionQueue] Ignored unrelated table leave. ActiveTable=%s LeftTable=%s"),
			*ActiveTableID,
			*TrimmedTableID);
		return;
	}

	if (!bPlayerInTableArea)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Duplicate table leave ignored."));
		return;
	}

	bPlayerInTableArea = false;
	if (!bSeatingGuidanceCompleted)
	{
		CurrentServiceTableID = TEXT("");
	}
	UpdateLastKnownServiceState();
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ActionQueue] Player left table area. SeatingGuidanceCompleted=%d CurrentServiceTableID=%s"),
		bSeatingGuidanceCompleted ? 1 : 0,
		*CurrentServiceTableID);
}

void UNPCActionQueueComponent::NotifyPlayerEnteredTableArea(const FString& TableID)
{
	SetPlayerInTableArea(true, TableID);
}

void UNPCActionQueueComponent::NotifyPlayerLeftTableArea(const FString& TableID)
{
	SetPlayerInTableArea(false, TableID);
}

void UNPCActionQueueComponent::NotifyPlayerDepartureConfirmed(const FString& TableID)
{
	const FString ConfirmedTableID = TableID.TrimStartAndEnd();
	const FString ActiveTableID = CurrentServiceTableID.TrimStartAndEnd();
	if (!ConfirmedTableID.IsEmpty()
		&& !ActiveTableID.IsEmpty()
		&& !ActiveTableID.Equals(ConfirmedTableID, ESearchCase::IgnoreCase))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ActionQueue] Departure table differs from active service table. Active=%s Confirmed=%s"),
			*ActiveTableID,
			*ConfirmedTableID);
	}

	bPlayerInTableArea = false;
	bSeatingGuidanceCompleted = false;
	bPostSeatingMenuGuidanceDone = false;
	CurrentServiceTableID = TEXT("");
	SetMealCompletionConfirmationPending(false, TEXT(""));
	SetDiningServiceCompleted(true, ConfirmedTableID);
	UpdateLastKnownServiceState();
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[ActionQueue] Confirmed player departure; cleared seated service state. Table=%s"),
		*ConfirmedTableID);
}

void UNPCActionQueueComponent::SetMealCompletionConfirmationPending(bool bPending, const FString& TableID)
{
	bLastKnownMealCompletionConfirmationPending = bPending;
	LastKnownMealCompletionConfirmationTableID = bPending
		? TableID.TrimStartAndEnd()
		: TEXT("");
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[ActionQueue] meal completion confirmation pending=%s Table=%s"),
		bPending ? TEXT("true") : TEXT("false"),
		LastKnownMealCompletionConfirmationTableID.IsEmpty()
			? TEXT("none")
			: *LastKnownMealCompletionConfirmationTableID);
}

void UNPCActionQueueComponent::SetDiningServiceCompleted(bool bCompleted, const FString& TableID)
{
	bLastKnownDiningServiceCompleted = bCompleted;
	LastKnownCompletedServiceTableID = bCompleted
		? TableID.TrimStartAndEnd()
		: TEXT("");
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[ActionQueue] dining service completed=%s Table=%s"),
		bCompleted ? TEXT("true") : TEXT("false"),
		LastKnownCompletedServiceTableID.IsEmpty()
			? TEXT("none")
			: *LastKnownCompletedServiceTableID);
}

FString UNPCActionQueueComponent::GetCurrentServiceTableID() const
{
	return CurrentServiceTableID;
}

bool UNPCActionQueueComponent::IsSeatedForService() const
{
	bool bPlayerActuallySeated = false;
	if (const AActor* PlayerActor = FindPlayerActor())
	{
		if (const UPlayerSeatComponent* SeatComponent = PlayerActor->FindComponentByClass<UPlayerSeatComponent>())
		{
			bPlayerActuallySeated = SeatComponent->bIsSeated;
		}
	}

	return bSeatingGuidanceCompleted || bPlayerActuallySeated;
}

bool UNPCActionQueueComponent::IsReceptionPhaseForService() const
{
	return !IsSeatedForService() && !IsGuidePlayerInProgress();
}

bool UNPCActionQueueComponent::IsReceptionSpeechAction(const FString& ActionType) const
{
	return ActionType.Equals(TEXT("recommend"), ESearchCase::IgnoreCase)
		|| ActionType.Equals(TEXT("explain"), ESearchCase::IgnoreCase)
		|| ActionType.Equals(TEXT("speak"), ESearchCase::IgnoreCase)
		|| ActionType.Equals(TEXT("greet"), ESearchCase::IgnoreCase);
}

bool UNPCActionQueueComponent::IsGuidePlayerInProgress() const
{
	const UNPCMoveToTableComponent* ResolvedMoveComponent = MoveComponent;
	if (!ResolvedMoveComponent)
	{
		if (const AActor* Owner = GetOwner())
		{
			ResolvedMoveComponent = Owner->FindComponentByClass<UNPCMoveToTableComponent>();
		}
	}

	return ResolvedMoveComponent && ResolvedMoveComponent->IsGuidingPlayer();
}

bool UNPCActionQueueComponent::IsFarewellBowInProgress() const
{
	return bFarewellBowInProgress;
}

bool UNPCActionQueueComponent::IsActionQueuePausedForFarewellBow() const
{
	return bActionQueuePausedForFarewellBow;
}

bool UNPCActionQueueComponent::IsBowMovementLockActive() const
{
	return bBowMovementLockActive;
}

bool UNPCActionQueueComponent::IsBlockingGestureInProgress() const
{
	return bBlockingGestureInProgress;
}

bool UNPCActionQueueComponent::IsBusyForAutonomy() const
{
	const bool bMoveComponentBusy = MoveComponent
		&& (MoveComponent->bIsMovingToTable
			|| MoveComponent->IsFollowingPlayer()
			|| MoveComponent->IsGuidingPlayer()
			|| MoveComponent->IsMovementPausedByServiceGesture());

	return bIsRunningQueue
		|| bWaitingForMove
		|| bMoveComponentBusy
		|| bPendingOrderFulfillment
		|| bFarewellBowInProgress
		|| bActionQueuePausedForFarewellBow
		|| bBowMovementLockActive
		|| bBlockingGestureInProgress
		|| bQueuedAnimationInProgress;
}

FString UNPCActionQueueComponent::GetCurrentBlockingGestureName() const
{
	return CurrentBlockingGestureName;
}

void UNPCActionQueueComponent::NotifyServiceGestureFinished(const FString& GestureName)
{
	const FString RequestedGestureName = GestureName.TrimStartAndEnd();
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] NotifyServiceGestureFinished received: %s"), *RequestedGestureName);

	if (!bBlockingGestureInProgress)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ActionQueue] NotifyServiceGestureFinished ignored because no blocking gesture is in progress. Received=%s"),
			*RequestedGestureName);
		return;
	}

	if (!RequestedGestureName.Equals(CurrentBlockingGestureName, ESearchCase::IgnoreCase))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ActionQueue] NotifyServiceGestureFinished ignored. Expected=%s Received=%s"),
			*CurrentBlockingGestureName,
			*RequestedGestureName);
		return;
	}

	if (bBlockingGestureFinishHandled)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Blocking gesture finished by notify: %s"), *RequestedGestureName);
	CompleteBlockingGesture(RequestedGestureName, false);
}

bool UNPCActionQueueComponent::IsLastKnownSeatedForService()
{
	return bLastKnownSeatedForService;
}

bool UNPCActionQueueComponent::IsLastKnownPlayerInTableArea()
{
	return bLastKnownPlayerInTableArea;
}

bool UNPCActionQueueComponent::IsLastKnownGuidePlayerInProgress()
{
	return bLastKnownGuidePlayerInProgress;
}

bool UNPCActionQueueComponent::IsLastKnownSeatingGuidanceCompleted()
{
	return bLastKnownSeatingGuidanceCompleted;
}

FString UNPCActionQueueComponent::GetLastKnownCurrentServiceTableID()
{
	return LastKnownCurrentServiceTableID;
}

FString UNPCActionQueueComponent::GetLastKnownGuideTargetID()
{
	return LastKnownGuideTargetID;
}

bool UNPCActionQueueComponent::IsLastKnownMealCompletionConfirmationPending()
{
	return bLastKnownMealCompletionConfirmationPending;
}

FString UNPCActionQueueComponent::GetLastKnownMealCompletionConfirmationTableID()
{
	return LastKnownMealCompletionConfirmationTableID;
}

bool UNPCActionQueueComponent::IsLastKnownDiningServiceCompleted()
{
	return bLastKnownDiningServiceCompleted;
}

FString UNPCActionQueueComponent::GetLastKnownCompletedServiceTableID()
{
	return LastKnownCompletedServiceTableID;
}

void UNPCActionQueueComponent::SetSuppressNextOrderConfirmSpeechForStrategyReply(bool bSuppress)
{
	bSuppressNextOrderConfirmSpeechForStrategyReply = bSuppress;
}

void UNPCActionQueueComponent::SetSuppressNextGuideStartedSpeechForStrategyReply(bool bSuppress)
{
	bSuppressNextGuideStartedSpeechForStrategyReply = bSuppress;
}

bool UNPCActionQueueComponent::ConsumeSuppressNextGuideStartedSpeechForStrategyReply()
{
	if (!bSuppressNextGuideStartedSpeechForStrategyReply)
	{
		return false;
	}

	bSuppressNextGuideStartedSpeechForStrategyReply = false;
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Guide started speech suppressed because strategy reply already spoke."));
	return true;
}

void UNPCActionQueueComponent::CacheOwnerComponents()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		MoveComponent = nullptr;
		FaceTargetComponent = nullptr;
		FoodCarryComponent = nullptr;
		return;
	}

	MoveComponent = Owner->FindComponentByClass<UNPCMoveToTableComponent>();
	FaceTargetComponent = Owner->FindComponentByClass<UNPCFaceTargetComponent>();
	FoodCarryComponent = Owner->FindComponentByClass<UNPCFoodCarryComponent>();
}

UNPCFoodCarryComponent* UNPCActionQueueComponent::ResolveFoodCarryComponent()
{
	if (!FoodCarryComponent)
	{
		CacheOwnerComponents();
	}

	return FoodCarryComponent;
}

void UNPCActionQueueComponent::CancelFollowingForExplicitCommand()
{
	CacheOwnerComponents();
	if (MoveComponent)
	{
		MoveComponent->CancelFollowingForExplicitCommand();
	}
}

void UNPCActionQueueComponent::UpdateLastKnownServiceState() const
{
	const UNPCMoveToTableComponent* ResolvedMoveComponent = MoveComponent;
	if (!ResolvedMoveComponent)
	{
		if (const AActor* Owner = GetOwner())
		{
			ResolvedMoveComponent = Owner->FindComponentByClass<UNPCMoveToTableComponent>();
		}
	}

	bLastKnownSeatedForService = IsSeatedForService();
	bLastKnownPlayerInTableArea = bPlayerInTableArea;
	bLastKnownGuidePlayerInProgress = ResolvedMoveComponent && ResolvedMoveComponent->IsGuidingPlayer();
	bLastKnownSeatingGuidanceCompleted = bSeatingGuidanceCompleted;
	LastKnownCurrentServiceTableID = CurrentServiceTableID.TrimStartAndEnd();
	LastKnownGuideTargetID = (ResolvedMoveComponent && ResolvedMoveComponent->IsGuidingPlayer())
		? ResolvedMoveComponent->GetCurrentGuideTargetID()
		: TEXT("");
}

bool UNPCActionQueueComponent::CancelReceptionForExplicitNPCCommand(const FString& Reason) const
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return false;
	}

	for (TActorIterator<ACafeServiceManagerActor> It(World); It; ++It)
	{
		ACafeServiceManagerActor* ServiceManager = *It;
		if (ServiceManager && ServiceManager->MaidNPCActor == Owner)
		{
			return ServiceManager->CancelReceptionForExplicitNPCCommand(Reason);
		}
	}

	return false;
}

void UNPCActionQueueComponent::BindMoveCompletedEvent()
{
	if (!MoveComponent)
	{
		CacheOwnerComponents();
	}

	if (MoveComponent)
	{
		MoveComponent->OnNPCMoveCompleted.RemoveDynamic(this, &UNPCActionQueueComponent::HandleNPCMoveCompleted);
		MoveComponent->OnNPCMoveCompleted.AddDynamic(this, &UNPCActionQueueComponent::HandleNPCMoveCompleted);
		MoveComponent->OnGuideArrived.RemoveDynamic(this, &UNPCActionQueueComponent::HandleGuideArrived);
		MoveComponent->OnGuideArrived.AddDynamic(this, &UNPCActionQueueComponent::HandleGuideArrived);
	}
}

void UNPCActionQueueComponent::UnbindMoveCompletedEvent()
{
	if (MoveComponent)
	{
		MoveComponent->OnNPCMoveCompleted.RemoveDynamic(this, &UNPCActionQueueComponent::HandleNPCMoveCompleted);
		MoveComponent->OnGuideArrived.RemoveDynamic(this, &UNPCActionQueueComponent::HandleGuideArrived);
	}
}

void UNPCActionQueueComponent::EnqueueAction(const FNPCQueuedAction& Action)
{
	ActionQueue.Add(Action);
}

void UNPCActionQueueComponent::ExecuteNextQueuedAction()
{
	if (!bIsRunningQueue)
	{
		return;
	}

	const bool bQueueContainsCounterMove = DoesQueuedActionListContainCounterMove();
	if (BlockMovementIfBlockingGestureInProgress(TEXT("ExecuteNextQueuedAction"), bQueueContainsCounterMove ? TEXT("counter") : TEXT("action_queue"), bQueueContainsCounterMove))
	{
		return;
	}

	if (bActionQueuePausedForFarewellBow || bFarewellBowInProgress)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Return/move to counter blocked because farewell bow is in progress."));
		return;
	}

	if (ActionQueue.Num() <= 0)
	{
		const bool bQueueSucceeded = !bCurrentQueueHadFailure;
		CurrentAction = FNPCQueuedAction();
		bWaitingForMove = false;
		bIsRunningQueue = false;
		bCurrentQueueHadFailure = false;
		PrintDebugMessage(TEXT("NPC action queue completed"), FColor::Cyan);
		OnActionQueueFinished.Broadcast(bQueueSucceeded);
		return;
	}

	CurrentAction = ActionQueue[0];
	ActionQueue.RemoveAt(0);
	bWaitingForMove = false;

	switch (CurrentAction.ActionType)
	{
	case ENPCQueuedActionType::MoveToTableID:
	case ENPCQueuedActionType::MoveToSceneTag:
	case ENPCQueuedActionType::MoveToFurnitureTag:
	case ENPCQueuedActionType::MoveToFurnitureID:
	case ENPCQueuedActionType::MoveNearPlayer:
		ExecuteMoveAction();
		break;

	case ENPCQueuedActionType::Wait:
		ExecuteWaitAction();
		break;

	case ENPCQueuedActionType::Animation:
		ExecuteAnimationAction();
		break;

	case ENPCQueuedActionType::AttachHeldProp:
	case ENPCQueuedActionType::RemoveHeldProp:
		ExecuteHeldPropAction();
		break;

	case ENPCQueuedActionType::PickUpPlacedFood:
	case ENPCQueuedActionType::PlaceCarriedFoodAtCounter:
		ExecuteFoodCarryAction();
		break;

	case ENPCQueuedActionType::FacePlayer:
		ExecuteFacePlayerAction();
		break;

	case ENPCQueuedActionType::Stop:
		StopActionQueue();
		break;

	default:
		CompleteCurrentAction(false, TEXT("Unsupported queued action"));
		break;
	}
}

void UNPCActionQueueComponent::ExecuteMoveAction()
{
	CacheOwnerComponents();
	BindMoveCompletedEvent();
	LogActionQueueMoveRequest(TEXT("ExecuteMoveAction"), CurrentAction.Target);

	const FString Target = CurrentAction.Target.TrimStartAndEnd();
	const bool bCurrentActionTargetsCounter = IsCounterMoveAction(CurrentAction);
	if (BlockMovementIfBlockingGestureInProgress(TEXT("ExecuteMoveAction"), bCurrentActionTargetsCounter ? TEXT("counter") : Target, bCurrentActionTargetsCounter))
	{
		bWaitingForMove = false;
		bIsRunningQueue = false;
		CurrentAction = FNPCQueuedAction();
		return;
	}

	if (bFarewellBowInProgress)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Return/move to counter blocked because farewell bow is in progress."));
		bWaitingForMove = false;
		bIsRunningQueue = false;
		CurrentAction = FNPCQueuedAction();
		return;
	}

	if (!MoveComponent)
	{
		CompleteCurrentAction(false, TEXT("NPCMoveToTableComponent not found"));
		return;
	}

	if (IsCounterMoveAction(CurrentAction) && BlockReturnToCounterIfFarewellBowInProgress())
	{
		bWaitingForMove = false;
		bIsRunningQueue = false;
		CurrentAction = FNPCQueuedAction();
		return;
	}

	const bool bNeedsStringTarget = CurrentAction.ActionType != ENPCQueuedActionType::MoveNearPlayer;
	if (bNeedsStringTarget && Target.IsEmpty())
	{
		CompleteCurrentAction(false, TEXT("Move target is empty"));
		return;
	}

	bool bMoveStarted = false;
	bWaitingForMove = true;
	MoveComponent->CancelFollowingForExplicitCommand();

	switch (CurrentAction.ActionType)
	{
	case ENPCQueuedActionType::MoveToTableID:
		bMoveStarted = MoveComponent->MoveToTableByID(Target);
		break;

	case ENPCQueuedActionType::MoveToSceneTag:
		bMoveStarted = MoveComponent->MoveToSceneTargetByTag(Target);
		break;

	case ENPCQueuedActionType::MoveToFurnitureTag:
		bMoveStarted = MoveComponent->MoveToFurnitureByTag(Target);
		break;

	case ENPCQueuedActionType::MoveToFurnitureID:
		bMoveStarted = MoveComponent->MoveToFurnitureByID(Target);
		break;

	case ENPCQueuedActionType::MoveNearPlayer:
		PrintDebugMessage(TEXT("Move near player"), FColor::Green);
		bMoveStarted = MoveComponent->MoveNearPlayer(CurrentAction.StopDistance);
		break;

	default:
		bWaitingForMove = false;
		CompleteCurrentAction(false, TEXT("Queued action is not a move"));
		return;
	}

	if (!bMoveStarted && bWaitingForMove)
	{
		bWaitingForMove = false;
		CompleteCurrentAction(false, FString::Printf(TEXT("Move action failed to start: %s"), *Target));
	}
}

void UNPCActionQueueComponent::ExecuteWaitAction()
{
	if (CurrentAction.WaitSeconds <= KINDA_SMALL_NUMBER)
	{
		PickUpFoodForPendingOrder();
		CompleteCurrentAction(true, TEXT(""));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		CompleteCurrentAction(false, TEXT("World not found for wait action"));
		return;
	}

	World->GetTimerManager().SetTimer(
		WaitTimerHandle,
		this,
		&UNPCActionQueueComponent::HandleWaitTimerFinished,
		CurrentAction.WaitSeconds,
		false);
}

void UNPCActionQueueComponent::ExecuteAnimationAction()
{
	if (!CurrentAction.Animation)
	{
		CompleteCurrentAction(false, TEXT("Queued animation is not configured"));
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		CompleteCurrentAction(false, TEXT("NPC owner not found for queued animation"));
		return;
	}

	USkeletalMeshComponent* MeshComponent = nullptr;
	TArray<USkeletalMeshComponent*> MeshComponents;
	OwnerActor->GetComponents<USkeletalMeshComponent>(MeshComponents);
	const USkeleton* AnimationSkeleton = CurrentAction.Animation->GetSkeleton();
	for (USkeletalMeshComponent* CandidateMesh : MeshComponents)
	{
		const USkeletalMesh* SkeletalMeshAsset = CandidateMesh ? CandidateMesh->GetSkeletalMeshAsset() : nullptr;
		if (CandidateMesh
			&& CandidateMesh->GetAnimInstance()
			&& SkeletalMeshAsset
			&& (!AnimationSkeleton || SkeletalMeshAsset->GetSkeleton() == AnimationSkeleton))
		{
			MeshComponent = CandidateMesh;
			break;
		}
	}
	if (!MeshComponent && AnimationSkeleton)
	{
		CompleteCurrentAction(false, FString::Printf(
			TEXT("No skeletal mesh compatible with queued animation %s (skeleton: %s)"),
			*CurrentAction.Animation->GetName(),
			*AnimationSkeleton->GetName()));
		return;
	}
	if (!MeshComponent)
	{
		if (ACharacter* CharacterOwner = Cast<ACharacter>(OwnerActor))
		{
			MeshComponent = CharacterOwner->GetMesh();
		}
	}
	if (!MeshComponent)
	{
		MeshComponent = OwnerActor->FindComponentByClass<USkeletalMeshComponent>();
	}

	UAnimInstance* AnimInstance = MeshComponent ? MeshComponent->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		CompleteCurrentAction(false, TEXT("NPC animation instance not found"));
		return;
	}

	const FName SlotName = CurrentAction.AnimationSlotName.IsNone()
		? FName(TEXT("DefaultSlot"))
		: CurrentAction.AnimationSlotName;
	const float PlayRate = FMath::Max(FMath::Abs(CurrentAction.AnimationPlayRate), KINDA_SMALL_NUMBER);
	ActiveQueuedAnimationMontage = AnimInstance->PlaySlotAnimationAsDynamicMontage(
		CurrentAction.Animation,
		SlotName,
		0.2f,
		0.2f,
		PlayRate,
		1,
		-1.0f,
		0.0f);

	if (!ActiveQueuedAnimationMontage)
	{
		CompleteCurrentAction(false, FString::Printf(
			TEXT("Failed to play queued animation: %s"),
			*CurrentAction.Animation->GetName()));
		return;
	}

	bQueuedAnimationInProgress = true;
	bQueuedAnimationPaused = false;
	ActiveQueuedAnimationInstance = AnimInstance;
	const float MontagePosition = AnimInstance->Montage_GetPosition(ActiveQueuedAnimationMontage);
	const float MontageRate = FMath::Max(FMath::Abs(AnimInstance->Montage_GetPlayRate(ActiveQueuedAnimationMontage)), KINDA_SMALL_NUMBER);
	const float MontageRemainingSeconds = FMath::Max(
		(ActiveQueuedAnimationMontage->GetPlayLength() - MontagePosition) / MontageRate,
		0.0f);
	const float WaitSeconds = MontageRemainingSeconds > KINDA_SMALL_NUMBER
		? MontageRemainingSeconds + 0.1f
		: CurrentAction.WaitSeconds;

	UWorld* World = GetWorld();
	if (!World)
	{
		StopQueuedAnimation(0.0f);
		CompleteCurrentAction(false, TEXT("World not found for queued animation"));
		return;
	}

	World->GetTimerManager().SetTimer(
		QueuedAnimationTimerHandle,
		this,
		&UNPCActionQueueComponent::HandleQueuedAnimationTimerFinished,
		FMath::Max(WaitSeconds, 0.1f),
		false);
}

void UNPCActionQueueComponent::ExecuteHeldPropAction()
{
	if (CurrentAction.ActionType == ENPCQueuedActionType::RemoveHeldProp)
	{
		RemoveActiveHeldProp();
		CompleteCurrentAction(true, TEXT(""));
		return;
	}

	if (CurrentAction.ActionType != ENPCQueuedActionType::AttachHeldProp || !CurrentAction.HeldPropMesh)
	{
		CompleteCurrentAction(false, TEXT("Held prop mesh is not configured"));
		return;
	}

	AActor* OwnerActor = GetOwner();
	FName ResolvedSocketName = CurrentAction.HeldPropSocketName;
	USkeletalMeshComponent* AttachmentMesh = ResolveHeldPropAttachmentMesh(
		CurrentAction.HeldPropSocketName,
		ResolvedSocketName);
	if (!OwnerActor || !AttachmentMesh)
	{
		CompleteCurrentAction(false, FString::Printf(
			TEXT("No skeletal mesh contains held prop socket/bone: %s"),
			*CurrentAction.HeldPropSocketName.ToString()));
		return;
	}

	RemoveActiveHeldProp();
	ActiveHeldPropComponent = NewObject<UStaticMeshComponent>(OwnerActor, NAME_None, RF_Transient);
	if (!ActiveHeldPropComponent)
	{
		CompleteCurrentAction(false, TEXT("Failed to create held prop component"));
		return;
	}

	OwnerActor->AddInstanceComponent(ActiveHeldPropComponent);
	ActiveHeldPropComponent->SetMobility(EComponentMobility::Movable);
	ActiveHeldPropComponent->SetStaticMesh(CurrentAction.HeldPropMesh);
	ActiveHeldPropComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ActiveHeldPropComponent->SetGenerateOverlapEvents(false);
	ActiveHeldPropComponent->SetCanEverAffectNavigation(false);
	ActiveHeldPropComponent->RegisterComponent();

	if (!ActiveHeldPropComponent->AttachToComponent(
		AttachmentMesh,
		FAttachmentTransformRules::KeepRelativeTransform,
		ResolvedSocketName))
	{
		RemoveActiveHeldProp();
		CompleteCurrentAction(false, TEXT("Failed to attach held prop to NPC mesh"));
		return;
	}

	ActiveHeldPropComponent->SetRelativeTransform(CurrentAction.HeldPropRelativeTransform);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[ActionQueue] held prop attached. Mesh=%s Socket=%s Location=%s Rotation=%s Scale=%s"),
		*CurrentAction.HeldPropMesh->GetName(),
		*ResolvedSocketName.ToString(),
		*CurrentAction.HeldPropRelativeTransform.GetLocation().ToCompactString(),
		*CurrentAction.HeldPropRelativeTransform.Rotator().ToCompactString(),
		*CurrentAction.HeldPropRelativeTransform.GetScale3D().ToCompactString());
	CompleteCurrentAction(true, TEXT(""));
}

void UNPCActionQueueComponent::ExecuteFoodCarryAction()
{
	UNPCFoodCarryComponent* ResolvedFoodCarryComponent = ResolveFoodCarryComponent();
	if (!ResolvedFoodCarryComponent)
	{
		CompleteCurrentAction(false, TEXT("NPCFoodCarryComponent not found for tableware action"));
		return;
	}

	if (CurrentAction.ActionType == ENPCQueuedActionType::PickUpPlacedFood)
	{
		AActor* TableActor = FindServiceTableActorByID(CurrentAction.Target);
		if (!TableActor)
		{
			CompleteCurrentAction(false, FString::Printf(
				TEXT("Table actor not found while collecting tableware: %s"),
				*CurrentAction.Target));
			return;
		}

		const bool bPickedUp = ResolvedFoodCarryComponent->PickUpPlacedFoodFromTable(TableActor);
		CompleteCurrentAction(
			bPickedUp,
			bPickedUp ? TEXT("") : TEXT("Failed to collect tableware from table"));
		return;
	}

	if (CurrentAction.ActionType == ENPCQueuedActionType::PlaceCarriedFoodAtCounter)
	{
		const bool bPlaced = ResolvedFoodCarryComponent->PlaceCarriedFoodAtCounter();
		CompleteCurrentAction(
			bPlaced,
			bPlaced ? TEXT("") : TEXT("Failed to place tableware at counter"));
		return;
	}

	CompleteCurrentAction(false, TEXT("Queued action is not a food carry action"));
}

void UNPCActionQueueComponent::ExecuteFacePlayerAction()
{
	AActor* PlayerActor = FindPlayerActor();
	if (!PlayerActor)
	{
		CompleteCurrentAction(false, TEXT("Player actor not found"));
		return;
	}

	CacheOwnerComponents();
	if (FaceTargetComponent)
	{
		FaceTargetComponent->StartFacingActor(PlayerActor);
	}
	else
	{
		FaceOwnerTowardActor(PlayerActor);
	}

	CompleteCurrentAction(true, TEXT(""));
}

void UNPCActionQueueComponent::CompleteCurrentAction(bool bSuccess, const FString& FailureReason)
{
	const bool bFailedPendingOrderReturn =
		!bSuccess
		&& bPendingOrderFulfillment
		&& CurrentAction.ActionType == ENPCQueuedActionType::MoveToTableID
		&& CurrentAction.Target.TrimStartAndEnd().Equals(PendingOrderReturnTableID.TrimStartAndEnd(), ESearchCase::IgnoreCase);

	if (!bSuccess)
	{
		bCurrentQueueHadFailure = true;
		const FString Message = FailureReason.IsEmpty() ? TEXT("NPC queued action failed") : FailureReason;
		PrintDebugMessage(Message, FColor::Red);
	}

	if (bFailedPendingOrderReturn)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Order return table failed: %s"), *PendingOrderReturnTableID);
		ClearPendingOrderState();
	}

	if (!bSuccess && bStopQueueOnActionFailure)
	{
		RemoveActiveHeldProp();
		ActionQueue.Reset();
		CurrentAction = FNPCQueuedAction();
		bWaitingForMove = false;
		bIsRunningQueue = false;
		ClearPendingOrderState();
		ClearOrderFulfillmentTimers();
		PrintDebugMessage(TEXT("NPC action queue stopped after failed action"), FColor::Red);
		bCurrentQueueHadFailure = false;
		OnActionQueueFinished.Broadcast(false);
		return;
	}

	CurrentAction = FNPCQueuedAction();
	bWaitingForMove = false;
	ExecuteNextQueuedAction();
}

bool UNPCActionQueueComponent::IsCurrentActionMove() const
{
	return CurrentAction.ActionType == ENPCQueuedActionType::MoveToTableID
		|| CurrentAction.ActionType == ENPCQueuedActionType::MoveToSceneTag
		|| CurrentAction.ActionType == ENPCQueuedActionType::MoveToFurnitureTag
		|| CurrentAction.ActionType == ENPCQueuedActionType::MoveToFurnitureID
		|| CurrentAction.ActionType == ENPCQueuedActionType::MoveNearPlayer;
}

FString UNPCActionQueueComponent::ResolveCurrentServiceTableID() const
{
	const FString TrimmedCurrentTableID = CurrentServiceTableID.TrimStartAndEnd();
	if (!TrimmedCurrentTableID.IsEmpty())
	{
		return TrimmedCurrentTableID;
	}

	UWorld* World = GetWorld();
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const AActor* Actor = *It;
			const UPlayerSeatComponent* SeatComponent = Actor ? Actor->FindComponentByClass<UPlayerSeatComponent>() : nullptr;
			if (SeatComponent && !SeatComponent->CurrentTableID.TrimStartAndEnd().IsEmpty())
			{
				return SeatComponent->CurrentTableID.TrimStartAndEnd();
			}
		}

		for (TActorIterator<ACafeServiceManagerActor> It(World); It; ++It)
		{
			const ACafeServiceManagerActor* ServiceManager = *It;
			if (ServiceManager && !ServiceManager->TargetTableID.TrimStartAndEnd().IsEmpty())
			{
				return ServiceManager->TargetTableID.TrimStartAndEnd();
			}
		}
	}

	return LastVisitedTableID.TrimStartAndEnd();
}

int32 UNPCActionQueueComponent::ResolveOrderPrice(const FString& OrderItem) const
{
	FString NormalizedOrderItem = OrderItem.TrimStartAndEnd();
	NormalizedOrderItem.ToLowerInline();

	if (NormalizedOrderItem.Contains(TEXT("coffee")) || NormalizedOrderItem.Contains(TEXT("コーヒー")))
	{
		return 500;
	}

	if (NormalizedOrderItem.Contains(TEXT("cake")) || NormalizedOrderItem.Contains(TEXT("ケーキ")))
	{
		return 700;
	}

	if (NormalizedOrderItem.Contains(TEXT("omurice")) || NormalizedOrderItem.Contains(TEXT("オムライス")))
	{
		return 1200;
	}

	if (NormalizedOrderItem.Contains(TEXT("curry")) || NormalizedOrderItem.Contains(TEXT("カレー")))
	{
		return 1000;
	}

	return DefaultOrderPrice;
}

AActor* UNPCActionQueueComponent::FindPlayerActor() const
{
	if (AActor* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		return PlayerPawn;
	}

	return UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
}

void UNPCActionQueueComponent::FaceOwnerTowardActor(AActor* TargetActor) const
{
	AActor* Owner = GetOwner();
	if (!Owner || !TargetActor)
	{
		return;
	}

	FVector Direction = TargetActor->GetActorLocation() - Owner->GetActorLocation();
	Direction.Z = 0.0f;
	if (Direction.IsNearlyZero())
	{
		return;
	}

	const FRotator CurrentRotation = Owner->GetActorRotation();
	const FRotator DesiredRotation(CurrentRotation.Pitch, Direction.Rotation().Yaw, CurrentRotation.Roll);
	Owner->SetActorRotation(DesiredRotation);
}

void UNPCActionQueueComponent::PrintDebugMessage(const FString& Message, const FColor& Color) const
{
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);

	if (bPrintActionQueueDebug && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, Color, Message);
	}
}

void UNPCActionQueueComponent::LogActionQueueMoveRequest(const TCHAR* FunctionName, const FString& Target) const
{
	const UNPCMoveToTableComponent* ResolvedMoveComponent = MoveComponent;
	if (!ResolvedMoveComponent)
	{
		if (const AActor* Owner = GetOwner())
		{
			ResolvedMoveComponent = Owner->FindComponentByClass<UNPCMoveToTableComponent>();
		}
	}

	const bool bMovePaused = ResolvedMoveComponent && ResolvedMoveComponent->IsMovementPausedByServiceGesture();
	const bool bGuideInProgress = ResolvedMoveComponent && ResolvedMoveComponent->IsGuidingPlayer();
	const FString GuideTarget = (ResolvedMoveComponent && ResolvedMoveComponent->IsGuidingPlayer())
		? ResolvedMoveComponent->GetCurrentGuideTargetID()
		: TEXT("");

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ActionQueue][MOVE_REQUEST] %s Target=%s FarewellBow=%d BowLock=%d MovementPause=%d ActionQueuePaused=%d IsGuiding=%d GuideTarget=%s"),
		FunctionName ? FunctionName : TEXT("unknown"),
		*Target,
		bFarewellBowInProgress ? 1 : 0,
		bBowMovementLockActive ? 1 : 0,
		bMovePaused ? 1 : 0,
		bActionQueuePausedForFarewellBow ? 1 : 0,
		bGuideInProgress ? 1 : 0,
		*GuideTarget);
}

void UNPCActionQueueComponent::ClearOrderFulfillmentTimers()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PostServePauseTimerHandle);
		World->GetTimerManager().ClearTimer(BillPresentationPauseTimerHandle);
		World->GetTimerManager().ClearTimer(ClosingBowPauseTimerHandle);
		World->GetTimerManager().ClearTimer(ReturnToCounterAfterClosingTimerHandle);
		if (!bBowMovementLockActive)
		{
			World->GetTimerManager().ClearTimer(BowMinimumDurationTimerHandle);
			World->GetTimerManager().ClearTimer(BowSafetyDelayTimerHandle);
		}
		if (!bFarewellBowInProgress)
		{
			World->GetTimerManager().ClearTimer(FarewellBowTimerHandle);
		}
	}
}

void UNPCActionQueueComponent::ClearPendingOrderState()
{
	bPendingOrderFulfillment = false;
	PendingOrderReturnTableID = TEXT("");
	PendingOrderItem = TEXT("");
	PendingPreSeatingOrderItem = TEXT("");
	PendingOrderPrice = 0;
	bPendingFarewellBowAfterBillGesture = false;
	bPendingPostServeFlowAfterServeGesture = false;
}

void UNPCActionQueueComponent::PickUpFoodForPendingOrder()
{
	if (!bPendingOrderFulfillment)
	{
		return;
	}

	const FString TrimmedOrderItem = PendingOrderItem.TrimStartAndEnd();
	if (TrimmedOrderItem.IsEmpty())
	{
		return;
	}

	UNPCFoodCarryComponent* ResolvedFoodCarryComponent = ResolveFoodCarryComponent();
	if (!ResolvedFoodCarryComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] NPCFoodCarryComponent not found while picking up food. Item=%s"), *TrimmedOrderItem);
		return;
	}

	if (ResolvedFoodCarryComponent->IsCarryingFood())
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Picking up food at counter. Item=%s"), *TrimmedOrderItem);
	ResolvedFoodCarryComponent->SpawnAndAttachFoodToHand(TrimmedOrderItem);
}

AActor* UNPCActionQueueComponent::FindServiceTableActorByID(const FString& TableID) const
{
	const FString TrimmedTableID = TableID.TrimStartAndEnd();
	if (TrimmedTableID.IsEmpty())
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<ATableMarkerActor> It(World); It; ++It)
	{
		ATableMarkerActor* TableMarker = *It;
		if (TableMarker && TableMarker->GetTableID().TrimStartAndEnd().Equals(TrimmedTableID, ESearchCase::IgnoreCase))
		{
			return TableMarker;
		}
	}

	const FName TableIDName(*TrimmedTableID);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		if (Actor->GetName().Equals(TrimmedTableID, ESearchCase::IgnoreCase) || Actor->ActorHasTag(TableIDName))
		{
			return Actor;
		}
	}

	return nullptr;
}

void UNPCActionQueueComponent::BroadcastServiceSpeech(const FString& Text)
{
	const FString TrimmedText = Text.TrimStartAndEnd();
	if (TrimmedText.IsEmpty())
	{
		return;
	}

	ULLMClient::BeginServiceSpeechDispatch();
	OnQueueSpeakRequested.Broadcast(TrimmedText);
	ULLMClient::EndServiceSpeechDispatch();
}

bool UNPCActionQueueComponent::BroadcastGuideLine(const FString& Text)
{
	const FString TrimmedText = Text.TrimStartAndEnd();
	if (TrimmedText.IsEmpty())
	{
		return false;
	}

	const double CurrentTime = FPlatformTime::Seconds();
	const double ElapsedSeconds = CurrentTime - LastGuideLineTime;
	if (TrimmedText.Equals(LastGuideLineText, ESearchCase::CaseSensitive)
		&& ElapsedSeconds >= 0.0
		&& ElapsedSeconds <= FMath::Max(DuplicateGuideLineSuppressSeconds, 0.0f))
	{
		UE_LOG(LogTemp, Warning, TEXT("[GuideLine] Duplicate guide line suppressed: %s"), *TrimmedText);
		return false;
	}

	LastGuideLineText = TrimmedText;
	LastGuideLineTime = CurrentTime;
	BroadcastServiceSpeech(TrimmedText);
	return true;
}

void UNPCActionQueueComponent::BroadcastQueueGesture(FName GestureName)
{
	if (IsBlockingServiceGesture(GestureName.ToString()))
	{
		BeginBlockingGesture(GestureName, GetBlockingGestureFallbackTimeout(GestureName.ToString()));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Gesture requested: %s"), *GestureName.ToString());
	OnQueueGestureRequested.Broadcast(GestureName);
}

void UNPCActionQueueComponent::StartOrderServingCompletionFlow()
{
	ClearOrderFulfillmentTimers();
	bPendingPostServeFlowAfterServeGesture = true;
	BroadcastQueueGesture(FName(TEXT("serve_food")));
}

bool UNPCActionQueueComponent::IsBlockingServiceGesture(const FString& GestureName) const
{
	FString NormalizedGestureName = GestureName.TrimStartAndEnd();
	NormalizedGestureName.ToLowerInline();

	if (NormalizedGestureName.IsEmpty())
	{
		return false;
	}

	if (NormalizedGestureName.Equals(TEXT("speak"))
		|| NormalizedGestureName.Equals(TEXT("speech"))
		|| NormalizedGestureName.Equals(TEXT("subtitle"))
		|| NormalizedGestureName.Equals(TEXT("look_at_player"))
		|| NormalizedGestureName.Equals(TEXT("guide_line"))
		|| NormalizedGestureName.Equals(TEXT("recommend"))
		|| NormalizedGestureName.Equals(TEXT("explain")))
	{
		return false;
	}

	return NormalizedGestureName.Equals(TEXT("bow"))
		|| NormalizedGestureName.Equals(TEXT("put_bill"))
		|| NormalizedGestureName.Equals(TEXT("serve_food"))
		|| NormalizedGestureName.Equals(TEXT("guide_seat_player"))
		|| NormalizedGestureName.Equals(TEXT("farewell_bow"))
		|| NormalizedGestureName.Equals(TEXT("bill"))
		|| NormalizedGestureName.Equals(TEXT("place_bill"))
		|| NormalizedGestureName.Equals(TEXT("hand_over_item"));
}

void UNPCActionQueueComponent::BeginBlockingGesture(FName GestureName, float FallbackTimeout)
{
	const FString GestureNameString = GestureName.ToString().TrimStartAndEnd();
	const bool bIsBowGesture = IsBowGestureName(GestureNameString);
	UAnimMontage* MontageBeforeRequest = nullptr;
	float MontagePositionBeforeRequest = 0.0f;
	float IgnoredRemainingSeconds = 0.0f;
	FString IgnoredAnimationName;
	TryGetActiveGestureAnimationState(
		MontageBeforeRequest,
		MontagePositionBeforeRequest,
		IgnoredRemainingSeconds,
		IgnoredAnimationName);
	BlockingGestureMontageBeforeRequest = MontageBeforeRequest;
	BlockingGestureMontagePositionBeforeRequest = MontagePositionBeforeRequest;
	PendingGestureAnimationTimingProbe = GestureName;
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Blocking gesture started: %s"), *GestureNameString);

	if (bBlockingGestureInProgress && !bBlockingGestureFinishHandled)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ActionQueue] Blocking gesture replaced before completion. Previous=%s New=%s"),
			*CurrentBlockingGestureName,
			*GestureNameString);
	}

	bBlockingGestureInProgress = true;
	CurrentBlockingGestureName = GestureNameString;
	bBlockingGestureFinishHandled = false;

	if (bIsBowGesture)
	{
		StartBowMovementLock(GestureName);
	}
	else
	{
		CacheOwnerComponents();
		if (MoveComponent)
		{
			MoveComponent->StopCurrentMovement();
			MoveComponent->SetMovementPausedByServiceGesture(true);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] NPCMoveToTableComponent not found for blocking gesture movement pause."));
		}
	}

	UWorld* World = GetWorld();
	const float Timeout = FMath::Max(FallbackTimeout, 0.0f);
	if (World && Timeout > KINDA_SMALL_NUMBER)
	{
		World->GetTimerManager().ClearTimer(BlockingServiceGesturePauseTimerHandle);
		World->GetTimerManager().SetTimer(
			BlockingServiceGesturePauseTimerHandle,
			this,
			&UNPCActionQueueComponent::HandleBlockingGestureTimeout,
			Timeout,
			false);
	}

	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Gesture requested: %s"), *GestureNameString);
	OnQueueGestureRequested.Broadcast(GestureName);

	if (World)
	{
		FTimerDelegate ProbeDelegate;
		ProbeDelegate.BindUObject(this, &UNPCActionQueueComponent::UpdateBlockingGestureTimingFromStartedAnimation, GestureName);
		World->GetTimerManager().SetTimerForNextTick(ProbeDelegate);
	}
}

float UNPCActionQueueComponent::GetBlockingGestureFallbackTimeout(const FString& GestureName) const
{
	FString NormalizedGestureName = GestureName.TrimStartAndEnd();
	NormalizedGestureName.ToLowerInline();

	if (NormalizedGestureName.Equals(TEXT("bow")) || NormalizedGestureName.Equals(TEXT("farewell_bow")))
	{
		return BowGestureFallbackTimeout;
	}

	if (NormalizedGestureName.Equals(TEXT("put_bill"))
		|| NormalizedGestureName.Equals(TEXT("place_bill"))
		|| NormalizedGestureName.Equals(TEXT("bill")))
	{
		return PutBillGestureFallbackTimeout;
	}

	if (NormalizedGestureName.Equals(TEXT("serve_food")) || NormalizedGestureName.Equals(TEXT("hand_over_item")))
	{
		return ServeFoodGestureFallbackTimeout;
	}

	if (NormalizedGestureName.Equals(TEXT("guide_seat_player")))
	{
		return GuideSeatGestureFallbackTimeout;
	}

	return DefaultBlockingGestureFallbackTimeout;
}

void UNPCActionQueueComponent::CompleteBlockingGesture(const FString& GestureName, bool bForced)
{
	if (!bBlockingGestureInProgress)
	{
		return;
	}

	const FString RequestedGestureName = GestureName.TrimStartAndEnd();
	if (!RequestedGestureName.Equals(CurrentBlockingGestureName, ESearchCase::IgnoreCase))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ActionQueue] Blocking gesture finish ignored: expected=%s actual=%s"),
			*CurrentBlockingGestureName,
			*RequestedGestureName);
		return;
	}

	if (bBlockingGestureFinishHandled)
	{
		return;
	}

	bBlockingGestureFinishHandled = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BlockingServiceGesturePauseTimerHandle);
	}

	const FString FinishedGestureName = CurrentBlockingGestureName;
	bBlockingGestureInProgress = false;
	CurrentBlockingGestureName = TEXT("");
	PendingGestureAnimationTimingProbe = NAME_None;
	BlockingGestureMontageBeforeRequest = nullptr;
	BlockingGestureMontagePositionBeforeRequest = 0.0f;
	const bool bFinishedBow = IsBowGestureName(FinishedGestureName);

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ActionQueue] Blocking gesture finished: %s%s"),
		*FinishedGestureName,
		bForced ? TEXT(" (forced)") : TEXT(""));

	if (bFinishedBow)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Bow finished by notify. Waiting safety delay before movement release."));
		MarkBowFinishNotifyReceived(FName(*FinishedGestureName));
		return;
	}

	CacheOwnerComponents();
	if (MoveComponent)
	{
		MoveComponent->SetMovementPausedByServiceGesture(false);
	}

	const bool bFinishedServeGesture =
		FinishedGestureName.Equals(TEXT("serve_food"), ESearchCase::IgnoreCase)
		|| FinishedGestureName.Equals(TEXT("hand_over_item"), ESearchCase::IgnoreCase);
	if (bPendingPostServeFlowAfterServeGesture && bFinishedServeGesture)
	{
		bPendingPostServeFlowAfterServeGesture = false;

		if (UNPCFoodCarryComponent* ResolvedFoodCarryComponent = ResolveFoodCarryComponent())
		{
			if (ResolvedFoodCarryComponent->IsCarryingFood())
			{
				PlaceCarriedFoodOnCurrentServiceTable();
			}
		}

		UWorld* World = GetWorld();
		const float ServePause = FMath::Max(PostServePauseSeconds, 0.0f);
		if (!World || ServePause <= KINDA_SMALL_NUMBER)
		{
			HandlePostServePauseTimerFinished();
			return;
		}

		World->GetTimerManager().SetTimer(
			PostServePauseTimerHandle,
			this,
			&UNPCActionQueueComponent::HandlePostServePauseTimerFinished,
			ServePause,
			false);
		return;
	}

	const bool bFinishedBillGesture =
		FinishedGestureName.Equals(TEXT("put_bill"), ESearchCase::IgnoreCase)
		|| FinishedGestureName.Equals(TEXT("place_bill"), ESearchCase::IgnoreCase)
		|| FinishedGestureName.Equals(TEXT("bill"), ESearchCase::IgnoreCase);
	if (bPendingFarewellBowAfterBillGesture && bFinishedBillGesture)
	{
		bPendingFarewellBowAfterBillGesture = false;

		UWorld* World = GetWorld();
		const float BowPause = FMath::Max(ClosingBowPauseSeconds, 0.0f);
		if (!World || BowPause <= KINDA_SMALL_NUMBER)
		{
			HandleClosingBowPauseTimerFinished();
			return;
		}

		World->GetTimerManager().SetTimer(
			ClosingBowPauseTimerHandle,
			this,
			&UNPCActionQueueComponent::HandleClosingBowPauseTimerFinished,
			BowPause,
			false);
		return;
	}
}

bool UNPCActionQueueComponent::BlockMovementIfBlockingGestureInProgress(const TCHAR* FunctionName, const FString& Target, bool bTargetIsCounter)
{
	const FString TrimmedTarget = Target.TrimStartAndEnd();
	const FString LogTarget = TrimmedTarget.IsEmpty()
		? FString(FunctionName ? FunctionName : TEXT("unknown"))
		: TrimmedTarget;
	const bool bCounterTarget = bTargetIsCounter || IsCounterTarget(TrimmedTarget);

	if (bBowMovementLockActive)
	{
		if (bCounterTarget)
		{
			bPendingReturnToCounterAfterBow = true;
		}

		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Movement request deferred during bow lock. Target=%s"), *LogTarget);
		return true;
	}

	if (bFarewellBowInProgress
		|| CurrentBlockingGestureName.Equals(TEXT("bow"), ESearchCase::IgnoreCase)
		|| CurrentBlockingGestureName.Equals(TEXT("farewell_bow"), ESearchCase::IgnoreCase))
	{
		if (bCounterTarget)
		{
			bPendingReturnToCounterAfterBow = true;
		}

		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Movement request deferred during bow lock. Target=%s"), *LogTarget);
		return true;
	}

	if (!bBlockingGestureInProgress)
	{
		return false;
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ActionQueue] Movement request blocked because blocking gesture is in progress: %s Function=%s"),
		*CurrentBlockingGestureName,
		FunctionName ? FunctionName : TEXT("unknown"));
	return true;
}

void UNPCActionQueueComponent::HandleBlockingGestureTimeout()
{
	if (!bBlockingGestureInProgress || bBlockingGestureFinishHandled)
	{
		return;
	}

	const FString TimedOutGestureName = CurrentBlockingGestureName;
	if (TimedOutGestureName.Equals(TEXT("bow"), ESearchCase::IgnoreCase)
		|| TimedOutGestureName.Equals(TEXT("farewell_bow"), ESearchCase::IgnoreCase))
	{
		ForceReleaseBowMovementLockByTimeout();
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Blocking gesture timeout: %s. Forcing completion."), *TimedOutGestureName);
	CompleteBlockingGesture(TimedOutGestureName, true);
}

bool UNPCActionQueueComponent::BlockReturnToCounterIfFarewellBowInProgress()
{
	if (bBowMovementLockActive)
	{
		bPendingReturnToCounterAfterBow = true;
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Movement request deferred during bow lock. Target=counter"));
		return true;
	}

	if (!bFarewellBowInProgress)
	{
		return false;
	}

	bPendingReturnToCounterAfterBow = true;
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Return/move to counter blocked because farewell bow is in progress."));
	return true;
}

bool UNPCActionQueueComponent::IsCounterTarget(const FString& Target) const
{
	const FString TrimmedTarget = Target.TrimStartAndEnd();
	return TrimmedTarget.Equals(TEXT("counter"), ESearchCase::IgnoreCase)
		|| TrimmedTarget.Equals(TEXT("Counter_01"), ESearchCase::IgnoreCase)
		|| TrimmedTarget.Contains(TEXT("counter"), ESearchCase::IgnoreCase)
		|| TrimmedTarget.Contains(TEXT("bar"), ESearchCase::IgnoreCase);
}

bool UNPCActionQueueComponent::IsCounterMoveAction(const FNPCQueuedAction& Action) const
{
	return (Action.ActionType == ENPCQueuedActionType::MoveToSceneTag
			|| Action.ActionType == ENPCQueuedActionType::MoveToFurnitureTag
			|| Action.ActionType == ENPCQueuedActionType::MoveToFurnitureID)
		&& IsCounterTarget(Action.Target);
}

bool UNPCActionQueueComponent::DoesQueuedActionListContainCounterMove() const
{
	return ActionQueue.ContainsByPredicate([this](const FNPCQueuedAction& Action)
	{
		return IsCounterMoveAction(Action);
	});
}

bool UNPCActionQueueComponent::IsBowGestureName(const FString& GestureName) const
{
	const FString NormalizedGestureName = GestureName.TrimStartAndEnd();
	return NormalizedGestureName.Equals(TEXT("bow"), ESearchCase::IgnoreCase)
		|| NormalizedGestureName.Equals(TEXT("farewell_bow"), ESearchCase::IgnoreCase);
}

void UNPCActionQueueComponent::StartBowMovementLock(FName GestureName)
{
	const FString GestureNameString = GestureName.ToString().TrimStartAndEnd();
	bBowMovementLockActive = true;
	bBowFinishNotifyReceived = false;
	bBowMinimumDurationElapsed = false;
	bBowSafetyDelayElapsed = false;
	bPendingReturnToCounterAfterBow = false;

	CacheOwnerComponents();
	if (MoveComponent)
	{
		MoveComponent->CancelActiveMovementForServiceGesture();
		MoveComponent->SetMovementPausedByServiceGesture(true);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] NPCMoveToTableComponent not found for bow movement lock."));
	}

	const float MinimumDuration = FMath::Max(BowMinimumLockDurationSeconds, 0.0f);
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Bow movement hard lock enabled."));
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ActionQueue] Bow movement lock started. Gesture=%s MinDuration=%.2f"),
		*GestureNameString,
		MinimumDuration);

	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(BowMinimumDurationTimerHandle);
		World->GetTimerManager().ClearTimer(BowSafetyDelayTimerHandle);
	}

	if (!World || MinimumDuration <= KINDA_SMALL_NUMBER)
	{
		MarkBowMinimumDurationElapsed();
		return;
	}

	World->GetTimerManager().SetTimer(
		BowMinimumDurationTimerHandle,
		this,
		&UNPCActionQueueComponent::MarkBowMinimumDurationElapsed,
		MinimumDuration,
		false);
}

void UNPCActionQueueComponent::MarkBowFinishNotifyReceived(FName GestureName)
{
	const FString GestureNameString = GestureName.ToString().TrimStartAndEnd();
	if (!IsBowGestureName(GestureNameString))
	{
		return;
	}

	if (!bBowMovementLockActive)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Bow finish notify received without active bow lock. Gesture=%s"), *GestureNameString);
		return;
	}

	if (!bBowFinishNotifyReceived)
	{
		bBowFinishNotifyReceived = true;
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Bow finish notify received. Waiting min duration and safety delay."));
	}

	const float SafetyDelay = FMath::Max(BowMovementReleaseSafetyDelay, 0.0f);
	UWorld* World = GetWorld();
	if (!World || SafetyDelay <= KINDA_SMALL_NUMBER)
	{
		MarkBowSafetyDelayElapsed();
		return;
	}

	World->GetTimerManager().ClearTimer(BowSafetyDelayTimerHandle);
	World->GetTimerManager().SetTimer(
		BowSafetyDelayTimerHandle,
		this,
		&UNPCActionQueueComponent::MarkBowSafetyDelayElapsed,
		SafetyDelay,
		false);

	TryReleaseBowMovementLock();
}

void UNPCActionQueueComponent::MarkBowMinimumDurationElapsed()
{
	if (!bBowMovementLockActive)
	{
		return;
	}

	bBowMinimumDurationElapsed = true;
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Bow minimum duration elapsed."));
	TryReleaseBowMovementLock();
}

void UNPCActionQueueComponent::MarkBowSafetyDelayElapsed()
{
	if (!bBowMovementLockActive)
	{
		return;
	}

	bBowSafetyDelayElapsed = true;
	TryReleaseBowMovementLock();
}

void UNPCActionQueueComponent::TryReleaseBowMovementLock()
{
	if (!bBowMovementLockActive)
	{
		return;
	}

	if (!bBowFinishNotifyReceived || !bBowMinimumDurationElapsed || !bBowSafetyDelayElapsed)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ActionQueue] Bow lock not ready. Notify=%d Min=%d Safety=%d"),
			bBowFinishNotifyReceived ? 1 : 0,
			bBowMinimumDurationElapsed ? 1 : 0,
			bBowSafetyDelayElapsed ? 1 : 0);
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BowMinimumDurationTimerHandle);
		World->GetTimerManager().ClearTimer(BowSafetyDelayTimerHandle);
		World->GetTimerManager().ClearTimer(BlockingServiceGesturePauseTimerHandle);
	}

	const bool bShouldReturnToCounter = bPendingReturnToCounterAfterBow || (bFarewellBowInProgress && bReturnToCounterAfterServing);
	bPendingReturnToCounterAfterBow = false;
	bBowMovementLockActive = false;
	bBowFinishNotifyReceived = false;
	bBowMinimumDurationElapsed = false;
	bBowSafetyDelayElapsed = false;

	ReleaseFarewellBowMovementLock();

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ActionQueue] Bow movement lock released. ReturningToCounter=%s"),
		bShouldReturnToCounter ? TEXT("true") : TEXT("false"));

	if (!bShouldReturnToCounter)
	{
		ClearPendingOrderState();
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Bow hard lock released. Scheduling return to counter."));
	ScheduleReturnToCounterAfterClosingDelay();
}

void UNPCActionQueueComponent::ForceReleaseBowMovementLockByTimeout()
{
	const FString TimedOutGestureName = CurrentBlockingGestureName;
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Bow timeout forced. BP notify is missing or too late."));

	bBlockingGestureFinishHandled = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BlockingServiceGesturePauseTimerHandle);
	}

	bBlockingGestureInProgress = false;
	CurrentBlockingGestureName = TEXT("");

	if (!bBowMovementLockActive && IsBowGestureName(TimedOutGestureName))
	{
		StartBowMovementLock(FName(*TimedOutGestureName));
	}

	MarkBowFinishNotifyReceived(FName(*TimedOutGestureName));
}

void UNPCActionQueueComponent::UpdateBlockingGestureTimingFromStartedAnimation(FName GestureName)
{
	if (!bBlockingGestureInProgress
		|| PendingGestureAnimationTimingProbe != GestureName
		|| !CurrentBlockingGestureName.Equals(GestureName.ToString(), ESearchCase::IgnoreCase))
	{
		return;
	}
	PendingGestureAnimationTimingProbe = NAME_None;

	UAnimMontage* ActiveMontage = nullptr;
	float MontagePosition = 0.0f;
	float RemainingSeconds = 0.0f;
	FString AnimationName;
	if (!TryGetActiveGestureAnimationState(ActiveMontage, MontagePosition, RemainingSeconds, AnimationName))
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[ActionQueue] Gesture animation probe found no active montage; using fallback. Gesture=%s"),
			*GestureName.ToString());
		return;
	}

	const UAnimMontage* MontageBeforeRequest = BlockingGestureMontageBeforeRequest.Get();
	const bool bNewMontage = ActiveMontage != MontageBeforeRequest;
	const bool bExistingMontageRestarted = ActiveMontage == MontageBeforeRequest
		&& MontagePosition + 0.05f < BlockingGestureMontagePositionBeforeRequest;
	if (!bNewMontage && !bExistingMontageRestarted)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[ActionQueue] Ignored pre-existing montage for gesture timing. Gesture=%s Animation=%s"),
			*GestureName.ToString(),
			*AnimationName);
		return;
	}

	if (IsBowGestureName(GestureName.ToString()))
	{
		const FString NormalizedAnimationName = AnimationName.ToLower();
		if (!NormalizedAnimationName.Contains(TEXT("bow"))
			&& !NormalizedAnimationName.Contains(TEXT("greet")))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[ActionQueue] Ignored unrelated montage for bow timing. Animation=%s"),
				*AnimationName);
			return;
		}
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FTimerManager& TimerManager = World->GetTimerManager();
	const float CompletionDelay = RemainingSeconds + 0.1f;
	TimerManager.ClearTimer(BlockingServiceGesturePauseTimerHandle);
	TimerManager.SetTimer(
		BlockingServiceGesturePauseTimerHandle,
		this,
		&UNPCActionQueueComponent::HandleBlockingGestureTimeout,
		CompletionDelay,
		false);

	if (IsBowGestureName(GestureName.ToString()) && bBowMovementLockActive)
	{
		const float CurrentLockRemaining = TimerManager.IsTimerActive(BowMinimumDurationTimerHandle)
			? TimerManager.GetTimerRemaining(BowMinimumDurationTimerHandle)
			: 0.0f;
		if (RemainingSeconds > CurrentLockRemaining + 0.05f)
		{
			bBowMinimumDurationElapsed = false;
			TimerManager.ClearTimer(BowMinimumDurationTimerHandle);
			TimerManager.SetTimer(
				BowMinimumDurationTimerHandle,
				this,
				&UNPCActionQueueComponent::MarkBowMinimumDurationElapsed,
				RemainingSeconds,
				false);
		}
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[ActionQueue] Gesture timing matched newly started animation. Gesture=%s Animation=%s Remaining=%.2f"),
		*GestureName.ToString(),
		*AnimationName,
		RemainingSeconds);
}

bool UNPCActionQueueComponent::TryGetActiveGestureAnimationState(
	UAnimMontage*& OutMontage,
	float& OutPosition,
	float& OutRemainingSeconds,
	FString& OutAnimationName) const
{
	OutMontage = nullptr;
	OutPosition = 0.0f;
	OutRemainingSeconds = 0.0f;
	OutAnimationName = TEXT("");

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	USkeletalMeshComponent* MeshComponent = nullptr;
	if (const ACharacter* CharacterOwner = Cast<ACharacter>(Owner))
	{
		MeshComponent = CharacterOwner->GetMesh();
	}

	if (!MeshComponent)
	{
		MeshComponent = Owner->FindComponentByClass<USkeletalMeshComponent>();
	}

	if (!MeshComponent)
	{
		return false;
	}

	UAnimInstance* AnimInstance = MeshComponent->GetAnimInstance();
	if (!AnimInstance)
	{
		return false;
	}

	UAnimMontage* ActiveMontage = AnimInstance->GetCurrentActiveMontage();
	if (!ActiveMontage)
	{
		return false;
	}

	const float PlayLength = ActiveMontage->GetPlayLength();
	if (PlayLength <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float Position = FMath::Clamp(AnimInstance->Montage_GetPosition(ActiveMontage), 0.0f, PlayLength);
	const float PlayRate = FMath::Abs(AnimInstance->Montage_GetPlayRate(ActiveMontage));
	const float EffectivePlayRate = PlayRate > KINDA_SMALL_NUMBER ? PlayRate : 1.0f;
	const float RemainingSeconds = (PlayLength - Position) / EffectivePlayRate;
	if (RemainingSeconds <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	OutRemainingSeconds = RemainingSeconds;
	OutMontage = ActiveMontage;
	OutPosition = Position;
	OutAnimationName = ActiveMontage->GetName();
	return true;
}

void UNPCActionQueueComponent::EnableFarewellBowLock()
{
	bFarewellBowInProgress = true;
	bActionQueuePausedForFarewellBow = true;
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Farewell bow lock enabled."));

	ActionQueue.Reset();
	CurrentAction = FNPCQueuedAction();
	bIsRunningQueue = false;
	bWaitingForMove = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WaitTimerHandle);
		World->GetTimerManager().ClearTimer(ReturnToCounterAfterClosingTimerHandle);
		World->GetTimerManager().ClearTimer(BlockingServiceGesturePauseTimerHandle);
		if (!bBowMovementLockActive)
		{
			World->GetTimerManager().ClearTimer(BowMinimumDurationTimerHandle);
			World->GetTimerManager().ClearTimer(BowSafetyDelayTimerHandle);
		}
	}
}

void UNPCActionQueueComponent::ReleaseFarewellBowMovementLock()
{
	bFarewellBowInProgress = false;
	bActionQueuePausedForFarewellBow = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BlockingServiceGesturePauseTimerHandle);
		World->GetTimerManager().ClearTimer(BowMinimumDurationTimerHandle);
		World->GetTimerManager().ClearTimer(BowSafetyDelayTimerHandle);
	}

	CacheOwnerComponents();
	if (MoveComponent)
	{
		MoveComponent->SetMovementPausedByServiceGesture(false);
	}
}

void UNPCActionQueueComponent::ReturnToCounterAfterServingIfNeeded()
{
	LogActionQueueMoveRequest(TEXT("ReturnToCounterAfterServingIfNeeded"), TEXT("counter"));

	if (BlockMovementIfBlockingGestureInProgress(TEXT("ReturnToCounterAfterServingIfNeeded"), TEXT("counter"), true))
	{
		return;
	}

	if (BlockReturnToCounterIfFarewellBowInProgress())
	{
		return;
	}

	if (!bReturnToCounterAfterServing)
	{
		ClearPendingOrderState();
		return;
	}

	ScheduleReturnToCounterAfterClosingDelay();
}

void UNPCActionQueueComponent::ScheduleReturnToCounterAfterClosingDelay()
{
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Waiting before returning to counter after closing"));

	UWorld* World = GetWorld();
	const float ReturnDelay = FMath::Max(ReturnToCounterAfterClosingDelaySeconds, 0.0f);
	if (!World || ReturnDelay <= KINDA_SMALL_NUMBER)
	{
		HandleReturnToCounterAfterClosingDelayFinished();
		return;
	}

	World->GetTimerManager().ClearTimer(ReturnToCounterAfterClosingTimerHandle);
	World->GetTimerManager().SetTimer(
		ReturnToCounterAfterClosingTimerHandle,
		this,
		&UNPCActionQueueComponent::HandleReturnToCounterAfterClosingDelayFinished,
		ReturnDelay,
		false);
}

void UNPCActionQueueComponent::HandleReturnToCounterAfterClosingDelayFinished()
{
	LogActionQueueMoveRequest(TEXT("HandleReturnToCounterAfterClosingDelayFinished"), TEXT("counter"));

	if (BlockMovementIfBlockingGestureInProgress(TEXT("HandleReturnToCounterAfterClosingDelayFinished"), TEXT("counter"), true))
	{
		return;
	}

	if (BlockReturnToCounterIfFarewellBowInProgress())
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Returning to counter after service closing"));
	ClearActionQueue();
	EnqueueMoveToSceneTag(TEXT("counter"));
	StartActionQueue();
}

void UNPCActionQueueComponent::HandleFarewellBowFinished()
{
	LogActionQueueMoveRequest(TEXT("HandleFarewellBowFinished"), TEXT("counter"));

	if (bBowMovementLockActive)
	{
		bPendingReturnToCounterAfterBow = true;
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Movement request deferred during bow lock. Target=counter"));
		return;
	}

	ReleaseFarewellBowMovementLock();

	if (!bReturnToCounterAfterServing)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Farewell bow lock released. Return to counter disabled."));
		ClearPendingOrderState();
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Farewell bow lock released. Scheduling return to counter."));
	ScheduleReturnToCounterAfterClosingDelay();
}

void UNPCActionQueueComponent::ClearPostSeatingMenuGuidanceTimers()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PostSeatingMenuGuidanceTimerHandle);
		World->GetTimerManager().ClearTimer(PostSeatingMenuHelpEnableTimerHandle);
	}
}

void UNPCActionQueueComponent::HandleNPCMoveCompleted(FName MoveReason, FString TargetID, bool bSuccess)
{
	if (!bIsRunningQueue || !bWaitingForMove || !IsCurrentActionMove())
	{
		return;
	}

	if (bSuccess && CurrentAction.ActionType == ENPCQueuedActionType::MoveToTableID)
	{
		LastVisitedTableID = TargetID.TrimStartAndEnd();
	}

	const FString CompletedTargetID = TargetID.TrimStartAndEnd();
	const FString OrderReturnTableID = PendingOrderReturnTableID.TrimStartAndEnd();
	const bool bCompletedPendingOrderReturn =
		bPendingOrderFulfillment
		&& bSuccess
		&& MoveReason == FName(TEXT("Table"))
		&& CompletedTargetID.Equals(OrderReturnTableID, ESearchCase::IgnoreCase);

	bWaitingForMove = false;
	const FString FailureReason = bSuccess
		? TEXT("")
		: FString::Printf(TEXT("NPC queued move failed: %s %s"), *MoveReason.ToString(), *TargetID);
	CompleteCurrentAction(bSuccess, FailureReason);

	if (bCompletedPendingOrderReturn)
	{
		const FString ServedItem = PendingOrderItem.TrimStartAndEnd();
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Order served at table: %s"), *CompletedTargetID);
		bPendingOrderFulfillment = false;
		PendingOrderReturnTableID = TEXT("");
		OnOrderServed.Broadcast(CompletedTargetID, ServedItem);
		StartOrderServingCompletionFlow();
	}
}

void UNPCActionQueueComponent::HandleGuideArrived(FName EventName, FString TargetID, FString SuggestedLine)
{
	const FString ArrivedTableID = TargetID.TrimStartAndEnd();
	if (ArrivedTableID.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Seating guidance completed with empty table id"));
		return;
	}

	if (!ArrivedTableID.StartsWith(TEXT("Table"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Ignored non-table guide arrival for seating. TargetID=%s"), *ArrivedTableID);
		return;
	}

	CurrentServiceTableID = ArrivedTableID;
	bSeatingGuidanceCompleted = true;
	UpdateLastKnownServiceState();
	bLastKnownGuidePlayerInProgress = false;
	LastKnownGuideTargetID = TEXT("");

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ActionQueue] Seating guidance completed. CurrentServiceTableID=%s"),
		*CurrentServiceTableID);

	FaceOwnerTowardActor(FindPlayerActor());
	GuideSeatPlayerOnce();
	BroadcastGuideLine(SuggestedLine);
	RunPostSeatingMenuGuidance();
}

void UNPCActionQueueComponent::HandleWaitTimerFinished()
{
	if (!bIsRunningQueue || CurrentAction.ActionType != ENPCQueuedActionType::Wait)
	{
		return;
	}

	PickUpFoodForPendingOrder();
	CompleteCurrentAction(true, TEXT(""));
}

void UNPCActionQueueComponent::HandleQueuedAnimationTimerFinished()
{
	if (!bIsRunningQueue || CurrentAction.ActionType != ENPCQueuedActionType::Animation)
	{
		return;
	}

	bQueuedAnimationInProgress = false;
	bQueuedAnimationPaused = false;
	ActiveQueuedAnimationMontage = nullptr;
	ActiveQueuedAnimationInstance = nullptr;
	CompleteCurrentAction(true, TEXT(""));
}

void UNPCActionQueueComponent::StopQueuedAnimation(float BlendOutSeconds)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(QueuedAnimationTimerHandle);
	}

	if (ActiveQueuedAnimationMontage)
	{
		if (ActiveQueuedAnimationInstance)
		{
			ActiveQueuedAnimationInstance->Montage_Stop(
				FMath::Max(BlendOutSeconds, 0.0f),
				ActiveQueuedAnimationMontage);
		}
	}

	bQueuedAnimationInProgress = false;
	bQueuedAnimationPaused = false;
	ActiveQueuedAnimationMontage = nullptr;
	ActiveQueuedAnimationInstance = nullptr;
}

USkeletalMeshComponent* UNPCActionQueueComponent::ResolveHeldPropAttachmentMesh(
	FName SocketName,
	FName& OutResolvedSocketName) const
{
	OutResolvedSocketName = SocketName;
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	TArray<FName> CandidateSocketNames;
	CandidateSocketNames.Add(SocketName);
	const FString RequestedSocket = SocketName.ToString();
	if (RequestedSocket.Equals(TEXT("RightHand"), ESearchCase::IgnoreCase)
		|| RequestedSocket.Equals(TEXT("hand_r"), ESearchCase::IgnoreCase))
	{
		CandidateSocketNames.AddUnique(FName(TEXT("hand_R")));
		CandidateSocketNames.AddUnique(FName(TEXT("RightHand")));
		CandidateSocketNames.AddUnique(FName(TEXT("hand_r")));
		CandidateSocketNames.AddUnique(FName(TEXT("mixamorig:RightHand")));
	}
	else if (RequestedSocket.Equals(TEXT("LeftHand"), ESearchCase::IgnoreCase)
		|| RequestedSocket.Equals(TEXT("hand_l"), ESearchCase::IgnoreCase))
	{
		CandidateSocketNames.AddUnique(FName(TEXT("hand_L")));
		CandidateSocketNames.AddUnique(FName(TEXT("LeftHand")));
		CandidateSocketNames.AddUnique(FName(TEXT("hand_l")));
		CandidateSocketNames.AddUnique(FName(TEXT("mixamorig:LeftHand")));
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	OwnerActor->GetComponents<USkeletalMeshComponent>(MeshComponents);
	for (FName CandidateSocketName : CandidateSocketNames)
	{
		for (USkeletalMeshComponent* CandidateMesh : MeshComponents)
		{
			if (CandidateMesh
				&& CandidateMesh->GetSkeletalMeshAsset()
				&& CandidateMesh->DoesSocketExist(CandidateSocketName))
			{
				if (CandidateSocketName != SocketName)
				{
					UE_LOG(
						LogTemp,
						Log,
						TEXT("[ActionQueue] resolved held prop socket alias. Requested=%s Resolved=%s"),
						*SocketName.ToString(),
						*CandidateSocketName.ToString());
				}
				OutResolvedSocketName = CandidateSocketName;
				return CandidateMesh;
			}
		}
	}

	return nullptr;
}

void UNPCActionQueueComponent::RemoveActiveHeldProp()
{
	if (!ActiveHeldPropComponent)
	{
		return;
	}

	const FString RemovedMeshName = ActiveHeldPropComponent->GetStaticMesh()
		? ActiveHeldPropComponent->GetStaticMesh()->GetName()
		: TEXT("none");
	ActiveHeldPropComponent->DestroyComponent();
	ActiveHeldPropComponent = nullptr;
	UE_LOG(LogTemp, Log, TEXT("[ActionQueue] held prop removed. Mesh=%s"), *RemovedMeshName);
}

void UNPCActionQueueComponent::HandlePostServePauseTimerFinished()
{
	BroadcastServiceSpeech(OrderServedLine);

	UWorld* World = GetWorld();
	const float BillPause = FMath::Max(BillPresentationPauseSeconds, 0.0f);
	if (!World || BillPause <= KINDA_SMALL_NUMBER)
	{
		HandleBillPresentationPauseTimerFinished();
		return;
	}

	World->GetTimerManager().SetTimer(
		BillPresentationPauseTimerHandle,
		this,
		&UNPCActionQueueComponent::HandleBillPresentationPauseTimerFinished,
		BillPause,
		false);
}

void UNPCActionQueueComponent::HandleBillPresentationPauseTimerFinished()
{
	RunBillDropBowAndReturnToCounter();
}

void UNPCActionQueueComponent::RunBillDropBowAndReturnToCounter()
{
	if (BlockReturnToCounterIfFarewellBowInProgress())
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Bill placed. Starting farewell bow."));
	EnableFarewellBowLock();
	bPendingFarewellBowAfterBillGesture = true;
	BroadcastQueueGesture(FName(TEXT("put_bill")));

	FString BillLine = BillLineFormat;
	BillLine.ReplaceInline(TEXT("{Price}"), *FString::FromInt(PendingOrderPrice));
	BroadcastServiceSpeech(BillLine);
}

bool UNPCActionQueueComponent::PlaceCarriedFoodOnCurrentServiceTable()
{
	UNPCFoodCarryComponent* ResolvedFoodCarryComponent = ResolveFoodCarryComponent();
	if (!ResolvedFoodCarryComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] NPCFoodCarryComponent not found while placing carried food."));
		return false;
	}

	const FString ResolvedTableID = ResolveCurrentServiceTableID();
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Placing carried food on current table. TableID=%s"), *ResolvedTableID);

	AActor* TableActor = FindServiceTableActorByID(ResolvedTableID);
	if (!TableActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Current service table actor not found. TableID=%s"), *ResolvedTableID);
		return false;
	}

	return ResolvedFoodCarryComponent->PlaceCarriedFoodOnTable(TableActor);
}

void UNPCActionQueueComponent::HandleClosingBowPauseTimerFinished()
{
	if (!bFarewellBowInProgress)
	{
		EnableFarewellBowLock();
	}

	BroadcastQueueGesture(FName(TEXT("bow")));
	if (bReturnToCounterAfterServing)
	{
		bPendingReturnToCounterAfterBow = true;
	}
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Farewell bow started. Waiting for completion before returning to counter."));
	BroadcastServiceSpeech(BillFarewellLine);
}

void UNPCActionQueueComponent::HandlePostSeatingMenuGuidanceTimerFinished()
{
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Post seating menu guidance line: %s"), *PostSeatingMenuGuidanceLine);
	BroadcastServiceSpeech(PostSeatingMenuGuidanceLine);

	UWorld* World = GetWorld();
	if (!World)
	{
		HandlePostSeatingMenuHelpEnableTimerFinished();
		return;
	}

	const float HelpEnableDelay = FMath::Max(PostSeatingMenuHelpEnableDelay, 0.0f);
	if (HelpEnableDelay <= KINDA_SMALL_NUMBER)
	{
		HandlePostSeatingMenuHelpEnableTimerFinished();
		return;
	}

	World->GetTimerManager().SetTimer(
		PostSeatingMenuHelpEnableTimerHandle,
		this,
		&UNPCActionQueueComponent::HandlePostSeatingMenuHelpEnableTimerFinished,
		HelpEnableDelay,
		false);
}

void UNPCActionQueueComponent::HandlePostSeatingMenuHelpEnableTimerFinished()
{
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Post seating menu guidance finished"));
	OnPostSeatingMenuGuidanceFinished.Broadcast();
}
