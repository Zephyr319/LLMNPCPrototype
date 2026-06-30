// Copyright Epic Games, Inc. All Rights Reserved.

#include "NPCActionQueueComponent.h"

#include "CafeServiceManagerActor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "LLMClient.h"
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
	PostSeatingMenuGuidanceLine = TEXT("メニューはテーブルの上にございます。お決まりになりましたら、いつでもお声がけください。");
	OrderPreparationSeconds = 3.0f;
	OrderConfirmLine = TEXT("かしこまりました。少々お待ちください。");
	OrderServedLine = TEXT("お待たせいたしました。ご注文の料理です。");
	bReturnToCounterAfterServing = true;
	PostServePauseSeconds = 0.8f;
	BillPresentationPauseSeconds = 0.8f;
	ClosingBowPauseSeconds = 0.8f;
	ReturnToCounterAfterClosingDelaySeconds = 1.5f;
	BowBeforeReturnDuration = 2.5f;
	BlockingServiceGesturePauseDuration = 1.5f;
	BowGestureFallbackTimeout = 10.0f;
	BowMovementReleaseSafetyDelay = 0.3f;
	PutBillGestureFallbackTimeout = 3.0f;
	ServeFoodGestureFallbackTimeout = 3.0f;
	GuideSeatGestureFallbackTimeout = 2.0f;
	DefaultBlockingGestureFallbackTimeout = 3.0f;
	DefaultOrderPrice = 800;
	BillLineFormat = TEXT("お会計は{Price}円でございます。");
	ServiceClosingLine = TEXT("ご用の際はいつでもお声がけください。");
	BillFarewellLine = TEXT("ご用がございましたらお呼びください。");
	bWaitingForMove = false;
	bPlayerInTableArea = false;
	bSeatingGuidanceCompleted = false;
	bPostSeatingMenuGuidanceDone = false;
	bPendingOrderFulfillment = false;
	bFarewellBowInProgress = false;
	bActionQueuePausedForFarewellBow = false;
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
	ActionQueue.Reset();
	CurrentAction = FNPCQueuedAction();
	bIsRunningQueue = false;
	bWaitingForMove = false;
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
	ActionQueue.Reset();
	CurrentAction = FNPCQueuedAction();
	bIsRunningQueue = false;
	bWaitingForMove = false;
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

void UNPCActionQueueComponent::EnqueueFacePlayer()
{
	FNPCQueuedAction Action;
	Action.ActionType = ENPCQueuedActionType::FacePlayer;
	EnqueueAction(Action);
}

void UNPCActionQueueComponent::StartActionQueue()
{
	if (BlockMovementIfBlockingGestureInProgress(TEXT("StartActionQueue")))
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
	PrintDebugMessage(TEXT("NPC action queue started"), FColor::Green);
	ExecuteNextQueuedAction();
}

void UNPCActionQueueComponent::RunCounterAndReturnToTable(const FString& ReturnTableID)
{
	LogActionQueueMoveRequest(TEXT("RunCounterAndReturnToTable"), ReturnTableID);

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunCounterAndReturnToTable")))
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

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunCounterAndReturnToPlayer")))
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

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunMoveNearPlayer")))
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

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunStartFollowPlayer")))
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

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunGuidePlayerToTable")))
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

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunGuidePlayerToSceneTag")))
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

	if (bFarewellBowInProgress)
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

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunTableRouteFromString")))
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

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunActionSequenceFromJson")))
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
		break;
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

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunCounterAndReturnToCurrentTable")))
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

	if (BlockMovementIfBlockingGestureInProgress(TEXT("RunOrderFulfillmentToCurrentTable")))
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

void UNPCActionQueueComponent::SetPlayerInTableArea(bool bInArea, const FString& TableID)
{
	const FString TrimmedTableID = TableID.TrimStartAndEnd();
	if (bInArea)
	{
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

		bSeatingGuidanceCompleted = true;
		UpdateLastKnownServiceState();
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ActionQueue] Player entered table area. CurrentServiceTableID=%s"),
			*CurrentServiceTableID);
		return;
	}

	if (!bPlayerInTableArea)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Duplicate table leave ignored."));
		return;
	}

	bPlayerInTableArea = false;
	UpdateLastKnownServiceState();
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Player left table area, keeping service seated state."));
}

void UNPCActionQueueComponent::NotifyPlayerEnteredTableArea(const FString& TableID)
{
	SetPlayerInTableArea(true, TableID);
}

void UNPCActionQueueComponent::NotifyPlayerLeftTableArea(const FString& TableID)
{
	SetPlayerInTableArea(false, TableID);
}

FString UNPCActionQueueComponent::GetCurrentServiceTableID() const
{
	return CurrentServiceTableID;
}

