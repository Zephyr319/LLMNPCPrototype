// Copyright Epic Games, Inc. All Rights Reserved.

#include "CafeServiceManagerActor.h"

#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "NPCMoveToTableComponent.h"
#include "PlayerSeatComponent.h"
#include "TableMarkerActor.h"

ACafeServiceManagerActor::ACafeServiceManagerActor()
{
	PrimaryActorTick.bCanEverTick = false;

	CurrentState = ECafeServiceState::Idle;
	MaidNPCActor = nullptr;
	CurrentPlayerActor = nullptr;
	TargetTableID = TEXT("");
	CurrentPartySize = 0;
	bCurrentWantsDining = false;
	MaidApproachDistance = 260.0f;
	bServiceStarted = false;
	MaidMoveComponent = nullptr;
	PlayerSeatComponent = nullptr;
}

void ACafeServiceManagerActor::StartServiceForPlayer(AActor* PlayerActor)
{
	if (bServiceStarted)
	{
		return;
	}

	if (MaidMoveComponent)
	{
		MaidMoveComponent->OnNPCMoveCompleted.RemoveDynamic(this, &ACafeServiceManagerActor::HandleMaidMoveCompleted);
	}

	if (PlayerSeatComponent)
	{
		PlayerSeatComponent->OnPlayerSeated.RemoveDynamic(this, &ACafeServiceManagerActor::HandlePlayerSeated);
	}

	MaidMoveComponent = nullptr;
	PlayerSeatComponent = nullptr;

	if (!PlayerActor || !MaidNPCActor)
	{
		PrintDebugMessage(TEXT("Cafe service missing player or maid"), FColor::Red);
		return;
	}

	CurrentPlayerActor = PlayerActor;
	TargetTableID = TEXT("");
	CurrentPartySize = 0;
	bCurrentWantsDining = false;
	MaidMoveComponent = MaidNPCActor->FindComponentByClass<UNPCMoveToTableComponent>();
	PlayerSeatComponent = PlayerActor->FindComponentByClass<UPlayerSeatComponent>();

	if (!MaidMoveComponent || !PlayerSeatComponent)
	{
		PrintDebugMessage(TEXT("Cafe service missing required components"), FColor::Red);
		CurrentPlayerActor = nullptr;
		MaidMoveComponent = nullptr;
		PlayerSeatComponent = nullptr;
		return;
	}

	MaidMoveComponent->OnNPCMoveCompleted.RemoveDynamic(this, &ACafeServiceManagerActor::HandleMaidMoveCompleted);
	MaidMoveComponent->OnNPCMoveCompleted.AddDynamic(this, &ACafeServiceManagerActor::HandleMaidMoveCompleted);

	PlayerSeatComponent->OnPlayerSeated.RemoveDynamic(this, &ACafeServiceManagerActor::HandlePlayerSeated);
	PlayerSeatComponent->OnPlayerSeated.AddDynamic(this, &ACafeServiceManagerActor::HandlePlayerSeated);

	bServiceStarted = true;
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
		RequestMaidSpeech(FText::FromString(TEXT("かしこまりました。ご利用の際はお声がけください。")));
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
		RequestMaidSpeech(FText::FromString(TEXT("申し訳ありません。ただいまご案内できるお席がありません。")));
		SetState(ECafeServiceState::WaitingForReceptionResponse);
		return;
	}

	TargetTableID = BestTable->GetTableID();
	RequestMaidSpeech(FText::FromString(TEXT("かしこまりました。お席へご案内します。")));
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
	}

	CurrentPlayerActor = nullptr;
	TargetTableID = TEXT("");
	CurrentPartySize = 0;
	bCurrentWantsDining = false;
	bServiceStarted = false;
	MaidMoveComponent = nullptr;
	PlayerSeatComponent = nullptr;
	SetState(ECafeServiceState::Idle);
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
	if (CurrentState == ECafeServiceState::MaidApproachingPlayer && MoveReason == FName(TEXT("Actor")))
	{
		if (bSuccess)
		{
			SetState(ECafeServiceState::ReceptionGreeting);
			RequestMaidSpeech(FText::FromString(TEXT("\u3044\u3089\u3063\u3057\u3083\u3044\u307E\u305B\u3002\u304A\u98DF\u4E8B\u3067\u306E\u3054\u5229\u7528\u3067\u3057\u3087\u3046\u304B\uFF1F")));
			SetState(ECafeServiceState::WaitingForReceptionResponse);
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
			RequestMaidSpeech(FText::FromString(TEXT("こちらのお席へどうぞ。")));
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
	if (CurrentState == ECafeServiceState::WaitingForPlayerSeated && TableID == TargetTableID)
	{
		SetState(ECafeServiceState::WaitingForOrder);
		RequestMaidSpeech(FText::FromString(TEXT("ご注文がお決まりでしたら、お呼びください。")));
	}
}

void ACafeServiceManagerActor::SetState(ECafeServiceState NewState)
{
	CurrentState = NewState;
	OnCafeServiceStateChanged.Broadcast(CurrentState);
	PrintDebugMessage(FString::Printf(TEXT("Cafe state: %s"), *GetStateName(CurrentState)), FColor::Cyan);
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
