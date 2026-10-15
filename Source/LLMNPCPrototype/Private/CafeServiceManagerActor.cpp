// Copyright Epic Games, Inc. All Rights Reserved.

#include "CafeServiceManagerActor.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "GameFramework/PlayerController.h"
#include "LLMClient.h"
#include "NPCActionQueueComponent.h"
#include "NPCMoveToTableComponent.h"
#include "NPCAutonomyComponent.h"
#include "NPCEmotionComponent.h"
#include "NPCMemoryComponent.h"
#include "NPCResearchLoggerComponent.h"
#include "PlayerSeatComponent.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TableMarkerActor.h"

ACafeServiceManagerActor::ACafeServiceManagerActor()
{
	PrimaryActorTick.bCanEverTick = true;
	AutonomyComponent = CreateDefaultSubobject<UNPCAutonomyComponent>(TEXT("AutonomyComponent"));
	MemoryComponent = CreateDefaultSubobject<UNPCMemoryComponent>(TEXT("MemoryComponent"));
	EmotionComponent = CreateDefaultSubobject<UNPCEmotionComponent>(TEXT("EmotionComponent"));
	ResearchLoggerComponent = CreateDefaultSubobject<UNPCResearchLoggerComponent>(TEXT("ResearchLoggerComponent"));

	CurrentState = ECafeServiceState::Idle;
	MaidNPCActor = nullptr;
	CurrentPlayerActor = nullptr;
	TargetTableID = TEXT("");
	CurrentPartySize = 0;
	bCurrentWantsDining = false;
	MaidApproachDistance = 260.0f;
	ReceptionGreetingDelaySeconds = 1.5f;
	DepartureDistanceThreshold = 260.0f;
	DepartureConfirmationSeconds = 3.0f;
	bRequireOrderServedBeforeDeparture = true;
	MealCompletionQuestion = FText::FromString(TEXT("先生、お食事クエストは完了しましたか？ 完了でしたら、アリスがテーブルを片付けます。"));
	MealCompletionApproachDistance = 180.0f;
	MealCompletionApproachDelaySeconds = 6.0f;
	bAwaitingMealCompletionConfirmation = false;
	bEnablePostServiceSocialInitiative = true;
	SocialApproachRadius = 300.0f;
	SocialApproachResetRadius = 450.0f;
	SocialApproachRearmSeconds = 8.0f;
	SocialLookDotThreshold = 0.65f;
	SocialLookHoldSeconds = 2.0f;
	SocialOpportunityCooldownSeconds = 60.0f;
	MinimumSecondsAfterOrderServedForSocialOpportunity = 6.0f;
	MinimumQuietSecondsBeforeSocialOpportunity = 4.0f;
	bPreferMemoryTopicsForSocialInitiative = true;
	SocialMemoryMinimumImportance = 0.45f;
	SocialMemoryAttentionAvailability = 0.85f;
	bServiceStarted = false;
	MaidMoveComponent = nullptr;
	PlayerSeatComponent = nullptr;
	MaidActionQueueComponent = nullptr;
	PendingDepartureSeatID = TEXT("");
	PendingDepartureTableID = TEXT("");
	PendingDepartureOrigin = FVector::ZeroVector;
	OutsideTableAreaSinceWorldTime = -1.0;
	bOrderServedForCurrentVisit = false;
	bDepartureConfirmedForCurrentVisit = false;
	bMaidApproachingForMealConfirmation = false;
	bSocialApproachConsumed = false;
	SocialLookHeldSeconds = 0.0f;
	SocialAwaySinceWorldTime = -1.0;
	LastSocialOpportunityWorldTime = -1.0;
	LastOrderServedWorldTime = -1.0;
	LastServedItem = TEXT("");
	LastSocialTopicMemoryID = TEXT("");
}

void ACafeServiceManagerActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdatePostServiceSocialOpportunity(DeltaSeconds);
}

