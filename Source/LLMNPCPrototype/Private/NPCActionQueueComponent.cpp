// Copyright Epic Games, Inc. All Rights Reserved.

#include "NPCActionQueueComponent.h"

#include "CafeServiceManagerActor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "NPCFaceTargetComponent.h"
#include "NPCMoveToTableComponent.h"
#include "PlayerSeatComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UNPCActionQueueComponent::UNPCActionQueueComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	bIsRunningQueue = false;
	bStopQueueOnActionFailure = false;
	bPrintActionQueueDebug = true;
	CurrentServiceTableID = TEXT("");
	LastVisitedTableID = TEXT("");
	bWaitingForMove = false;
	MoveComponent = nullptr;
	FaceTargetComponent = nullptr;
}

void UNPCActionQueueComponent::BeginPlay()
{
	Super::BeginPlay();

	CacheOwnerComponents();
	BindMoveCompletedEvent();
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

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WaitTimerHandle);
	}

	PrintDebugMessage(TEXT("NPC action queue cleared"), FColor::Yellow);
}

void UNPCActionQueueComponent::StopActionQueue()
{
	ActionQueue.Reset();
	CurrentAction = FNPCQueuedAction();
	bIsRunningQueue = false;
	bWaitingForMove = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WaitTimerHandle);
	}

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
	PrintDebugMessage(TEXT("Run move near player"), FColor::Green);
	CancelFollowingForExplicitCommand();
	ClearActionQueue();
	EnqueueMoveNearPlayer(StopDistance);
	StartActionQueue();
}

void UNPCActionQueueComponent::RunStartFollowPlayer(float DesiredDistance)
{
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
	const FString TrimmedTableID = TableID.TrimStartAndEnd();
	PrintDebugMessage(FString::Printf(TEXT("Run guide player to table: %s"), *TrimmedTableID), FColor::Green);
	ClearActionQueue();

	CacheOwnerComponents();
	if (!MoveComponent)
	{
		PrintDebugMessage(TEXT("NPCMoveToTableComponent not found"), FColor::Red);
		return;
	}

	MoveComponent->StartGuideToTable(TrimmedTableID);
}

void UNPCActionQueueComponent::RunGuidePlayerToSceneTag(const FString& TargetTag)
{
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
}

void UNPCActionQueueComponent::RunTableRouteFromString(const FString& RouteString)
{
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

void UNPCActionQueueComponent::SetCurrentServiceTableID(const FString& TableID)
{
	CurrentServiceTableID = TableID.TrimStartAndEnd();
}

FString UNPCActionQueueComponent::GetCurrentServiceTableID() const
{
	return CurrentServiceTableID;
}

void UNPCActionQueueComponent::CacheOwnerComponents()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		MoveComponent = nullptr;
		FaceTargetComponent = nullptr;
		return;
	}

	MoveComponent = Owner->FindComponentByClass<UNPCMoveToTableComponent>();
	FaceTargetComponent = Owner->FindComponentByClass<UNPCFaceTargetComponent>();
}

void UNPCActionQueueComponent::CancelFollowingForExplicitCommand()
{
	CacheOwnerComponents();
	if (MoveComponent)
	{
		MoveComponent->CancelFollowingForExplicitCommand();
	}
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
	}
}

void UNPCActionQueueComponent::UnbindMoveCompletedEvent()
{
	if (MoveComponent)
	{
		MoveComponent->OnNPCMoveCompleted.RemoveDynamic(this, &UNPCActionQueueComponent::HandleNPCMoveCompleted);
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

	if (!MoveComponent)
	{
		CompleteCurrentAction(false, TEXT("NPCMoveToTableComponent not found"));
		return;
	}

	const FString Target = CurrentAction.Target.TrimStartAndEnd();
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
	if (!bSuccess)
	{
		const FString Message = FailureReason.IsEmpty() ? TEXT("NPC queued action failed") : FailureReason;
		PrintDebugMessage(Message, FColor::Red);
	}

	if (!bSuccess && bStopQueueOnActionFailure)
	{
		ActionQueue.Reset();
		CurrentAction = FNPCQueuedAction();
		bWaitingForMove = false;
		bIsRunningQueue = false;
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

	bWaitingForMove = false;
	const FString FailureReason = bSuccess
		? TEXT("")
		: FString::Printf(TEXT("NPC queued move failed: %s %s"), *MoveReason.ToString(), *TargetID);
	CompleteCurrentAction(bSuccess, FailureReason);
}

void UNPCActionQueueComponent::HandleWaitTimerFinished()
{
	if (!bIsRunningQueue || CurrentAction.ActionType != ENPCQueuedActionType::Wait)
	{
		return;
	}

	CompleteCurrentAction(true, TEXT(""));
}