bool UNPCActionQueueComponent::IsSeatedForService() const
{
	return bPlayerInTableArea || bSeatingGuidanceCompleted || !CurrentServiceTableID.TrimStartAndEnd().IsEmpty();
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

bool UNPCActionQueueComponent::IsBlockingGestureInProgress() const
{
	return bBlockingGestureInProgress;
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

	if (BlockMovementIfBlockingGestureInProgress(TEXT("ExecuteNextQueuedAction")))
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
		CurrentAction = FNPCQueuedAction();
		bWaitingForMove = false;
		bIsRunningQueue = false;
		PrintDebugMessage(TEXT("NPC action queue completed"), FColor::Cyan);
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

	if (BlockMovementIfBlockingGestureInProgress(TEXT("ExecuteMoveAction")))
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

	const FString Target = CurrentAction.Target.TrimStartAndEnd();
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
		ActionQueue.Reset();
		CurrentAction = FNPCQueuedAction();
		bWaitingForMove = false;
		bIsRunningQueue = false;
		ClearPendingOrderState();
		ClearOrderFulfillmentTimers();
		PrintDebugMessage(TEXT("NPC action queue stopped after failed action"), FColor::Red);
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
		TEXT("[ActionQueue][MOVE_REQUEST] %s Target=%s FarewellBow=%d MovementPause=%d ActionQueuePaused=%d IsGuiding=%d GuideTarget=%s"),
		FunctionName ? FunctionName : TEXT("unknown"),
		*Target,
		bFarewellBowInProgress ? 1 : 0,
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
		World->GetTimerManager().ClearTimer(BowMovementReleaseSafetyTimerHandle);
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
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Blocking gesture started: %s. Waiting for completion."), *GestureNameString);
	if (GestureName == FName(TEXT("bow")) || GestureName == FName(TEXT("farewell_bow")))
	{
		if (AActor* Owner = GetOwner())
		{
			UE_LOG(LogTemp, Warning, TEXT("[ActionQueue][BOW_LOCATION] bow start location=%s"), *Owner->GetActorLocation().ToString());
		}
	}

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
		World->GetTimerManager().ClearTimer(BowMovementReleaseSafetyTimerHandle);
	}

	const FString FinishedGestureName = CurrentBlockingGestureName;
	bBlockingGestureInProgress = false;
	CurrentBlockingGestureName = TEXT("");
	const bool bFinishedFarewellBow =
		FinishedGestureName.Equals(TEXT("bow"), ESearchCase::IgnoreCase)
		|| FinishedGestureName.Equals(TEXT("farewell_bow"), ESearchCase::IgnoreCase);
	const bool bDelayFarewellBowRelease = bFarewellBowInProgress && bFinishedFarewellBow;

	CacheOwnerComponents();
	if (MoveComponent && !bDelayFarewellBowRelease)
	{
		MoveComponent->SetMovementPausedByServiceGesture(false);
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ActionQueue] Blocking gesture finished: %s%s"),
		*FinishedGestureName,
		bForced ? TEXT(" (forced)") : TEXT(""));

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

	if (bDelayFarewellBowRelease)
	{
		const float SafetyDelay = FMath::Max(BowMovementReleaseSafetyDelay, 0.0f);
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ActionQueue] Bow finished by %s. Releasing movement after safety delay=%.1f"),
			bForced ? TEXT("timeout") : TEXT("notify"),
			SafetyDelay);

		UWorld* World = GetWorld();
		if (!World || SafetyDelay <= KINDA_SMALL_NUMBER)
		{
			HandleBowMovementReleaseSafetyDelayFinished();
			return;
		}

		World->GetTimerManager().ClearTimer(BowMovementReleaseSafetyTimerHandle);
		World->GetTimerManager().SetTimer(
			BowMovementReleaseSafetyTimerHandle,
			this,
			&UNPCActionQueueComponent::HandleBowMovementReleaseSafetyDelayFinished,
			SafetyDelay,
			false);
	}
}

bool UNPCActionQueueComponent::BlockMovementIfBlockingGestureInProgress(const TCHAR* FunctionName) const
{
	if (bFarewellBowInProgress
		|| CurrentBlockingGestureName.Equals(TEXT("bow"), ESearchCase::IgnoreCase)
		|| CurrentBlockingGestureName.Equals(TEXT("farewell_bow"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Movement request blocked while bow is still active."));
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
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Blocking gesture timeout: bow. This means BP bow completion notify is late or missing."));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Blocking gesture timeout: %s. Forcing completion."), *TimedOutGestureName);
	}
	CompleteBlockingGesture(TimedOutGestureName, true);
}

bool UNPCActionQueueComponent::BlockReturnToCounterIfFarewellBowInProgress() const
{
	if (!bFarewellBowInProgress)
	{
		return false;
	}

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
		World->GetTimerManager().ClearTimer(BowMovementReleaseSafetyTimerHandle);
	}
}

void UNPCActionQueueComponent::ReleaseFarewellBowMovementLock()
{
	bFarewellBowInProgress = false;
	bActionQueuePausedForFarewellBow = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BlockingServiceGesturePauseTimerHandle);
		World->GetTimerManager().ClearTimer(BowMovementReleaseSafetyTimerHandle);
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

	if (BlockMovementIfBlockingGestureInProgress(TEXT("ReturnToCounterAfterServingIfNeeded")))
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

	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Waiting before returning to counter after closing"));

	UWorld* World = GetWorld();
	const float ReturnDelay = FMath::Max(ReturnToCounterAfterClosingDelaySeconds, 0.0f);
	if (!World || ReturnDelay <= KINDA_SMALL_NUMBER)
	{
		HandleReturnToCounterAfterClosingDelayFinished();
		return;
	}

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

	if (BlockMovementIfBlockingGestureInProgress(TEXT("HandleReturnToCounterAfterClosingDelayFinished")))
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
	if (AActor* Owner = GetOwner())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue][BOW_LOCATION] bow end location=%s"), *Owner->GetActorLocation().ToString());
	}

	ReleaseFarewellBowMovementLock();

	if (!bReturnToCounterAfterServing)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Farewell bow lock released. Return to counter disabled."));
		ClearPendingOrderState();
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Farewell bow lock released. Returning to counter."));
	ClearActionQueue();
	EnqueueMoveToSceneTag(TEXT("counter"));
	StartActionQueue();
}

void UNPCActionQueueComponent::HandleBowMovementReleaseSafetyDelayFinished()
{
	UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Bow safety delay finished. Returning to counter."));
	HandleFarewellBowFinished();
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
		UE_LOG(LogTemp, Warning, TEXT("[ActionQueue] Order served at table: %s"), *CompletedTargetID);
		bPendingOrderFulfillment = false;
		PendingOrderReturnTableID = TEXT("");
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