void ACafeServiceManagerActor::StartServiceForPlayer(AActor* PlayerActor)
{
	if (bServiceStarted)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReceptionGreetingTimerHandle);
	}
	PendingReceptionGreetingText = FText::GetEmpty();
	ClearPendingDeparture();

	if (MaidMoveComponent)
	{
		MaidMoveComponent->OnNPCMoveCompleted.RemoveDynamic(this, &ACafeServiceManagerActor::HandleMaidMoveCompleted);
	}

	if (PlayerSeatComponent)
	{
		PlayerSeatComponent->OnPlayerSeated.RemoveDynamic(this, &ACafeServiceManagerActor::HandlePlayerSeated);
		PlayerSeatComponent->OnPlayerLeftSeat.RemoveDynamic(this, &ACafeServiceManagerActor::HandlePlayerLeftSeat);
	}

	if (MaidActionQueueComponent)
	{
		MaidActionQueueComponent->OnOrderStarted.RemoveDynamic(this, &ACafeServiceManagerActor::HandleOrderStarted);
		MaidActionQueueComponent->OnOrderServed.RemoveDynamic(this, &ACafeServiceManagerActor::HandleOrderServed);
	}

	MaidMoveComponent = nullptr;
	PlayerSeatComponent = nullptr;
	MaidActionQueueComponent = nullptr;

	if (!PlayerActor || !MaidNPCActor)
	{
		PrintDebugMessage(TEXT("Cafe service missing player or maid"), FColor::Red);
		return;
	}

	CurrentPlayerActor = PlayerActor;
	TargetTableID = TEXT("");
	CurrentPartySize = 0;
	bCurrentWantsDining = false;
	bOrderServedForCurrentVisit = false;
	bDepartureConfirmedForCurrentVisit = false;
	bSocialApproachConsumed = false;
	SocialLookHeldSeconds = 0.0f;
	SocialAwaySinceWorldTime = -1.0;
	LastSocialOpportunityWorldTime = -1.0;
	LastOrderServedWorldTime = -1.0;
	LastServedItem = TEXT("");
	LastSocialTopicMemoryID = TEXT("");
	MaidMoveComponent = MaidNPCActor->FindComponentByClass<UNPCMoveToTableComponent>();
	MaidActionQueueComponent = MaidNPCActor->FindComponentByClass<UNPCActionQueueComponent>();
	PlayerSeatComponent = PlayerActor->FindComponentByClass<UPlayerSeatComponent>();

	if (!MaidMoveComponent || !PlayerSeatComponent)
	{
		PrintDebugMessage(TEXT("Cafe service missing required components"), FColor::Red);
		CurrentPlayerActor = nullptr;
		MaidMoveComponent = nullptr;
		PlayerSeatComponent = nullptr;
		return;
	}

	if (MaidActionQueueComponent)
	{
		MaidActionQueueComponent->PrepareForNewReception();
	}

	MaidMoveComponent->OnNPCMoveCompleted.RemoveDynamic(this, &ACafeServiceManagerActor::HandleMaidMoveCompleted);
	MaidMoveComponent->OnNPCMoveCompleted.AddDynamic(this, &ACafeServiceManagerActor::HandleMaidMoveCompleted);

	PlayerSeatComponent->OnPlayerSeated.RemoveDynamic(this, &ACafeServiceManagerActor::HandlePlayerSeated);
	PlayerSeatComponent->OnPlayerSeated.AddDynamic(this, &ACafeServiceManagerActor::HandlePlayerSeated);
	PlayerSeatComponent->OnPlayerLeftSeat.RemoveDynamic(this, &ACafeServiceManagerActor::HandlePlayerLeftSeat);
	PlayerSeatComponent->OnPlayerLeftSeat.AddDynamic(this, &ACafeServiceManagerActor::HandlePlayerLeftSeat);
	if (MaidActionQueueComponent)
	{
		MaidActionQueueComponent->OnOrderStarted.RemoveDynamic(this, &ACafeServiceManagerActor::HandleOrderStarted);
		MaidActionQueueComponent->OnOrderStarted.AddDynamic(this, &ACafeServiceManagerActor::HandleOrderStarted);
		MaidActionQueueComponent->OnOrderServed.RemoveDynamic(this, &ACafeServiceManagerActor::HandleOrderServed);
		MaidActionQueueComponent->OnOrderServed.AddDynamic(this, &ACafeServiceManagerActor::HandleOrderServed);
	}

	bServiceStarted = true;
	MemoryComponent->RecordMemoryEvent(
		FName(TEXT("player_entered_cafe")),
		TEXT("A customer entered the cafe and the maid began the reception service."),
		FName(TEXT("entrance")),
		{ FName(TEXT("player")), FName(TEXT("customer")), FName(TEXT("reception")) },
		0.1f,
		0.35f,
		0.45f,
		false);
	SetState(ECafeServiceState::MaidApproachingPlayer);

	if (!MaidMoveComponent->MoveNearActor(PlayerActor, MaidApproachDistance))
	{
		bServiceStarted = false;
		SetState(ECafeServiceState::Idle);
	}
}

void ACafeServiceManagerActor::SubmitReceptionResponse(int32 PartySize, bool bWantsDining)
{
	if (CurrentState != ECafeServiceState::WaitingForReceptionResponse)
	{
		return;
	}

	CurrentPartySize = PartySize;
	bCurrentWantsDining = bWantsDining;

	if (!bWantsDining)
	{
		MemoryComponent->RecordMemoryEvent(
			FName(TEXT("player_declined_dining")),
			TEXT("The customer decided not to dine and ended the reception interaction."),
			FName(TEXT("entrance")),
			{ FName(TEXT("player")), FName(TEXT("customer")), FName(TEXT("reception")) },
			-0.05f,
			0.15f,
			0.3f,
			false);
		RequestMaidSpeech(FText::FromString(TEXT("了解しました、先生。必要になったらアリスを呼んでください。")));
		SetState(ECafeServiceState::Idle);
		bServiceStarted = false;
		return;
	}

	if (CurrentPartySize <= 0)
	{
		CurrentPartySize = 1;
	}

	SetState(ECafeServiceState::SelectingSeat);

	const ATableMarkerActor* BestTable = FindBestAvailableTable(CurrentPartySize);
	if (!BestTable)
	{
		PrintDebugMessage(TEXT("No available table"), FColor::Red);
		RequestMaidSpeech(FText::FromString(TEXT("申し訳ありません、先生。現在ご案内できる席がありません。")));
		SetState(ECafeServiceState::WaitingForReceptionResponse);
		return;
	}

	TargetTableID = BestTable->GetTableID();
	RequestMaidSpeech(FText::FromString(TEXT("了解しました、先生。アリスが席まで案内します！")));
	SetState(ECafeServiceState::GuidingToSeat);

	if (!MaidMoveComponent || !MaidMoveComponent->StartGuideToTable(TargetTableID))
	{
		PrintDebugMessage(TEXT("Failed to start table guide movement"), FColor::Red);
		SetState(ECafeServiceState::WaitingForReceptionResponse);
	}
}

void ACafeServiceManagerActor::ResetService()
{
	if (MaidMoveComponent)
	{
		MaidMoveComponent->OnNPCMoveCompleted.RemoveDynamic(this, &ACafeServiceManagerActor::HandleMaidMoveCompleted);
	}

	if (PlayerSeatComponent)
	{
		PlayerSeatComponent->OnPlayerSeated.RemoveDynamic(this, &ACafeServiceManagerActor::HandlePlayerSeated);
		PlayerSeatComponent->OnPlayerLeftSeat.RemoveDynamic(this, &ACafeServiceManagerActor::HandlePlayerLeftSeat);
	}

	if (MaidActionQueueComponent)
	{
		MaidActionQueueComponent->OnOrderStarted.RemoveDynamic(this, &ACafeServiceManagerActor::HandleOrderStarted);
		MaidActionQueueComponent->OnOrderServed.RemoveDynamic(this, &ACafeServiceManagerActor::HandleOrderServed);
	}

	CurrentPlayerActor = nullptr;
	TargetTableID = TEXT("");
	CurrentPartySize = 0;
	bCurrentWantsDining = false;
	bServiceStarted = false;
	ClearPendingDeparture();
	MaidMoveComponent = nullptr;
	PlayerSeatComponent = nullptr;
	MaidActionQueueComponent = nullptr;
	PendingReceptionGreetingText = FText::GetEmpty();
	bOrderServedForCurrentVisit = false;
	bDepartureConfirmedForCurrentVisit = false;
	bSocialApproachConsumed = false;
	SocialLookHeldSeconds = 0.0f;
	SocialAwaySinceWorldTime = -1.0;
	LastSocialOpportunityWorldTime = -1.0;
	LastOrderServedWorldTime = -1.0;
	LastServedItem = TEXT("");
	LastSocialTopicMemoryID = TEXT("");
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReceptionGreetingTimerHandle);
	}
	SetState(ECafeServiceState::Idle);
}

bool ACafeServiceManagerActor::CancelReceptionForExplicitNPCCommand(const FString& Reason)
{
	const bool bIsCancelableReceptionState =
		CurrentState == ECafeServiceState::MaidApproachingPlayer
		|| CurrentState == ECafeServiceState::ReceptionGreeting
		|| CurrentState == ECafeServiceState::WaitingForReceptionResponse;
	if (!bIsCancelableReceptionState)
	{
		return false;
	}

	const FString PreviousStateName = GetStateName(CurrentState);
	ResetService();
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[CafeService] Reception canceled by explicit NPC command. PreviousState=%s Reason=%s"),
		*PreviousStateName,
		*Reason);
	return true;
}

ATableMarkerActor* ACafeServiceManagerActor::FindBestAvailableTable(int32 PartySize) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	ATableMarkerActor* BestTable = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (TActorIterator<ATableMarkerActor> It(World); It; ++It)
	{
		ATableMarkerActor* TableMarker = *It;
		if (!TableMarker || TableMarker->IsTableOccupied() || TableMarker->GetSeatCount() < PartySize)
		{
			continue;
		}

		const float CandidateDistanceSq = CurrentPlayerActor
			? FVector::DistSquared(TableMarker->GetActorLocation(), CurrentPlayerActor->GetActorLocation())
			: 0.0f;

		const bool bHasHigherPriority = !BestTable || TableMarker->GetPriority() > BestTable->GetPriority();
		const bool bHasSamePriorityAndIsCloser = BestTable
			&& TableMarker->GetPriority() == BestTable->GetPriority()
			&& CandidateDistanceSq < BestDistanceSq;

		if (bHasHigherPriority || bHasSamePriorityAndIsCloser)
		{
			BestTable = TableMarker;
			BestDistanceSq = CandidateDistanceSq;
		}
	}

	return BestTable;
}

void ACafeServiceManagerActor::HandleMaidMoveCompleted(FName MoveReason, FString TargetID, bool bSuccess)
{
	if (bMaidApproachingForMealConfirmation && MoveReason == FName(TEXT("Actor")))
	{
		bMaidApproachingForMealConfirmation = false;
		if (bAwaitingMealCompletionConfirmation)
		{
			RequestMaidSpeech(MealCompletionQuestion);
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[CafeService] maid asked whether dining is finished. ApproachSucceeded=%s Table=%s"),
				bSuccess ? TEXT("true") : TEXT("false"),
				*PendingDepartureTableID);
		}
		return;
	}

	if (CurrentState == ECafeServiceState::MaidApproachingPlayer && MoveReason == FName(TEXT("Actor")))
	{
		if (bSuccess)
		{
			SetState(ECafeServiceState::ReceptionGreeting);
			RequestReceptionGreetingAfterDelay(FText::FromString(TEXT("いらっしゃいませ、先生。お食事クエストを開始しますか？")));
		}
		else
		{
			PrintDebugMessage(TEXT("Maid failed to approach player"), FColor::Red);
			SetState(ECafeServiceState::Idle);
			bServiceStarted = false;
		}
		return;
	}

	if (CurrentState == ECafeServiceState::GuidingToSeat && MoveReason == FName(TEXT("Table")))
	{
		if (bSuccess)
		{
			SetState(ECafeServiceState::WaitingForPlayerSeated);
			RequestMaidSpeech(FText::FromString(TEXT("目的地に到着しました、先生。こちらの席へどうぞ。")));
		}
		else
		{
			PrintDebugMessage(FString::Printf(TEXT("Maid failed to reach table: %s"), *TargetID), FColor::Red);
			SetState(ECafeServiceState::WaitingForReceptionResponse);
		}
	}
}

void ACafeServiceManagerActor::HandlePlayerSeated(FString SeatID, FString TableID)
{
	ClearPendingDeparture();
	if (CurrentState == ECafeServiceState::WaitingForPlayerSeated && TableID == TargetTableID)
	{
		if (MemoryComponent->bRunRecallValidationWhenPlayerSits)
		{
			FNPCMemoryEntry RecalledMemory;
			FNPCRecallScore RecallScore;
			MemoryComponent->TryRecallBestMemory(
				{ FName(TEXT("player")), FName(TEXT("customer")), FName(TEXT("reception")), FName(TEXT("service")) },
				0.8f,
				true,
				RecalledMemory,
				RecallScore);
		}

		MemoryComponent->RecordMemoryEvent(
			FName(TEXT("player_seated")),
			FString::Printf(TEXT("The maid successfully guided the customer to table %s."), *TableID),
			FName(TEXT("dining_area")),
			{ FName(TEXT("player")), FName(TEXT("customer")), FName(TEXT("table")), FName(TEXT("service")) },
			0.15f,
			0.25f,
			0.4f,
			false);
		SetState(ECafeServiceState::WaitingForOrder);
		RequestMaidSpeech(FText::FromString(TEXT("注文が決まったら、アリスを呼んでください。")));
	}
}

void ACafeServiceManagerActor::HandlePlayerLeftSeat(FString SeatID, FString TableID)
{
	PendingDepartureSeatID = SeatID.TrimStartAndEnd();
	PendingDepartureTableID = TableID.TrimStartAndEnd().IsEmpty()
		? TargetTableID
		: TableID.TrimStartAndEnd();
	PendingDepartureOrigin = CurrentPlayerActor
		? CurrentPlayerActor->GetActorLocation()
		: FVector::ZeroVector;
	OutsideTableAreaSinceWorldTime = -1.0;
	if (!bRequireOrderServedBeforeDeparture || bOrderServedForCurrentVisit)
	{
		ScheduleMealCompletionConfirmation();
	}
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[CafeService] player stood up; meal completion confirmation started. Seat=%s Table=%s OrderServed=%s"),
		*SeatID,
		*PendingDepartureTableID,
		bOrderServedForCurrentVisit ? TEXT("true") : TEXT("false"));
}

void ACafeServiceManagerActor::HandleOrderStarted(FString TableID, FString Item)
{
	ClearPendingDeparture();
	if (MaidActionQueueComponent)
	{
		MaidActionQueueComponent->SetDiningServiceCompleted(false, TEXT(""));
	}
	bOrderServedForCurrentVisit = false;
	bDepartureConfirmedForCurrentVisit = false;
	bSocialApproachConsumed = false;
	SocialLookHeldSeconds = 0.0f;
	SocialAwaySinceWorldTime = -1.0;
	LastOrderServedWorldTime = -1.0;
	LastServedItem = TEXT("");
	SetState(ECafeServiceState::OrderReceived);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[CafeService] new order cycle started; departure state reset. Table=%s Item=%s"),
		*TableID,
		*Item);
}

void ACafeServiceManagerActor::HandleOrderServed(FString TableID, FString Item)
{
	bOrderServedForCurrentVisit = true;
	bDepartureConfirmedForCurrentVisit = false;
	const FString ResolvedItem = Item.TrimStartAndEnd().IsEmpty() ? TEXT("the order") : Item.TrimStartAndEnd();
	LastServedItem = ResolvedItem;
	LastOrderServedWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
	bSocialApproachConsumed = false;
	SocialLookHeldSeconds = 0.0f;
	SocialAwaySinceWorldTime = -1.0;
	MemoryComponent->RecordMemoryEvent(
		FName(TEXT("order_served")),
		FString::Printf(TEXT("Alice delivered %s to table %s."), *ResolvedItem, *TableID),
		FName(TEXT("dining_area")),
		{ FName(TEXT("player")), FName(TEXT("customer")), FName(TEXT("order")), FName(TEXT("service")) },
		0.25f,
		0.3f,
		0.5f,
		false);
	SetState(ECafeServiceState::Served);
	if (!PendingDepartureTableID.IsEmpty())
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[CafeService] discarded a departure candidate created before the order was served. A fresh post-serve stand-up is required. Table=%s"),
			*PendingDepartureTableID);
		ClearPendingDeparture();
	}
	UE_LOG(LogTemp, Log, TEXT("[CafeService] real order-served event recorded. Table=%s Item=%s"), *TableID, *ResolvedItem);
}

void ACafeServiceManagerActor::UpdatePostServiceSocialOpportunity(float DeltaSeconds)
{
	if (!bEnablePostServiceSocialInitiative
		|| !bServiceStarted
		|| !CurrentPlayerActor
		|| !MaidNPCActor)
	{
		SocialLookHeldSeconds = 0.0f;
		return;
	}

	const float DistanceToMaid = FVector::Dist2D(
		CurrentPlayerActor->GetActorLocation(),
		MaidNPCActor->GetActorLocation());
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double CurrentTime = World->GetTimeSeconds();
	const bool bProtectedTaskInProgress = AutonomyComponent
		&& AutonomyComponent->HasProtectedTaskInProgress();
	if (bProtectedTaskInProgress)
	{
		SocialLookHeldSeconds = 0.0f;
		SocialAwaySinceWorldTime = -1.0;
		return;
	}

	const float ResetRadius = FMath::Max(SocialApproachResetRadius, SocialApproachRadius + 25.0f);
	if (DistanceToMaid >= ResetRadius)
	{
		if (SocialAwaySinceWorldTime < 0.0)
		{
			SocialAwaySinceWorldTime = CurrentTime;
		}
		if (bSocialApproachConsumed
			&& CurrentTime - SocialAwaySinceWorldTime >= FMath::Max(SocialApproachRearmSeconds, 0.0f))
		{
			bSocialApproachConsumed = false;
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[NPCSocial] approach rearmed after sustained distance. Distance=%.1f AwaySeconds=%.1f"),
				DistanceToMaid,
				CurrentTime - SocialAwaySinceWorldTime);
		}
		SocialLookHeldSeconds = 0.0f;
		return;
	}
	SocialAwaySinceWorldTime = -1.0;

	if (bSocialApproachConsumed
		|| CurrentState != ECafeServiceState::Served
		|| !bOrderServedForCurrentVisit
		|| bAwaitingMealCompletionConfirmation
		|| DistanceToMaid > SocialApproachRadius
		|| (PlayerSeatComponent && PlayerSeatComponent->bIsSeated))
	{
		SocialLookHeldSeconds = 0.0f;
		return;
	}

	if (LastOrderServedWorldTime < 0.0
		|| CurrentTime - LastOrderServedWorldTime < MinimumSecondsAfterOrderServedForSocialOpportunity
		|| (LastSocialOpportunityWorldTime >= 0.0
			&& CurrentTime - LastSocialOpportunityWorldTime < SocialOpportunityCooldownSeconds))
	{
		SocialLookHeldSeconds = 0.0f;
		return;
	}

	const bool bAutonomyActivityRunning = AutonomyComponent && AutonomyComponent->IsAutonomyActivityRunning();
	if (MaidActionQueueComponent
		&& MaidActionQueueComponent->IsBusyForAutonomy()
		&& !bAutonomyActivityRunning)
	{
		SocialLookHeldSeconds = 0.0f;
		return;
	}

	float LookDot = -1.0f;
	if (!IsPlayerLookingAtMaid(LookDot) || LookDot < SocialLookDotThreshold)
	{
		SocialLookHeldSeconds = 0.0f;
		return;
	}

	SocialLookHeldSeconds += FMath::Max(DeltaSeconds, 0.0f);
	if (SocialLookHeldSeconds < FMath::Max(SocialLookHoldSeconds, 0.1f))
	{
		return;
	}

	ULLMClient* InteractiveClient = ULLMClient::GetMostRecentInteractiveClient();
	if (!InteractiveClient
		|| !InteractiveClient->IsReadyForSocialOpportunity(MinimumQuietSecondsBeforeSocialOpportunity))
	{
		SocialLookHeldSeconds = 0.0f;
		return;
	}

	const FString ContextJson = BuildSocialOpportunityContext(DistanceToMaid, LookDot);
	const int32 RequestId = InteractiveClient->SendSocialOpportunityMessage(ContextJson);
	if (RequestId <= 0)
	{
		return;
	}

	bSocialApproachConsumed = true;
	SocialLookHeldSeconds = 0.0f;
	LastSocialOpportunityWorldTime = CurrentTime;
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[NPCSocial] opportunity triggered. RequestId=%d Distance=%.1f LookDot=%.2f"),
		RequestId,
		DistanceToMaid,
		LookDot);
}

bool ACafeServiceManagerActor::IsPlayerLookingAtMaid(float& OutLookDot) const
{
	OutLookDot = -1.0f;
	const UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController || !MaidNPCActor)
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector MaidFocusLocation = MaidNPCActor->GetActorLocation() + FVector(0.0f, 0.0f, 110.0f);
	const FVector ToMaid = (MaidFocusLocation - ViewLocation).GetSafeNormal();
	if (ToMaid.IsNearlyZero())
	{
		return false;
	}

	OutLookDot = FVector::DotProduct(ViewRotation.Vector(), ToMaid);
	return true;
}

FString ACafeServiceManagerActor::BuildSocialOpportunityContext(float DistanceToMaid, float LookDot)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("event"), TEXT("post_service_social_opportunity"));
	Root->SetStringField(TEXT("situation"), TEXT("The customer left the seat, approached Alice, and kept looking at her without calling her."));
	Root->SetStringField(TEXT("service_state"), TEXT("served"));
	Root->SetStringField(TEXT("last_served_item"), LastServedItem.IsEmpty() ? TEXT("unknown") : LastServedItem);
	Root->SetBoolField(TEXT("first_opportunity_this_approach"), true);
	Root->SetBoolField(TEXT("player_is_seated"), PlayerSeatComponent && PlayerSeatComponent->bIsSeated);
	Root->SetBoolField(TEXT("departure_confirmed"), bDepartureConfirmedForCurrentVisit);
	Root->SetNumberField(TEXT("distance_to_maid_cm"), DistanceToMaid);
	Root->SetNumberField(TEXT("camera_look_dot"), LookDot);
	Root->SetNumberField(TEXT("look_hold_seconds"), SocialLookHeldSeconds);

	FNPCMemoryEntry TopicMemory;
	FNPCRecallScore TopicRecallScore;
	const bool bHasMemoryTopic = SelectSocialTopicMemory(TopicMemory, TopicRecallScore);
	Root->SetBoolField(TEXT("memory_topic_available"), bHasMemoryTopic);
	if (bHasMemoryTopic)
	{
		TSharedPtr<FJsonObject> MemoryTopic = MakeShared<FJsonObject>();
		MemoryTopic->SetStringField(TEXT("event"), TopicMemory.EventTag.ToString());
		MemoryTopic->SetStringField(TEXT("summary"), TopicMemory.Summary);
		MemoryTopic->SetNumberField(TEXT("valence"), TopicMemory.Valence);
		MemoryTopic->SetNumberField(TEXT("importance"), TopicMemory.Importance);
		MemoryTopic->SetNumberField(TEXT("relevance"), TopicRecallScore.CueRelevance);
		MemoryTopic->SetNumberField(TEXT("memory_strength"), TopicRecallScore.MemoryStrength);
		Root->SetObjectField(TEXT("preferred_conversation_topic"), MemoryTopic);
	}

	if (AutonomyComponent)
	{
		Root->SetStringField(
			TEXT("maid_current_activity"),
			AutonomyComponent->CurrentActivityID.IsEmpty() ? TEXT("idle") : AutonomyComponent->CurrentActivityID);
		Root->SetNumberField(TEXT("maid_boredom"), AutonomyComponent->MotiveState.Boredom);
		Root->SetNumberField(TEXT("maid_duty"), AutonomyComponent->MotiveState.Duty);
		Root->SetNumberField(TEXT("maid_social_attention"), AutonomyComponent->MotiveState.SocialAttention);
		Root->SetNumberField(TEXT("maid_energy"), AutonomyComponent->ResourceState.Energy);
		Root->SetStringField(
			TEXT("maid_plan_goal"),
			AutonomyComponent->ActivePlan.Goal.IsEmpty() ? TEXT("none") : AutonomyComponent->ActivePlan.Goal);
	}

	if (EmotionComponent)
	{
		Root->SetStringField(TEXT("maid_emotion"), EmotionComponent->CurrentState.EmotionLabel);
		Root->SetNumberField(TEXT("maid_short_term_valence"), EmotionComponent->CurrentState.ShortTermValence);
		Root->SetNumberField(TEXT("maid_mood_valence"), EmotionComponent->CurrentState.MoodValence);
		Root->SetNumberField(TEXT("maid_arousal"), EmotionComponent->CurrentState.Arousal);
	}

	FString ContextJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ContextJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return ContextJson;
}

bool ACafeServiceManagerActor::SelectSocialTopicMemory(FNPCMemoryEntry& OutMemory, FNPCRecallScore& OutScore)
{
	if (!bPreferMemoryTopicsForSocialInitiative || !MemoryComponent || MemoryComponent->Memories.IsEmpty())
	{
		return false;
	}

	static const TArray<FName> SocialCueTags = {
		FName(TEXT("player")),
		FName(TEXT("customer")),
		FName(TEXT("dialogue")),
		FName(TEXT("praise")),
		FName(TEXT("gratitude")),
		FName(TEXT("complaint")),
		FName(TEXT("apology")),
		FName(TEXT("service"))
	};
	static const TArray<FString> SocialEventTerms = {
		TEXT("praise"),
		TEXT("gratitude"),
		TEXT("complaint"),
		TEXT("insult"),
		TEXT("apology"),
		TEXT("conversation"),
		TEXT("disclosure"),
		TEXT("promise")
	};

	const float Attention = FMath::Clamp(SocialMemoryAttentionAvailability, 0.0f, 1.0f);
	const float MinimumImportance = FMath::Clamp(SocialMemoryMinimumImportance, 0.0f, 1.0f);
	float BestSelectionScore = -1.0f;
	const FNPCMemoryEntry* BestMemory = nullptr;
	FNPCRecallScore BestRecallScore;
	for (const FNPCMemoryEntry& Memory : MemoryComponent->Memories)
	{
		const FString EventName = Memory.EventTag.ToString();
		const bool bSocialEvent = SocialEventTerms.ContainsByPredicate([&EventName](const FString& Term)
		{
			return EventName.Contains(Term, ESearchCase::IgnoreCase);
		});
		const bool bDialogueMemory = Memory.CueTags.Contains(FName(TEXT("dialogue")));
		if (Memory.Summary.IsEmpty()
			|| Memory.Importance < MinimumImportance
			|| (!bSocialEvent && !bDialogueMemory))
		{
			continue;
		}

		const FNPCRecallScore RecallScore = MemoryComponent->CalculateRecallScore(
			Memory,
			SocialCueTags,
			Attention);
		const float RepeatPenalty = Memory.MemoryID.Equals(LastSocialTopicMemoryID, ESearchCase::CaseSensitive)
			? 0.2f
			: 0.0f;
		const float SelectionScore = RecallScore.RecallProbability
			+ Memory.Importance * 0.25f
			- RepeatPenalty;
		if (!BestMemory || SelectionScore > BestSelectionScore)
		{
			BestMemory = &Memory;
			BestRecallScore = RecallScore;
			BestSelectionScore = SelectionScore;
		}
	}

	if (!BestMemory)
	{
		return false;
	}

	OutMemory = *BestMemory;
	OutScore = BestRecallScore;
	LastSocialTopicMemoryID = BestMemory->MemoryID;
	MemoryComponent->MarkMemoryRecalled(BestMemory->MemoryID);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[NPCSocial] memory topic selected. Event=%s Importance=%.2f RecallProbability=%.3f Summary=%s"),
		*BestMemory->EventTag.ToString(),
		BestMemory->Importance,
		BestRecallScore.RecallProbability,
		*BestMemory->Summary);
	return true;
}

void ACafeServiceManagerActor::StartDepartureEvaluation()
{
	if (PendingDepartureTableID.IsEmpty() || bDepartureConfirmedForCurrentVisit)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DepartureEvaluationTimerHandle);
		World->GetTimerManager().SetTimer(
			DepartureEvaluationTimerHandle,
			this,
			&ACafeServiceManagerActor::EvaluatePendingDeparture,
			0.5f,
			true,
			0.5f);
	}
}

void ACafeServiceManagerActor::EvaluatePendingDeparture()
{
	UWorld* World = GetWorld();
	if (!World || !CurrentPlayerActor || PendingDepartureTableID.IsEmpty())
	{
		ClearPendingDeparture();
		return;
	}
	if (PlayerSeatComponent && PlayerSeatComponent->bIsSeated)
	{
		ClearPendingDeparture();
		return;
	}
	if (bRequireOrderServedBeforeDeparture && !bOrderServedForCurrentVisit)
	{
		OutsideTableAreaSinceWorldTime = -1.0;
		return;
	}

	const ATableMarkerActor* Table = FindTableByID(PendingDepartureTableID);
	const FVector ReferenceLocation = Table ? Table->GetActorLocation() : PendingDepartureOrigin;
	const float Distance2D = FVector::Dist2D(CurrentPlayerActor->GetActorLocation(), ReferenceLocation);
	if (Distance2D < FMath::Max(DepartureDistanceThreshold, 50.0f))
	{
		OutsideTableAreaSinceWorldTime = -1.0;
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();
	if (OutsideTableAreaSinceWorldTime < 0.0)
	{
		OutsideTableAreaSinceWorldTime = CurrentTime;
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[CafeService] player is outside table distance; confirmation started. Table=%s Distance=%.1f RequiredSeconds=%.1f"),
			*PendingDepartureTableID,
			Distance2D,
			DepartureConfirmationSeconds);
		return;
	}

	if (CurrentTime - OutsideTableAreaSinceWorldTime >= FMath::Max(DepartureConfirmationSeconds, 0.5f))
	{
		ConfirmPlayerDeparture();
	}
}

void ACafeServiceManagerActor::ClearPendingDeparture()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DepartureEvaluationTimerHandle);
		World->GetTimerManager().ClearTimer(MealCompletionApproachDelayTimerHandle);
	}
	PendingDepartureSeatID = TEXT("");
	PendingDepartureTableID = TEXT("");
	PendingDepartureOrigin = FVector::ZeroVector;
	OutsideTableAreaSinceWorldTime = -1.0;
	bAwaitingMealCompletionConfirmation = false;
	bMaidApproachingForMealConfirmation = false;
	if (MaidActionQueueComponent)
	{
		MaidActionQueueComponent->SetMealCompletionConfirmationPending(false, TEXT(""));
	}
}

void ACafeServiceManagerActor::ScheduleMealCompletionConfirmation()
{
	if (PendingDepartureTableID.IsEmpty()
		|| bDepartureConfirmedForCurrentVisit
		|| (bRequireOrderServedBeforeDeparture && !bOrderServedForCurrentVisit))
	{
		return;
	}

	bAwaitingMealCompletionConfirmation = true;
	if (MaidActionQueueComponent)
	{
		MaidActionQueueComponent->SetMealCompletionConfirmationPending(true, PendingDepartureTableID);
	}

	UWorld* World = GetWorld();
	const float DelaySeconds = FMath::Max(MealCompletionApproachDelaySeconds, 6.0f);
	if (World && DelaySeconds > KINDA_SMALL_NUMBER)
	{
		World->GetTimerManager().ClearTimer(MealCompletionApproachDelayTimerHandle);
		World->GetTimerManager().SetTimer(
			MealCompletionApproachDelayTimerHandle,
			this,
			&ACafeServiceManagerActor::BeginMealCompletionConfirmation,
			DelaySeconds,
			false);
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[CafeService] meal completion approach scheduled. Table=%s Delay=%.1fs"),
			*PendingDepartureTableID,
			DelaySeconds);
		return;
	}

	BeginMealCompletionConfirmation();
}

void ACafeServiceManagerActor::BeginMealCompletionConfirmation()
{
	if (PendingDepartureTableID.IsEmpty()
		|| bDepartureConfirmedForCurrentVisit
		|| (bRequireOrderServedBeforeDeparture && !bOrderServedForCurrentVisit))
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DepartureEvaluationTimerHandle);
	}
	bAwaitingMealCompletionConfirmation = true;
	if (MaidActionQueueComponent)
	{
		MaidActionQueueComponent->SetMealCompletionConfirmationPending(true, PendingDepartureTableID);
	}
	if (AutonomyComponent)
	{
		AutonomyComponent->NotifyPlayerInteraction();
	}

	bMaidApproachingForMealConfirmation = MaidMoveComponent
		&& CurrentPlayerActor
		&& MaidMoveComponent->MoveNearActor(CurrentPlayerActor, MealCompletionApproachDistance);
	if (!bMaidApproachingForMealConfirmation)
	{
		RequestMaidSpeech(MealCompletionQuestion);
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[CafeService] awaiting explicit meal completion confirmation. Table=%s ApproachingPlayer=%s"),
		*PendingDepartureTableID,
		bMaidApproachingForMealConfirmation ? TEXT("true") : TEXT("false"));
}

bool ACafeServiceManagerActor::SubmitMealCompletionConfirmation(bool bFinished)
{
	if (!bOrderServedForCurrentVisit)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CafeService] ignored meal completion confirmation before an order was served."));
		return false;
	}

	if (!bFinished)
	{
		if (!bAwaitingMealCompletionConfirmation)
		{
			return false;
		}
		const FString ContinuedTableID = PendingDepartureTableID;
		ClearPendingDeparture();
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[CafeService] player has not finished dining; cleanup remains inactive. Table=%s"),
			*ContinuedTableID);
		return true;
	}

	if (PendingDepartureTableID.IsEmpty())
	{
		PendingDepartureTableID = TargetTableID.TrimStartAndEnd();
		if (PendingDepartureTableID.IsEmpty() && MaidActionQueueComponent)
		{
			PendingDepartureTableID = MaidActionQueueComponent->GetCurrentServiceTableID().TrimStartAndEnd();
		}
	}
	if (PendingDepartureTableID.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[CafeService] meal completion confirmed, but no service table could be resolved."));
		return false;
	}

	ConfirmPlayerDeparture();
	return bDepartureConfirmedForCurrentVisit;
}

void ACafeServiceManagerActor::ConfirmPlayerDeparture()
{
	if (bDepartureConfirmedForCurrentVisit || PendingDepartureTableID.IsEmpty())
	{
		return;
	}

	const FString ConfirmedSeatID = PendingDepartureSeatID;
	const FString ConfirmedTableID = PendingDepartureTableID;
	bDepartureConfirmedForCurrentVisit = true;
	ClearPendingDeparture();
	if (MaidActionQueueComponent)
	{
		MaidActionQueueComponent->NotifyPlayerDepartureConfirmed(ConfirmedTableID);
	}
	MemoryComponent->RecordMemoryEvent(
		FName(TEXT("player_left_table")),
		FString::Printf(TEXT("The customer finished dining and left table %s."), *ConfirmedTableID),
		FName(TEXT("dining_area")),
		{ FName(TEXT("player")), FName(TEXT("customer")), FName(TEXT("table")), FName(TEXT("cleanup")) },
		0.0f,
		0.25f,
		0.55f,
		false);
	if (AutonomyComponent)
	{
		AutonomyComponent->SetWorldStateTag(FName(TEXT("dining_area_needs_tidy")), true, true);
	}
	SetState(ECafeServiceState::Served);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[CafeService] player departure confirmed; cleanup task activated. Seat=%s Table=%s"),
		*ConfirmedSeatID,
		*ConfirmedTableID);
}

const ATableMarkerActor* ACafeServiceManagerActor::FindTableByID(const FString& TableID) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ATableMarkerActor> It(World); It; ++It)
	{
		const ATableMarkerActor* Table = *It;
		if (Table && Table->GetTableID().Equals(TableID.TrimStartAndEnd(), ESearchCase::IgnoreCase))
		{
			return Table;
		}
	}
	return nullptr;
}

void ACafeServiceManagerActor::SetState(ECafeServiceState NewState)
{
	CurrentState = NewState;
	OnCafeServiceStateChanged.Broadcast(CurrentState);
	PrintDebugMessage(FString::Printf(TEXT("Cafe state: %s"), *GetStateName(CurrentState)), FColor::Cyan);
}

void ACafeServiceManagerActor::RequestReceptionGreetingAfterDelay(const FText& SpeechText)
{
	PendingReceptionGreetingText = SpeechText;

	UWorld* World = GetWorld();
	const float GreetingDelay = FMath::Max(ReceptionGreetingDelaySeconds, 0.0f);
	if (World && GreetingDelay > KINDA_SMALL_NUMBER)
	{
		World->GetTimerManager().ClearTimer(ReceptionGreetingTimerHandle);
		World->GetTimerManager().SetTimer(
			ReceptionGreetingTimerHandle,
			this,
			&ACafeServiceManagerActor::HandleReceptionGreetingDelayFinished,
			GreetingDelay,
			false);
		UE_LOG(LogTemp, Warning, TEXT("[CafeService] Reception greeting scheduled after %.1fs."), GreetingDelay);
		return;
	}

	HandleReceptionGreetingDelayFinished();
}

void ACafeServiceManagerActor::HandleReceptionGreetingDelayFinished()
{
	if (CurrentState != ECafeServiceState::ReceptionGreeting || PendingReceptionGreetingText.IsEmpty())
	{
		return;
	}

	RequestMaidSpeech(PendingReceptionGreetingText);
	PendingReceptionGreetingText = FText::GetEmpty();
	SetState(ECafeServiceState::WaitingForReceptionResponse);
}

void ACafeServiceManagerActor::RequestMaidSpeech(const FText& SpeechText)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::White, SpeechText.ToString());
	}

	OnMaidSpeechRequested.Broadcast(SpeechText);
}

void ACafeServiceManagerActor::PrintDebugMessage(const FString& Message, const FColor& Color) const
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, Color, Message);
	}
}

FString ACafeServiceManagerActor::GetStateName(ECafeServiceState State) const
{
	const UEnum* StateEnum = StaticEnum<ECafeServiceState>();
	return StateEnum ? StateEnum->GetNameStringByValue(static_cast<int64>(State)) : TEXT("Unknown");
}
