// Copyright Epic Games, Inc. All Rights Reserved.

#include "NPCMoveToTableComponent.h"

#include "CafeTargetPointActor.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "FurnitureMarkerActor.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "TableMarkerActor.h"
#include "UObject/UnrealType.h"

namespace
{
struct FNPCApproachCandidate
{
	FVector Location = FVector::ZeroVector;
	float Score = 0.0f;
	float PathLength = 0.0f;
	bool bHasLineOfSight = false;
};

struct FNPCApproachRejectStats
{
	int32 NavProjectFailed = 0;
	int32 PathFailed = 0;
	int32 BlockedByOverlap = 0;
	int32 InsideBounds = 0;
	int32 TooClose = 0;
	int32 Valid = 0;
};

bool IsCounterMoveHint(const FString& TargetHint)
{
	const FString TrimmedHint = TargetHint.TrimStartAndEnd();
	return TrimmedHint.Contains(TEXT("counter"), ESearchCase::IgnoreCase)
		|| TrimmedHint.Contains(TEXT("counter_side"), ESearchCase::IgnoreCase)
		|| TrimmedHint.Contains(TEXT("bar"), ESearchCase::IgnoreCase)
		|| TrimmedHint.Contains(TEXT("カウンター"), ESearchCase::IgnoreCase)
		|| TrimmedHint.Contains(TEXT("吧台"), ESearchCase::IgnoreCase);
}

bool IsFuzzyGuideTableTarget(const FString& RequestedTable)
{
	const FString TrimmedTable = RequestedTable.TrimStartAndEnd();
	if (TrimmedTable.IsEmpty())
	{
		return true;
	}

	return TrimmedTable.Equals(TEXT("nearest"), ESearchCase::IgnoreCase)
		|| TrimmedTable.Equals(TEXT("seat"), ESearchCase::IgnoreCase)
		|| TrimmedTable.Equals(TEXT("table"), ESearchCase::IgnoreCase)
		|| TrimmedTable.Equals(TEXT("any"), ESearchCase::IgnoreCase)
		|| TrimmedTable.Equals(TEXT("available"), ESearchCase::IgnoreCase)
		|| TrimmedTable.Equals(TEXT("recommended"), ESearchCase::IgnoreCase)
		|| TrimmedTable.Contains(TEXT("\u5EA7\u4F4D"))
		|| TrimmedTable.Contains(TEXT("\u5E2D"))
		|| TrimmedTable.Contains(TEXT("\u304A\u5E2D"))
		|| TrimmedTable.Contains(TEXT("\u30C6\u30FC\u30D6\u30EB"));
}

bool ShouldPreferNearestGuideTable(const FString& RequestedTable)
{
	return RequestedTable.TrimStartAndEnd().Equals(TEXT("nearest"), ESearchCase::IgnoreCase);
}

FString GetApproachDebugTargetName(const AActor* TargetActor)
{
	if (const AFurnitureMarkerActor* FurnitureMarker = Cast<AFurnitureMarkerActor>(TargetActor))
	{
		if (!FurnitureMarker->FurnitureID.IsEmpty())
		{
			return FurnitureMarker->FurnitureID;
		}
	}

	return TargetActor ? TargetActor->GetName() : TEXT("None");
}

const TCHAR* MovementModeToString(ENPCMovementMode Mode)
{
	switch (Mode)
	{
	case ENPCMovementMode::Idle:
		return TEXT("Idle");
	case ENPCMovementMode::FollowPlayer:
		return TEXT("FollowPlayer");
	case ENPCMovementMode::CommandedMove:
		return TEXT("CommandedMove");
	case ENPCMovementMode::GuidePlayer:
		return TEXT("GuidePlayer");
	case ENPCMovementMode::SeekPlayer:
		return TEXT("SeekPlayer");
	default:
		return TEXT("Unknown");
	}
}
}

UNPCMoveToTableComponent::UNPCMoveToTableComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	MoveSpeed = 120.0f;
	bUseDynamicMoveSpeed = true;
	WalkSpeed = 90.0f;
	RunSpeed = 220.0f;
	RunStartDistance = 500.0f;
	WalkResumeDistance = 250.0f;
	SlowDownDistance = 140.0f;
	MinApproachSpeed = 55.0f;
	SpeedInterpRate = 5.0f;
	bIsRunning = false;
	RemainingMoveDistance = 0.0f;
	bPrintMoveSpeedDebug = false;
	AcceptanceRadius = 20.0f;
	StopDistanceFromTable = 80.0f;
	bIsMovingToTable = false;
	CurrentMoveSpeed = 0.0f;
	CurrentMoveVelocity = FVector::ZeroVector;
	FacingYawOffsetDegrees = 0.0f;
	bFaceMovementDirection = true;
	bFaceTableOnArrival = true;
	bUseNavigationPath = true;
	CurrentPathIndex = 0;
	bStopOnCollisionHit = true;
	MovementMode = ENPCMovementMode::Idle;
	bExplicitCommandCancelsFollow = true;
	TargetLocation = FVector::ZeroVector;
	TargetTableLocation = FVector::ZeroVector;
	CurrentMoveReason = NAME_None;
	CurrentMoveTargetID = TEXT("");
	bUseAutomaticApproachLocation = true;
	ApproachSampleCount = 24;
	ApproachRingCount = 3;
	DefaultApproachDistance = 120.0f;
	ApproachDistanceStep = 40.0f;
	ApproachCollisionRadius = 35.0f;
	ApproachCollisionHalfHeight = 90.0f;
	bRandomizeAmongTopApproachCandidates = true;
	TopApproachCandidateCount = 3;
	bDrawDebugApproachCandidates = false;
	bPrintApproachCandidateRejectReasons = false;
	PlayerApproachDistance = 140.0f;
	FollowStartDistance = 280.0f;
	FollowStopDistance = 170.0f;
	FollowDesiredDistance = 160.0f;
	FollowUpdateInterval = 0.5f;
	FollowMinRepathInterval = 1.2f;
	FollowRepathPlayerMoveThreshold = 180.0f;
	bIsFollowingPlayer = false;
	bFollowMoveInProgress = false;
	bPrintFollowDebug = false;
	bIsGuidingPlayer = false;
	GuidePlayerPhase = EGuidePlayerPhase::None;
	GuidePlayerMaxDistance = 450.0f;
	GuidePlayerResumeDistance = 280.0f;
	GuideCheckInterval = 0.5f;
	bGuideApproachPlayerFirst = true;
	GuideStartPlayerDistanceThreshold = 320.0f;
	GuideStartApproachPlayerDistance = 160.0f;
	GuidePausedLineVariants = {
		TEXT("お客様、大丈夫ですか？こちらへどうぞ。"),
		TEXT("すみません、少し離れてしまいました。こちらです。"),
		TEXT("お客様、こちらについてきてください。"),
		TEXT("こちらでお待ちしていますね。")
	};
	GuideResumedLineVariants = {
		TEXT("ありがとうございます。それではご案内します。"),
		TEXT("では、続きをご案内しますね。"),
		TEXT("こちらです。ゆっくりお進みください。")
	};
	GuideArrivedLineVariants = {
		TEXT("こちらのお席へどうぞ。"),
		TEXT("こちらがご案内のお席です。"),
		TEXT("どうぞ、こちらにお掛けください。")
	};
	LastFollowPlayerLocation = FVector::ZeroVector;
	LastFollowGoalLocation = FVector::ZeroVector;
	FollowUpdateTimer = 0.0f;
	FollowRepathTimer = 0.0f;
	FollowDebugTimer = 0.0f;
	bPrintedFollowModeSkipWarning = false;
	GuideCheckTimer = 0.0f;
	bGuidePausedForPlayer = false;
	GuideTargetType = TEXT("");
	GuideTargetID = TEXT("");
	bPreserveMovementModeForGuideMove = false;
	bGuideStartedEventBroadcasted = false;
	bGuideArrivedEventBroadcasted = false;
	LastGuidePausedLine = TEXT("");
	LastGuideResumedLine = TEXT("");
	LastGuideArrivedLine = TEXT("");
	MoveSpeedDebugTimer = 0.0f;
}

void UNPCMoveToTableComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		if (bIsFollowingPlayer)
		{
			StopFollowingPlayer();
		}
		if (bIsGuidingPlayer)
		{
			StopGuidingPlayer();
		}
		if (bIsMovingToTable)
		{
			FailMoveToTable(TEXT("NPC movement owner missing"));
		}
		return;
	}

	if (DeltaTime <= KINDA_SMALL_NUMBER)
	{
		CurrentMoveVelocity = FVector::ZeroVector;
		CurrentMoveSpeed = 0.0f;
		SetOwnerComponentVelocity(Owner, CurrentMoveVelocity);
		return;
	}

	if (bIsFollowingPlayer && MovementMode == ENPCMovementMode::FollowPlayer)
	{
		bPrintedFollowModeSkipWarning = false;
		UpdateFollowPlayer(Owner, DeltaTime);
	}
	else if (bIsFollowingPlayer && MovementMode != ENPCMovementMode::FollowPlayer)
	{
		if (!bPrintedFollowModeSkipWarning)
		{
			UE_LOG(LogTemp, Warning, TEXT("Follow Tick skipped because MovementMode != FollowPlayer"));
			bPrintedFollowModeSkipWarning = true;
		}
	}

	if (bIsGuidingPlayer && MovementMode == ENPCMovementMode::GuidePlayer)
	{
		UpdateGuidePlayer(Owner, DeltaTime);
		if (bGuidePausedForPlayer)
		{
			return;
		}
	}

	if (!bIsMovingToTable)
	{
		return;
	}

	const FVector OldLocation = Owner->GetActorLocation();
	FVector MoveTarget = TargetLocation;
	if (CurrentPathPoints.Num() > 0)
	{
		while (CurrentPathPoints.IsValidIndex(CurrentPathIndex)
			&& FVector::Dist(OldLocation, CurrentPathPoints[CurrentPathIndex]) <= AcceptanceRadius)
		{
			++CurrentPathIndex;
		}

		if (!CurrentPathPoints.IsValidIndex(CurrentPathIndex))
		{
			FinishMoveToTable(Owner);
			return;
		}

		MoveTarget = CurrentPathPoints[CurrentPathIndex];
	}
	else if (FVector::Dist(OldLocation, TargetLocation) <= AcceptanceRadius)
	{
		FinishMoveToTable(Owner);
		return;
	}

	const float DistanceToCurrentMovePoint = FVector::Dist(OldLocation, MoveTarget);
	RemainingMoveDistance = CalculateRemainingPathDistance();
	const float DesiredSpeed = GetDesiredMoveSpeed(RemainingMoveDistance);
	const float EffectiveSpeedInterpRate = FMath::Max(SpeedInterpRate, 0.0f);
	CurrentMoveSpeed = EffectiveSpeedInterpRate <= KINDA_SMALL_NUMBER
		? DesiredSpeed
		: FMath::FInterpTo(CurrentMoveSpeed, DesiredSpeed, DeltaTime, EffectiveSpeedInterpRate);

	FVector DesiredLocation = MoveTarget;
	if (DistanceToCurrentMovePoint > KINDA_SMALL_NUMBER)
	{
		const float MoveStep = CurrentMoveSpeed * DeltaTime;
		const FVector MoveDirection = (MoveTarget - OldLocation).GetSafeNormal();
		DesiredLocation = OldLocation + MoveDirection * FMath::Min(MoveStep, DistanceToCurrentMovePoint);
	}

	FHitResult SweepHit;
	Owner->SetActorLocation(DesiredLocation, true, &SweepHit);

	const FVector NewLocation = Owner->GetActorLocation();
	const FVector DeltaLocation = NewLocation - OldLocation;
	CurrentMoveVelocity = DeltaLocation.IsNearlyZero()
		? FVector::ZeroVector
		: DeltaLocation.GetSafeNormal() * CurrentMoveSpeed;
	SetOwnerComponentVelocity(Owner, CurrentMoveVelocity);

	if (SweepHit.bBlockingHit && bStopOnCollisionHit)
	{
		FailMoveToTable(TEXT("NPC movement blocked"));
		return;
	}

	if (bPrintMoveSpeedDebug)
	{
		MoveSpeedDebugTimer += DeltaTime;
		if (MoveSpeedDebugTimer >= 0.5f)
		{
			PrintDebugMessage(
				FString::Printf(TEXT("Move speed | Remaining=%.1f Speed=%.1f Running=%s"), RemainingMoveDistance, CurrentMoveSpeed, bIsRunning ? TEXT("true") : TEXT("false")),
				FColor::Yellow);
			MoveSpeedDebugTimer = 0.0f;
		}
	}
	else
	{
		MoveSpeedDebugTimer = 0.0f;
	}

	if (bFaceMovementDirection && CurrentMoveSpeed > KINDA_SMALL_NUMBER)
	{
		FaceOwnerTowardDirection(Owner, DeltaLocation, DeltaTime, true);
	}

	if (CurrentPathPoints.Num() > 0)
	{
		while (CurrentPathPoints.IsValidIndex(CurrentPathIndex)
			&& FVector::Dist(NewLocation, CurrentPathPoints[CurrentPathIndex]) <= AcceptanceRadius)
		{
			++CurrentPathIndex;
		}

		if (!CurrentPathPoints.IsValidIndex(CurrentPathIndex))
		{
			FinishMoveToTable(Owner);
		}
	}
	else if (FVector::Dist(NewLocation, TargetLocation) <= AcceptanceRadius)
	{
		FinishMoveToTable(Owner);
	}
}

bool UNPCMoveToTableComponent::MoveToTableByID(const FString& TargetTableID)
{
	CancelFollowingForExplicitCommand();

	CurrentMoveReason = TEXT("Table");
	CurrentMoveTargetID = TargetTableID;

	UWorld* World = GetWorld();
	if (!World)
	{
		FailMoveToTable(TEXT("NPC movement world missing"));
		return false;
	}

	for (TActorIterator<ATableMarkerActor> It(World); It; ++It)
	{
		ATableMarkerActor* TableMarker = *It;
		if (TableMarker && TableMarker->GetTableID() == TargetTableID)
		{
			return StartMovingToTable(TableMarker);
		}
	}

	FailMoveToTable(FString::Printf(TEXT("Table not found: %s"), *TargetTableID));
	return false;
}

bool UNPCMoveToTableComponent::MoveToTableByHint(const FString& TargetHint, AActor* ReferenceActor)
{
	CancelFollowingForExplicitCommand();

	CurrentMoveReason = TEXT("Table");
	CurrentMoveTargetID = TargetHint;

	UWorld* World = GetWorld();
	if (!World)
	{
		FailMoveToTable(TEXT("NPC movement world missing"));
		return false;
	}

	const FString TrimmedHint = TargetHint.TrimStartAndEnd();
	if (IsCounterMoveHint(TrimmedHint))
	{
		if (MoveToSceneTargetByTag(TEXT("counter")))
		{
			return true;
		}

		if (MoveToFurnitureByTag(TEXT("counter")))
		{
			return true;
		}

		CurrentMoveReason = TEXT("Table");
		CurrentMoveTargetID = TargetHint;
	}

	const AActor* DistanceReferenceActor = ReferenceActor ? ReferenceActor : GetOwner();
	const bool bUseNearestTable = TrimmedHint.IsEmpty()
		|| TrimmedHint.Equals(TEXT("nearest"), ESearchCase::IgnoreCase)
		|| TrimmedHint.Equals(TEXT("empty"), ESearchCase::IgnoreCase);

	ATableMarkerActor* BestTable = nullptr;

	if (!bUseNearestTable)
	{
		for (TActorIterator<ATableMarkerActor> It(World); It; ++It)
		{
			ATableMarkerActor* TableMarker = *It;
			if (!TableMarker || TableMarker->IsTableOccupied())
			{
				continue;
			}

			if (TableMarker->GetTableID() == TrimmedHint && IsBetterTableCandidate(TableMarker, BestTable, DistanceReferenceActor))
			{
				BestTable = TableMarker;
			}
		}

		if (BestTable)
		{
			return StartMovingToTable(BestTable);
		}
	}

	for (TActorIterator<ATableMarkerActor> It(World); It; ++It)
	{
		ATableMarkerActor* TableMarker = *It;
		if (!TableMarker || TableMarker->IsTableOccupied())
		{
			continue;
		}

		const bool bIsCandidate = bUseNearestTable
			|| DoesHintMatchTableName(TrimmedHint, TableMarker)
			|| DoesHintMatchTableTags(TrimmedHint, TableMarker);

		if (bIsCandidate && IsBetterTableCandidate(TableMarker, BestTable, DistanceReferenceActor))
		{
			BestTable = TableMarker;
		}
	}

	if (BestTable)
	{
		return StartMovingToTable(BestTable);
	}

	FailMoveToTable(FString::Printf(TEXT("No table matched hint: %s"), *TargetHint));
	return false;
}

bool UNPCMoveToTableComponent::MoveNearActor(AActor* TargetActor, float StopDistance)
{
	CurrentMoveReason = TEXT("Actor");
	CurrentMoveTargetID = TargetActor ? TargetActor->GetName() : TEXT("");

	if (!TargetActor)
	{
		FailMoveToTable(TEXT("Target actor not found"));
		return false;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		FailMoveToTable(TEXT("NPC movement owner missing"));
		return false;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	const FVector TargetActorLocation = TargetActor->GetActorLocation();
	FVector DirectionToTarget = TargetActorLocation - OwnerLocation;
	DirectionToTarget.Z = 0.0f;
	if (DirectionToTarget.IsNearlyZero())
	{
		DirectionToTarget = -TargetActor->GetActorForwardVector();
		DirectionToTarget.Z = 0.0f;
	}
	if (DirectionToTarget.IsNearlyZero())
	{
		DirectionToTarget = FVector::ForwardVector;
	}

	TargetTableLocation = TargetActorLocation;
	TargetLocation = TargetActorLocation - DirectionToTarget.GetSafeNormal() * FMath::Max(StopDistance, 0.0f);
	TargetLocation.Z = OwnerLocation.Z;
	CurrentTargetTableID = TEXT("");
	CurrentPathPoints.Reset();
	CurrentPathIndex = 0;

	if (bUseNavigationPath && !BuildNavigationPathToTarget(Owner))
	{
		PrintDebugMessage(TEXT("Nav path failed, using direct move"), FColor::Yellow);
	}

	bIsMovingToTable = true;
	CurrentMoveVelocity = FVector::ZeroVector;
	CurrentMoveSpeed = 0.0f;
	bIsRunning = false;
	RemainingMoveDistance = 0.0f;
	MoveSpeedDebugTimer = 0.0f;
	return true;
}

AActor* UNPCMoveToTableComponent::FindPlayerActor() const
{
	if (AActor* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		return PlayerPawn;
	}

	return UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
}

bool UNPCMoveToTableComponent::MoveNearPlayer(float StopDistance)
{
	CancelFollowingForExplicitCommand();
	SetMovementMode(ENPCMovementMode::SeekPlayer);

	CurrentMoveReason = TEXT("near_player");
	CurrentMoveTargetID = TEXT("player");

	AActor* PlayerActor = FindPlayerActor();
	if (!PlayerActor)
	{
		const FString Message = TEXT("Player actor not found");
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
		FailMoveToTable(Message);
		return false;
	}

	const float EffectiveStopDistance = StopDistance > KINDA_SMALL_NUMBER
		? StopDistance
		: PlayerApproachDistance;
	const bool bStartedMove = MoveNearActor(PlayerActor, EffectiveStopDistance);
	if (bStartedMove)
	{
		CurrentMoveReason = TEXT("near_player");
		CurrentMoveTargetID = TEXT("player");
		PrintDebugMessage(TEXT("Move near player"), FColor::Green);
	}

	return bStartedMove;
}

void UNPCMoveToTableComponent::StartFollowingPlayer(float DesiredDistance)
{
	if (bIsGuidingPlayer)
	{
		StopGuidingPlayer();
	}

	FollowDesiredDistance = DesiredDistance > KINDA_SMALL_NUMBER
		? DesiredDistance
		: FollowDesiredDistance;

	if (bIsFollowingPlayer)
	{
		SetMovementMode(ENPCMovementMode::FollowPlayer);
		PrintDebugMessage(TEXT("Already following player; updated desired distance"), FColor::Yellow);
		return;
	}

	FollowPlayerActor = FindPlayerActor();
	if (AActor* PlayerActor = FollowPlayerActor.Get())
	{
		LastFollowPlayerLocation = PlayerActor->GetActorLocation();
	}
	else
	{
		LastFollowPlayerLocation = FVector::ZeroVector;
	}
	LastFollowGoalLocation = FVector::ZeroVector;
	FollowUpdateTimer = FollowUpdateInterval;
	FollowRepathTimer = FollowMinRepathInterval;
	FollowDebugTimer = 0.0f;
	bFollowMoveInProgress = false;
	bIsFollowingPlayer = true;
	SetMovementMode(ENPCMovementMode::FollowPlayer);

	UE_LOG(LogTemp, Warning, TEXT("Start following player"));
	PrintDebugMessage(TEXT("Start following player"), FColor::Green);
}

void UNPCMoveToTableComponent::StopFollowingPlayer()
{
	const bool bWasFollowMode = MovementMode == ENPCMovementMode::FollowPlayer;
	bIsFollowingPlayer = false;
	bFollowMoveInProgress = false;
	FollowPlayerActor.Reset();
	FollowUpdateTimer = 0.0f;
	FollowRepathTimer = 0.0f;
	FollowDebugTimer = 0.0f;
	LastFollowPlayerLocation = FVector::ZeroVector;
	LastFollowGoalLocation = FVector::ZeroVector;

	const bool bCurrentMoveTargetsPlayer = CurrentMoveTargetID.Equals(TEXT("player"), ESearchCase::IgnoreCase)
		|| CurrentMoveReason == FName(TEXT("near_player"))
		|| CurrentMoveReason == FName(TEXT("player"))
		|| CurrentMoveReason == FName(TEXT("follow_player"));
	if (bIsMovingToTable && bCurrentMoveTargetsPlayer)
	{
		ClearMovementState();
	}
	else if (bWasFollowMode)
	{
		CurrentMoveSpeed = 0.0f;
		CurrentMoveVelocity = FVector::ZeroVector;
		SetOwnerComponentVelocity(GetOwner(), CurrentMoveVelocity);
	}

	if (bWasFollowMode || bCurrentMoveTargetsPlayer || !bIsMovingToTable)
	{
		SetMovementMode(ENPCMovementMode::Idle);
	}

	UE_LOG(LogTemp, Warning, TEXT("Stop following player"));
	PrintDebugMessage(TEXT("Stop following player"), FColor::Yellow);
}

bool UNPCMoveToTableComponent::IsFollowingPlayer() const
{
	return bIsFollowingPlayer;
}

FString UNPCMoveToTableComponent::ResolveGuideTableTarget(const FString& RequestedTable) const
{
	const FString TrimmedTable = RequestedTable.TrimStartAndEnd();
	UWorld* World = GetWorld();
	if (!World)
	{
		return IsFuzzyGuideTableTarget(TrimmedTable) ? TEXT("") : TrimmedTable;
	}

	for (TActorIterator<ATableMarkerActor> It(World); It; ++It)
	{
		const ATableMarkerActor* TableMarker = *It;
		if (!TableMarker)
		{
			continue;
		}

		const FString CandidateTableID = TableMarker->GetTableID();
		if (CandidateTableID == TrimmedTable || CandidateTableID.TrimStartAndEnd() == TrimmedTable)
		{
			return CandidateTableID;
		}
	}

	if (!IsFuzzyGuideTableTarget(TrimmedTable))
	{
		return TrimmedTable;
	}

	const AActor* DistanceReferenceActor = FindPlayerActor();
	if (!DistanceReferenceActor)
	{
		DistanceReferenceActor = GetOwner();
	}

	const bool bPreferNearest = ShouldPreferNearestGuideTable(TrimmedTable);
	ATableMarkerActor* BestTable = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (TActorIterator<ATableMarkerActor> It(World); It; ++It)
	{
		ATableMarkerActor* TableMarker = *It;
		if (!TableMarker || TableMarker->IsTableOccupied())
		{
			continue;
		}

		const float CandidateDistanceSq = DistanceReferenceActor
			? FVector::DistSquared(TableMarker->GetActorLocation(), DistanceReferenceActor->GetActorLocation())
			: 0.0f;

		bool bIsBetter = false;
		if (!BestTable)
		{
			bIsBetter = true;
		}
		else if (bPreferNearest)
		{
			bIsBetter = CandidateDistanceSq < BestDistanceSq - KINDA_SMALL_NUMBER
				|| (FMath::IsNearlyEqual(CandidateDistanceSq, BestDistanceSq, 1.0f)
					&& TableMarker->GetPriority() > BestTable->GetPriority());
		}
		else
		{
			bIsBetter = IsBetterSceneCandidate(
				TableMarker->GetPriority(),
				CandidateDistanceSq,
				BestTable != nullptr,
				BestTable->GetPriority(),
				BestDistanceSq);
		}

		if (bIsBetter)
		{
			BestTable = TableMarker;
			BestDistanceSq = CandidateDistanceSq;
		}
	}

	return BestTable ? BestTable->GetTableID() : TEXT("");
}

bool UNPCMoveToTableComponent::StartGuideToTable(const FString& TableID)
{
	const FString RequestedTableID = TableID.TrimStartAndEnd();
	UE_LOG(LogTemp, Warning, TEXT("[Guide] Requested table target: %s"), *RequestedTableID);

	const FString TrimmedTableID = ResolveGuideTableTarget(RequestedTableID);
	if (TrimmedTableID.IsEmpty())
	{
		const FString Message = FString::Printf(TEXT("[Guide] Failed to resolve table target: %s"), *RequestedTableID);
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
		PrintDebugMessage(Message, FColor::Red);
		return false;
	}
	UE_LOG(LogTemp, Warning, TEXT("[Guide] Resolved table target: %s"), *TrimmedTableID);

	CancelFollowingForExplicitCommand();
	SetMovementMode(ENPCMovementMode::GuidePlayer);
	bIsGuidingPlayer = true;
	bGuidePausedForPlayer = false;
	GuideTargetType = TEXT("table");
	GuideTargetID = TrimmedTableID;
	GuidePlayerActor = FindPlayerActor();
	GuideCheckTimer = GuideCheckInterval;
	GuidePlayerPhase = EGuidePlayerPhase::None;
	bGuideStartedEventBroadcasted = false;
	bGuideArrivedEventBroadcasted = false;

	const FString Message = FString::Printf(TEXT("Start guide to table: %s"), *TrimmedTableID);
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
	PrintDebugMessage(Message, FColor::Green);

	AActor* Owner = GetOwner();
	AActor* PlayerActor = GuidePlayerActor.Get();
	if (bGuideApproachPlayerFirst && Owner && PlayerActor)
	{
		const float DistanceToPlayer = FVector::Dist2D(Owner->GetActorLocation(), PlayerActor->GetActorLocation());
		const float EffectiveStartThreshold = GuideStartPlayerDistanceThreshold > KINDA_SMALL_NUMBER
			? GuideStartPlayerDistanceThreshold
			: 320.0f;
		if (DistanceToPlayer > EffectiveStartThreshold)
		{
			GuidePlayerPhase = EGuidePlayerPhase::ApproachingPlayer;
			const float EffectiveApproachDistance = GuideStartApproachPlayerDistance > KINDA_SMALL_NUMBER
				? GuideStartApproachPlayerDistance
				: 160.0f;
			const bool bStartedApproach = MoveNearActor(PlayerActor, EffectiveApproachDistance);
			if (bStartedApproach)
			{
				CurrentMoveReason = TEXT("guide_approach_player");
				CurrentMoveTargetID = TEXT("player");
				UE_LOG(LogTemp, Warning, TEXT("Guide start: approaching player first"));
				PrintDebugMessage(TEXT("Guide start: approaching player first"), FColor::Green);
			}
			else
			{
				StopGuidingPlayer();
			}
			return bStartedApproach;
		}
	}

	return StartGuideMovementToSavedTarget();
}

bool UNPCMoveToTableComponent::StartGuideToSceneTag(const FString& TargetTag)
{
	const FString TrimmedTargetTag = TargetTag.TrimStartAndEnd();
	if (TrimmedTargetTag.IsEmpty())
	{
		PrintDebugMessage(TEXT("Guide scene tag target is empty"), FColor::Red);
		return false;
	}

	CancelFollowingForExplicitCommand();
	SetMovementMode(ENPCMovementMode::GuidePlayer);
	bIsGuidingPlayer = true;
	bGuidePausedForPlayer = false;
	GuideTargetType = TEXT("scene_tag");
	GuideTargetID = TrimmedTargetTag;
	GuidePlayerActor = FindPlayerActor();
	GuideCheckTimer = GuideCheckInterval;
	GuidePlayerPhase = EGuidePlayerPhase::None;
	bGuideStartedEventBroadcasted = false;
	bGuideArrivedEventBroadcasted = false;

	const FString Message = FString::Printf(TEXT("Start guide to scene tag: %s"), *TrimmedTargetTag);
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
	PrintDebugMessage(Message, FColor::Green);

	AActor* Owner = GetOwner();
	AActor* PlayerActor = GuidePlayerActor.Get();
	if (bGuideApproachPlayerFirst && Owner && PlayerActor)
	{
		const float DistanceToPlayer = FVector::Dist2D(Owner->GetActorLocation(), PlayerActor->GetActorLocation());
		const float EffectiveStartThreshold = GuideStartPlayerDistanceThreshold > KINDA_SMALL_NUMBER
			? GuideStartPlayerDistanceThreshold
			: 320.0f;
		if (DistanceToPlayer > EffectiveStartThreshold)
		{
			GuidePlayerPhase = EGuidePlayerPhase::ApproachingPlayer;
			const float EffectiveApproachDistance = GuideStartApproachPlayerDistance > KINDA_SMALL_NUMBER
				? GuideStartApproachPlayerDistance
				: 160.0f;
			const bool bStartedApproach = MoveNearActor(PlayerActor, EffectiveApproachDistance);
			if (bStartedApproach)
			{
				CurrentMoveReason = TEXT("guide_approach_player");
				CurrentMoveTargetID = TEXT("player");
				UE_LOG(LogTemp, Warning, TEXT("Guide start: approaching player first"));
				PrintDebugMessage(TEXT("Guide start: approaching player first"), FColor::Green);
			}
			else
			{
				StopGuidingPlayer();
			}
			return bStartedApproach;
		}
	}

	return StartGuideMovementToSavedTarget();
}

void UNPCMoveToTableComponent::StopGuidingPlayer()
{
	const bool bWasGuiding = bIsGuidingPlayer;
	const FString StoppedTargetID = GuideTargetID;

	if (bWasGuiding)
	{
		OnGuideStopped.Broadcast(TEXT("guide_stopped"), StoppedTargetID, GuideStoppedLine);
		UE_LOG(LogTemp, Warning, TEXT("Broadcast OnGuideStopped: %s"), *StoppedTargetID);
	}

	bIsGuidingPlayer = false;
	bGuidePausedForPlayer = false;
	GuidePlayerPhase = EGuidePlayerPhase::None;
	GuidePlayerActor.Reset();
	GuideCheckTimer = 0.0f;
	GuideTargetType = TEXT("");
	GuideTargetID = TEXT("");
	bPreserveMovementModeForGuideMove = false;
	bGuideStartedEventBroadcasted = false;
	bGuideArrivedEventBroadcasted = false;

	if (bIsMovingToTable || bWasGuiding)
	{
		ClearMovementState();
	}

	if (MovementMode == ENPCMovementMode::GuidePlayer)
	{
		SetMovementMode(ENPCMovementMode::Idle);
	}

	UE_LOG(LogTemp, Warning, TEXT("Stop guiding player"));
	PrintDebugMessage(TEXT("Stop guiding player"), FColor::Yellow);
}

bool UNPCMoveToTableComponent::IsGuidingPlayer() const
{
	return bIsGuidingPlayer;
}

bool UNPCMoveToTableComponent::MoveToFurnitureByID(const FString& FurnitureID)
{
	CancelFollowingForExplicitCommand();

	CurrentMoveReason = TEXT("Furniture");
	CurrentMoveTargetID = FurnitureID;

	UWorld* World = GetWorld();
	if (!World)
	{
		FailMoveToTable(TEXT("NPC movement world missing"));
		return false;
	}

	for (TActorIterator<AFurnitureMarkerActor> It(World); It; ++It)
	{
		AFurnitureMarkerActor* FurnitureMarker = *It;
		if (FurnitureMarker && FurnitureMarker->FurnitureID == FurnitureID)
		{
			return StartMovingToFurniture(FurnitureMarker);
		}
	}

	FailMoveToTable(FString::Printf(TEXT("Furniture not found: %s"), *FurnitureID));
	return false;
}

bool UNPCMoveToTableComponent::MoveToFurnitureByTag(const FString& FurnitureTag)
{
	CancelFollowingForExplicitCommand();

	CurrentMoveReason = TEXT("Furniture");
	CurrentMoveTargetID = FurnitureTag;

	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		FailMoveToTable(TEXT("NPC movement world or owner missing"));
		return false;
	}

	AFurnitureMarkerActor* BestFurniture = nullptr;
	FVector BestMoveLocation = FVector::ZeroVector;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (TActorIterator<AFurnitureMarkerActor> It(World); It; ++It)
	{
		AFurnitureMarkerActor* FurnitureMarker = *It;
		if (!FurnitureMarker || !DoesTagMatchStringArray(FurnitureTag, FurnitureMarker->FurnitureTags))
		{
			continue;
		}

		FVector CandidateMoveLocation = FVector::ZeroVector;
		if (!ResolveFurnitureMoveLocation(FurnitureMarker, CandidateMoveLocation))
		{
			continue;
		}

		const float CandidateDistanceSq = GetDistanceSquaredToLocation(CandidateMoveLocation, Owner);
		if (IsBetterSceneCandidate(FurnitureMarker->Priority, CandidateDistanceSq, BestFurniture != nullptr, BestFurniture ? BestFurniture->Priority : 0, BestDistanceSq))
		{
			BestFurniture = FurnitureMarker;
			BestMoveLocation = CandidateMoveLocation;
			BestDistanceSq = CandidateDistanceSq;
		}
	}

	if (BestFurniture)
	{
		return StartMovingToFurnitureLocation(BestFurniture, BestMoveLocation);
	}

	FailMoveToTable(FString::Printf(TEXT("Furniture tag not found: %s"), *FurnitureTag));
	return false;
}

bool UNPCMoveToTableComponent::MoveToFurnitureByType(const FString& FurnitureType)
{
	CancelFollowingForExplicitCommand();

	CurrentMoveReason = TEXT("Furniture");
	CurrentMoveTargetID = FurnitureType;

	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		FailMoveToTable(TEXT("NPC movement world or owner missing"));
		return false;
	}

	const FString TrimmedType = FurnitureType.TrimStartAndEnd();
	AFurnitureMarkerActor* BestFurniture = nullptr;
	FVector BestMoveLocation = FVector::ZeroVector;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (TActorIterator<AFurnitureMarkerActor> It(World); It; ++It)
	{
		AFurnitureMarkerActor* FurnitureMarker = *It;
		if (!FurnitureMarker || !FurnitureMarker->FurnitureType.TrimStartAndEnd().Equals(TrimmedType, ESearchCase::IgnoreCase))
		{
			continue;
		}

		FVector CandidateMoveLocation = FVector::ZeroVector;
		if (!ResolveFurnitureMoveLocation(FurnitureMarker, CandidateMoveLocation))
		{
			continue;
		}

		const float CandidateDistanceSq = GetDistanceSquaredToLocation(CandidateMoveLocation, Owner);
		if (IsBetterSceneCandidate(FurnitureMarker->Priority, CandidateDistanceSq, BestFurniture != nullptr, BestFurniture ? BestFurniture->Priority : 0, BestDistanceSq))
		{
			BestFurniture = FurnitureMarker;
			BestMoveLocation = CandidateMoveLocation;
			BestDistanceSq = CandidateDistanceSq;
		}
	}

	if (BestFurniture)
	{
		return StartMovingToFurnitureLocation(BestFurniture, BestMoveLocation);
	}

	FailMoveToTable(FString::Printf(TEXT("Furniture type not found: %s"), *FurnitureType));
	return false;
}

bool UNPCMoveToTableComponent::MoveToTargetPointByID(const FString& TargetID)
{
	CancelFollowingForExplicitCommand();

	CurrentMoveReason = TEXT("TargetPoint");
	CurrentMoveTargetID = TargetID;

	UWorld* World = GetWorld();
	if (!World)
	{
		FailMoveToTable(TEXT("NPC movement world missing"));
		return false;
	}

	for (TActorIterator<ACafeTargetPointActor> It(World); It; ++It)
	{
		ACafeTargetPointActor* TargetPoint = *It;
		if (TargetPoint && TargetPoint->bIsAvailable && TargetPoint->TargetID == TargetID)
		{
			return StartMovingToTargetPoint(TargetPoint);
		}
	}

	FailMoveToTable(FString::Printf(TEXT("Target point not found: %s"), *TargetID));
	return false;
}

bool UNPCMoveToTableComponent::MoveToTargetPointByTag(const FString& TargetTag)
{
	CancelFollowingForExplicitCommand();

	CurrentMoveReason = TEXT("TargetPoint");
	CurrentMoveTargetID = TargetTag;

	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		FailMoveToTable(TEXT("NPC movement world or owner missing"));
		return false;
	}

	ACafeTargetPointActor* BestTargetPoint = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (TActorIterator<ACafeTargetPointActor> It(World); It; ++It)
	{
		ACafeTargetPointActor* TargetPoint = *It;
		if (!TargetPoint || !TargetPoint->bIsAvailable || !TargetPoint->HasTagString(TargetTag))
		{
			continue;
		}

		const float CandidateDistanceSq = GetDistanceSquaredToLocation(TargetPoint->GetTargetTransform().GetLocation(), Owner);
		if (IsBetterSceneCandidate(TargetPoint->Priority, CandidateDistanceSq, BestTargetPoint != nullptr, BestTargetPoint ? BestTargetPoint->Priority : 0, BestDistanceSq))
		{
			BestTargetPoint = TargetPoint;
			BestDistanceSq = CandidateDistanceSq;
		}
	}

	if (BestTargetPoint)
	{
		return StartMovingToTargetPoint(BestTargetPoint);
	}

	FailMoveToTable(FString::Printf(TEXT("Target point tag not found: %s"), *TargetTag));
	return false;
}

bool UNPCMoveToTableComponent::MoveToSceneTargetByID(const FString& TargetID)
{
	CancelFollowingForExplicitCommand();

	CurrentMoveReason = TEXT("SceneTarget");
	CurrentMoveTargetID = TargetID;

	UWorld* World = GetWorld();
	if (!World)
	{
		FailMoveToTable(TEXT("NPC movement world missing"));
		return false;
	}

	for (TActorIterator<ATableMarkerActor> It(World); It; ++It)
	{
		ATableMarkerActor* TableMarker = *It;
		if (TableMarker && TableMarker->GetTableID() == TargetID)
		{
			return StartMovingToTable(TableMarker, true);
		}
	}

	for (TActorIterator<AFurnitureMarkerActor> It(World); It; ++It)
	{
		AFurnitureMarkerActor* FurnitureMarker = *It;
		if (FurnitureMarker && FurnitureMarker->FurnitureID == TargetID)
		{
			return StartMovingToFurniture(FurnitureMarker);
		}
	}

	for (TActorIterator<ACafeTargetPointActor> It(World); It; ++It)
	{
		ACafeTargetPointActor* TargetPoint = *It;
		if (TargetPoint && TargetPoint->bIsAvailable && TargetPoint->TargetID == TargetID)
		{
			return StartMovingToTargetPoint(TargetPoint);
		}
	}

	FailMoveToTable(FString::Printf(TEXT("Scene target not found: %s"), *TargetID));
	return false;
}

bool UNPCMoveToTableComponent::MoveToSceneTargetByTag(const FString& TargetTag)
{
	CancelFollowingForExplicitCommand();

	CurrentMoveReason = TEXT("SceneTarget");
	CurrentMoveTargetID = TargetTag;

	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		FailMoveToTable(TEXT("NPC movement world or owner missing"));
		return false;
	}

	bool bHasBestTarget = false;
	int32 BestPriority = 0;
	float BestDistanceSq = TNumericLimits<float>::Max();
	FName BestMoveReason = NAME_None;
	FString BestTargetID;
	FVector BestMoveLocation = FVector::ZeroVector;
	FVector BestFaceLocation = FVector::ZeroVector;
	FString BestDebugMessage;

	auto ConsiderTarget = [&](int32 CandidatePriority, const FVector& CandidateDistanceLocation, const FVector& CandidateMoveLocation, const FVector& CandidateFaceLocation, const FName& CandidateMoveReason, const FString& CandidateTargetID, const FString& CandidateDebugMessage)
	{
		const float CandidateDistanceSq = GetDistanceSquaredToLocation(CandidateDistanceLocation, Owner);
		if (IsBetterSceneCandidate(CandidatePriority, CandidateDistanceSq, bHasBestTarget, BestPriority, BestDistanceSq))
		{
			bHasBestTarget = true;
			BestPriority = CandidatePriority;
			BestDistanceSq = CandidateDistanceSq;
			BestMoveReason = CandidateMoveReason;
			BestTargetID = CandidateTargetID;
			BestMoveLocation = CandidateMoveLocation;
			BestFaceLocation = CandidateFaceLocation;
			BestDebugMessage = CandidateDebugMessage;
		}
	};

	for (TActorIterator<ATableMarkerActor> It(World); It; ++It)
	{
		ATableMarkerActor* TableMarker = *It;
		if (!TableMarker || !DoesTagMatchStringArray(TargetTag, TableMarker->GetTableTags()))
		{
			continue;
		}

		const FVector TableLocation = TableMarker->GetActorLocation();
		const FString TableID = TableMarker->GetTableID();
		ConsiderTarget(
			TableMarker->GetPriority(),
			TableLocation,
			TableLocation - TableMarker->GetActorForwardVector() * StopDistanceFromTable,
			TableLocation,
			TEXT("Table"),
			TableID,
			FString::Printf(TEXT("Move to table: %s"), *TableID));
	}

	for (TActorIterator<AFurnitureMarkerActor> It(World); It; ++It)
	{
		AFurnitureMarkerActor* FurnitureMarker = *It;
		if (!FurnitureMarker || !DoesTagMatchStringArray(TargetTag, FurnitureMarker->FurnitureTags))
		{
			continue;
		}

		const FString FurnitureID = FurnitureMarker->FurnitureID;
		FVector FurnitureMoveLocation = FVector::ZeroVector;
		if (!ResolveFurnitureMoveLocation(FurnitureMarker, FurnitureMoveLocation))
		{
			continue;
		}

		ConsiderTarget(
			FurnitureMarker->Priority,
			FurnitureMoveLocation,
			FurnitureMoveLocation,
			FurnitureMarker->GetActorLocation(),
			TEXT("Furniture"),
			FurnitureID,
			FString::Printf(TEXT("Move to furniture: %s"), *FurnitureID));
	}

	for (TActorIterator<ACafeTargetPointActor> It(World); It; ++It)
	{
		ACafeTargetPointActor* TargetPoint = *It;
		if (!TargetPoint || !TargetPoint->bIsAvailable || !TargetPoint->HasTagString(TargetTag))
		{
			continue;
		}

		const FString PointID = TargetPoint->TargetID;
		const FVector PointLocation = TargetPoint->GetTargetTransform().GetLocation();
		ConsiderTarget(
			TargetPoint->Priority,
			PointLocation,
			PointLocation,
			PointLocation,
			TEXT("TargetPoint"),
			PointID,
			FString::Printf(TEXT("Move to target point: %s"), *PointID));
	}

	if (bHasBestTarget)
	{
		const FString SceneTagDebugMessage = TargetTag.Equals(TEXT("counter"), ESearchCase::IgnoreCase)
			? FString::Printf(TEXT("Move to scene target by tag: %s"), *TargetTag)
			: BestDebugMessage;
		return StartMovingToLocation(BestMoveLocation, BestFaceLocation, BestMoveReason, BestTargetID, SceneTagDebugMessage, true);
	}

	FailMoveToTable(FString::Printf(TEXT("Scene target tag not found: %s"), *TargetTag));
	return false;
}

void UNPCMoveToTableComponent::StopMovingToTable()
{
	if (bIsMovingToTable)
	{
		FailMoveToTable(TEXT("NPC movement stopped"));
		return;
	}

	ClearMovementState();
}

void UNPCMoveToTableComponent::SetMovementMode(ENPCMovementMode NewMode)
{
	if (MovementMode == NewMode)
	{
		return;
	}

	MovementMode = NewMode;
	bPrintedFollowModeSkipWarning = false;
	UE_LOG(LogTemp, Warning, TEXT("MovementMode changed: %s"), MovementModeToString(MovementMode));
}

ENPCMovementMode UNPCMoveToTableComponent::GetMovementMode() const
{
	return MovementMode;
}

void UNPCMoveToTableComponent::CancelFollowingForExplicitCommand()
{
	if (bIsGuidingPlayer && !bPreserveMovementModeForGuideMove)
	{
		StopGuidingPlayer();
	}

	if (!bExplicitCommandCancelsFollow)
	{
		if (!bPreserveMovementModeForGuideMove)
		{
			SetMovementMode(ENPCMovementMode::CommandedMove);
		}
		return;
	}

	if (!bIsFollowingPlayer)
	{
		if (!bPreserveMovementModeForGuideMove)
		{
			SetMovementMode(ENPCMovementMode::CommandedMove);
		}
		return;
	}

	bIsFollowingPlayer = false;
	bFollowMoveInProgress = false;
	FollowPlayerActor.Reset();
	FollowUpdateTimer = 0.0f;
	FollowRepathTimer = 0.0f;
	FollowDebugTimer = 0.0f;
	LastFollowPlayerLocation = FVector::ZeroVector;
	LastFollowGoalLocation = FVector::ZeroVector;

	if (!bPreserveMovementModeForGuideMove)
	{
		SetMovementMode(ENPCMovementMode::CommandedMove);
	}
	UE_LOG(LogTemp, Warning, TEXT("Follow cancelled by explicit command"));
}

bool UNPCMoveToTableComponent::StartMovingToTable(const ATableMarkerActor* TableMarker, bool bRequireNavigationPath)
{
	if (!TableMarker)
	{
		FailMoveToTable(TEXT("Table marker missing"));
		return false;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		FailMoveToTable(TEXT("NPC movement owner missing"));
		return false;
	}

	const FString TableID = TableMarker->GetTableID();
	const FVector TableLocation = TableMarker->GetActorLocation();
	const FVector MoveLocation = TableLocation - TableMarker->GetActorForwardVector() * StopDistanceFromTable;

	return StartMovingToLocation(
		MoveLocation,
		TableLocation,
		TEXT("Table"),
		TableID,
		FString::Printf(TEXT("Move to table: %s"), *TableID),
		bRequireNavigationPath);
}

bool UNPCMoveToTableComponent::StartMovingToFurniture(AFurnitureMarkerActor* FurnitureMarker)
{
	if (!FurnitureMarker)
	{
		FailMoveToTable(TEXT("Furniture marker missing"));
		return false;
	}

	FVector FurnitureMoveLocation = FVector::ZeroVector;
	if (!ResolveFurnitureMoveLocation(FurnitureMarker, FurnitureMoveLocation))
	{
		FailMoveToTable(FString::Printf(TEXT("Scene target path failed: %s"), *FurnitureMarker->FurnitureID));
		return false;
	}

	return StartMovingToFurnitureLocation(FurnitureMarker, FurnitureMoveLocation);
}

bool UNPCMoveToTableComponent::StartMovingToFurnitureLocation(AFurnitureMarkerActor* FurnitureMarker, const FVector& MoveLocation)
{
	if (!FurnitureMarker)
	{
		FailMoveToTable(TEXT("Furniture marker missing"));
		return false;
	}

	return StartMovingToLocation(
		MoveLocation,
		FurnitureMarker->GetActorLocation(),
		TEXT("Furniture"),
		FurnitureMarker->FurnitureID,
		FString::Printf(TEXT("Move to furniture: %s"), *FurnitureMarker->FurnitureID),
		true);
}

bool UNPCMoveToTableComponent::StartMovingToTargetPoint(const ACafeTargetPointActor* TargetPoint)
{
	if (!TargetPoint)
	{
		FailMoveToTable(TEXT("Target point missing"));
		return false;
	}

	const FVector TargetPointLocation = TargetPoint->GetTargetTransform().GetLocation();
	return StartMovingToLocation(
		TargetPointLocation,
		TargetPointLocation,
		TEXT("TargetPoint"),
		TargetPoint->TargetID,
		FString::Printf(TEXT("Move to target point: %s"), *TargetPoint->TargetID),
		true);
}

bool UNPCMoveToTableComponent::StartMovingToLocation(const FVector& MoveLocation, const FVector& FaceLocation, const FName& MoveReason, const FString& TargetID, const FString& DebugMessage, bool bRequireNavigationPath)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		FailMoveToTable(TEXT("NPC movement owner missing"));
		return false;
	}

	TargetTableLocation = FaceLocation;
	TargetLocation = MoveLocation;
	CurrentTargetTableID = MoveReason == FName(TEXT("Table")) ? TargetID : TEXT("");
	CurrentMoveReason = MoveReason;
	CurrentMoveTargetID = TargetID;
	CurrentPathPoints.Reset();
	CurrentPathIndex = 0;

	if (bUseNavigationPath && !BuildNavigationPathToTarget(Owner))
	{
		if (bRequireNavigationPath)
		{
			FailMoveToTable(FString::Printf(TEXT("Scene target path failed: %s"), *TargetID));
			return false;
		}

		PrintDebugMessage(TEXT("Nav path failed, using direct move"), FColor::Yellow);
	}

	bIsMovingToTable = true;
	CurrentMoveVelocity = FVector::ZeroVector;
	CurrentMoveSpeed = 0.0f;
	bIsRunning = false;
	RemainingMoveDistance = 0.0f;
	MoveSpeedDebugTimer = 0.0f;
	PrintDebugMessage(DebugMessage, FColor::Green);
	return true;
}

bool UNPCMoveToTableComponent::BuildNavigationPathToTarget(AActor* Owner)
{
	CurrentPathPoints.Reset();
	CurrentPathIndex = 0;

	if (!Owner)
	{
		return false;
	}

	FVector ProjectedTargetLocation = TargetLocation;
	if (ProjectLocationToNavigation(Owner, TargetLocation, ProjectedTargetLocation))
	{
		TargetLocation = ProjectedTargetLocation;
	}

	TArray<FVector> PathPoints;
	if (!FindNavigationPathToLocation(Owner, TargetLocation, &PathPoints, nullptr))
	{
		return false;
	}

	CurrentPathPoints = PathPoints;
	CurrentPathIndex = 1;
	return CurrentPathPoints.IsValidIndex(CurrentPathIndex);
}

bool UNPCMoveToTableComponent::ProjectTargetLocationToNavigation(AActor* Owner)
{
	FVector ProjectedLocation = TargetLocation;
	if (ProjectLocationToNavigation(Owner, TargetLocation, ProjectedLocation))
	{
		TargetLocation = ProjectedLocation;
		return true;
	}

	return false;
}

bool UNPCMoveToTableComponent::ProjectLocationToNavigation(AActor* Owner, const FVector& InLocation, FVector& OutLocation) const
{
	if (!Owner)
	{
		return false;
	}

	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		return false;
	}

	UClass* NavigationSystemClass = LoadObject<UClass>(nullptr, TEXT("/Script/NavigationSystem.NavigationSystemV1"));
	if (!NavigationSystemClass)
	{
		return false;
	}

	UFunction* ProjectFunction = NavigationSystemClass->FindFunctionByName(TEXT("K2_ProjectPointToNavigation"));
	UObject* NavigationSystemDefaultObject = NavigationSystemClass->GetDefaultObject();
	if (!ProjectFunction || !NavigationSystemDefaultObject)
	{
		return false;
	}

	struct FProjectPointToNavigationParams
	{
		UObject* WorldContextObject = nullptr;
		FVector Point = FVector::ZeroVector;
		FVector ProjectedLocation = FVector::ZeroVector;
		UObject* NavData = nullptr;
		UClass* FilterClass = nullptr;
		FVector QueryExtent = FVector(200.0f, 200.0f, 400.0f);
		bool ReturnValue = false;
	};

	FProjectPointToNavigationParams ProjectParams;
	ProjectParams.WorldContextObject = World;
	ProjectParams.Point = InLocation;
	ProjectParams.ProjectedLocation = InLocation;
	NavigationSystemDefaultObject->ProcessEvent(ProjectFunction, &ProjectParams);

	if (ProjectParams.ReturnValue)
	{
		OutLocation = ProjectParams.ProjectedLocation;
		return true;
	}

	return false;
}

bool UNPCMoveToTableComponent::FindNavigationPathToLocation(AActor* Owner, const FVector& InTargetLocation, TArray<FVector>* OutPathPoints, float* OutPathLength) const
{
	if (OutPathPoints)
	{
		OutPathPoints->Reset();
	}
	if (OutPathLength)
	{
		*OutPathLength = 0.0f;
	}

	if (!Owner)
	{
		return false;
	}

	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		return false;
	}

	UClass* NavigationSystemClass = LoadObject<UClass>(nullptr, TEXT("/Script/NavigationSystem.NavigationSystemV1"));
	if (!NavigationSystemClass)
	{
		return false;
	}

	UFunction* FindPathFunction = NavigationSystemClass->FindFunctionByName(TEXT("FindPathToLocationSynchronously"));
	UObject* NavigationSystemDefaultObject = NavigationSystemClass->GetDefaultObject();
	if (!FindPathFunction || !NavigationSystemDefaultObject)
	{
		return false;
	}

	struct FFindPathToLocationSynchronouslyParams
	{
		UObject* WorldContextObject = nullptr;
		FVector PathStart = FVector::ZeroVector;
		FVector PathEnd = FVector::ZeroVector;
		AActor* PathfindingContext = nullptr;
		UClass* FilterClass = nullptr;
		UObject* ReturnValue = nullptr;
	};

	FFindPathToLocationSynchronouslyParams FindPathParams;
	FindPathParams.WorldContextObject = World;
	FindPathParams.PathStart = Owner->GetActorLocation();
	FindPathParams.PathEnd = InTargetLocation;
	FindPathParams.PathfindingContext = Owner;
	NavigationSystemDefaultObject->ProcessEvent(FindPathFunction, &FindPathParams);

	UObject* NavigationPath = FindPathParams.ReturnValue;
	if (!NavigationPath)
	{
		return false;
	}

	UFunction* IsValidFunction = NavigationPath->FindFunction(TEXT("IsValid"));
	if (!IsValidFunction)
	{
		return false;
	}

	struct FNavigationPathIsValidParams
	{
		bool ReturnValue = false;
	};

	FNavigationPathIsValidParams IsValidParams;
	NavigationPath->ProcessEvent(IsValidFunction, &IsValidParams);
	if (!IsValidParams.ReturnValue)
	{
		return false;
	}

	const FArrayProperty* PathPointsProperty = FindFProperty<FArrayProperty>(NavigationPath->GetClass(), TEXT("PathPoints"));
	if (!PathPointsProperty)
	{
		return false;
	}

	const TArray<FVector>* PathPoints = PathPointsProperty->ContainerPtrToValuePtr<TArray<FVector>>(NavigationPath);
	if (!PathPoints || PathPoints->Num() <= 1)
	{
		return false;
	}

	if (OutPathPoints)
	{
		*OutPathPoints = *PathPoints;
	}

	if (OutPathLength)
	{
		float PathLength = 0.0f;
		for (int32 Index = 1; Index < PathPoints->Num(); ++Index)
		{
			PathLength += FVector::Dist((*PathPoints)[Index - 1], (*PathPoints)[Index]);
		}
		*OutPathLength = PathLength;
	}

	return true;
}

bool UNPCMoveToTableComponent::ResolveFurnitureMoveLocation(AFurnitureMarkerActor* FurnitureMarker, FVector& OutMoveLocation)
{
	if (!FurnitureMarker)
	{
		return false;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	if (bUseAutomaticApproachLocation && ResolveApproachLocationForActor(FurnitureMarker, FurnitureMarker->FurnitureType, OutMoveLocation))
	{
		return true;
	}

	auto TryReachableApproachLocation = [&](const FVector& CandidateLocation, FVector& OutReachableLocation) -> bool
	{
		FVector ProjectedLocation = CandidateLocation;
		if (!ProjectLocationToNavigation(Owner, CandidateLocation, ProjectedLocation))
		{
			return false;
		}

		if (!IsApproachLocationFree(ProjectedLocation, FurnitureMarker))
		{
			return false;
		}

		if (!FindNavigationPathToLocation(Owner, ProjectedLocation, nullptr, nullptr))
		{
			return false;
		}

		OutReachableLocation = ProjectedLocation;
		return true;
	};

	if (TryReachableApproachLocation(FurnitureMarker->GetNPCStandTransform().GetLocation(), OutMoveLocation))
	{
		return true;
	}

	FVector TargetCenter = FurnitureMarker->GetActorLocation();
	FVector TargetExtent = FVector::ZeroVector;
	FurnitureMarker->GetActorBounds(false, TargetCenter, TargetExtent);
	if (TargetExtent.IsNearlyZero())
	{
		const FBox ComponentsBounds = FurnitureMarker->GetComponentsBoundingBox(true);
		if (ComponentsBounds.IsValid)
		{
			TargetCenter = ComponentsBounds.GetCenter();
			TargetExtent = ComponentsBounds.GetExtent();
		}
	}

	float DesiredDistance = DefaultApproachDistance;
	const FString NormalizedTargetType = FurnitureMarker->FurnitureType.TrimStartAndEnd();
	if (NormalizedTargetType.Equals(TEXT("chair"), ESearchCase::IgnoreCase))
	{
		DesiredDistance = 80.0f;
	}
	else if (NormalizedTargetType.Equals(TEXT("counter"), ESearchCase::IgnoreCase))
	{
		DesiredDistance = 130.0f;
	}
	else if (NormalizedTargetType.Equals(TEXT("sofa"), ESearchCase::IgnoreCase))
	{
		DesiredDistance = 110.0f;
	}
	else if (NormalizedTargetType.Equals(TEXT("table"), ESearchCase::IgnoreCase))
	{
		DesiredDistance = 120.0f;
	}

	const float TargetRadius = FMath::Max(TargetExtent.X, TargetExtent.Y);
	const float FallbackRadius = TargetRadius + DesiredDistance + static_cast<float>(FMath::Max(ApproachRingCount, 1)) * ApproachDistanceStep + ApproachDistanceStep;
	const int32 SafeSampleCount = FMath::Max(ApproachSampleCount, 8);
	float BestFallbackDistanceSq = TNumericLimits<float>::Max();
	bool bHasFallbackLocation = false;

	for (int32 SampleIndex = 0; SampleIndex < SafeSampleCount; ++SampleIndex)
	{
		const float Angle = 2.0f * PI * static_cast<float>(SampleIndex) / static_cast<float>(SafeSampleCount);
		const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
		FVector CandidateMoveLocation = FVector::ZeroVector;
		if (!TryReachableApproachLocation(TargetCenter + Direction * FallbackRadius, CandidateMoveLocation))
		{
			continue;
		}

		const float CandidateDistanceSq = FVector::DistSquared(CandidateMoveLocation, Owner->GetActorLocation());
		if (!bHasFallbackLocation || CandidateDistanceSq < BestFallbackDistanceSq)
		{
			bHasFallbackLocation = true;
			BestFallbackDistanceSq = CandidateDistanceSq;
			OutMoveLocation = CandidateMoveLocation;
		}
	}

	if (bHasFallbackLocation)
	{
		return true;
	}

	const FString FailureMessage = FString::Printf(TEXT("Auto approach failed and fallback failed: %s"), *FurnitureMarker->FurnitureID);
	UE_LOG(LogTemp, Warning, TEXT("%s"), *FailureMessage);
	PrintDebugMessage(FailureMessage, FColor::Red);
	return false;
}

bool UNPCMoveToTableComponent::ResolveApproachLocationForActor(AActor* TargetActor, const FString& TargetType, FVector& OutApproachLocation)
{
	AActor* Owner = GetOwner();
	if (!Owner || !IsValid(TargetActor))
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FString NormalizedTargetType = TargetType.TrimStartAndEnd();
	float DesiredDistance = DefaultApproachDistance;
	if (NormalizedTargetType.Equals(TEXT("chair"), ESearchCase::IgnoreCase))
	{
		DesiredDistance = 80.0f;
	}
	else if (NormalizedTargetType.Equals(TEXT("counter"), ESearchCase::IgnoreCase))
	{
		DesiredDistance = 130.0f;
	}
	else if (NormalizedTargetType.Equals(TEXT("sofa"), ESearchCase::IgnoreCase))
	{
		DesiredDistance = 110.0f;
	}
	else if (NormalizedTargetType.Equals(TEXT("table"), ESearchCase::IgnoreCase))
	{
		DesiredDistance = 120.0f;
	}

	FVector TargetCenter = TargetActor->GetActorLocation();
	FVector TargetExtent = FVector::ZeroVector;
	TargetActor->GetActorBounds(false, TargetCenter, TargetExtent);
	if (TargetExtent.IsNearlyZero())
	{
		const FBox ComponentsBounds = TargetActor->GetComponentsBoundingBox(true);
		if (ComponentsBounds.IsValid)
		{
			TargetCenter = ComponentsBounds.GetCenter();
			TargetExtent = ComponentsBounds.GetExtent();
		}
	}

	const float TargetRadius = FMath::Max(TargetExtent.X, TargetExtent.Y);
	FVector TargetForward = TargetActor->GetActorForwardVector();
	TargetForward.Z = 0.0f;
	if (TargetForward.IsNearlyZero())
	{
		TargetForward = FVector::ForwardVector;
	}
	TargetForward.Normalize();

	const FVector OwnerLocation = Owner->GetActorLocation();
	TArray<FNPCApproachCandidate> Candidates;
	FNPCApproachRejectStats RejectStats;
	const int32 SafeRingCount = FMath::Max(ApproachRingCount, 1);
	const int32 SafeSampleCount = FMath::Max(ApproachSampleCount, 4);

	for (int32 RingIndex = 0; RingIndex < SafeRingCount; ++RingIndex)
	{
		const float Radius = TargetRadius + DesiredDistance + static_cast<float>(RingIndex) * ApproachDistanceStep;
		for (int32 SampleIndex = 0; SampleIndex < SafeSampleCount; ++SampleIndex)
		{
			const float Angle = 2.0f * PI * static_cast<float>(SampleIndex) / static_cast<float>(SafeSampleCount);
			const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			const FVector RawCandidateLocation = TargetCenter + Direction * Radius;

			FVector ProjectedCandidateLocation = RawCandidateLocation;
			bool bCandidateAccepted = false;
			if (!ProjectLocationToNavigation(Owner, RawCandidateLocation, ProjectedCandidateLocation))
			{
				++RejectStats.NavProjectFailed;
			}
			else
			{
				const bool bInsideTargetBoundsXY =
					FMath::Abs(ProjectedCandidateLocation.X - TargetCenter.X) <= TargetExtent.X
					&& FMath::Abs(ProjectedCandidateLocation.Y - TargetCenter.Y) <= TargetExtent.Y;
				const float DistanceToTargetCenter = FVector::Dist2D(ProjectedCandidateLocation, TargetCenter);
				const float MinApproachDistance = FMath::Max(ApproachCollisionRadius, DesiredDistance * 0.45f);

				if (bInsideTargetBoundsXY)
				{
					++RejectStats.InsideBounds;
				}
				else if (DistanceToTargetCenter < MinApproachDistance)
				{
					++RejectStats.TooClose;
				}
				else if (!IsApproachLocationFree(ProjectedCandidateLocation, TargetActor))
				{
					++RejectStats.BlockedByOverlap;
				}
				else
				{
					float PathLength = 0.0f;
					if (!FindNavigationPathToLocation(Owner, ProjectedCandidateLocation, nullptr, &PathLength))
					{
						++RejectStats.PathFailed;
					}
					else
					{
						FCollisionQueryParams LineTraceParams(SCENE_QUERY_STAT(NPCApproachLineOfSight), false);
						LineTraceParams.AddIgnoredActor(Owner);
						LineTraceParams.AddIgnoredActor(TargetActor);
						FHitResult LineTraceHit;
						const bool bBlockingLineTrace = World->LineTraceSingleByChannel(LineTraceHit, ProjectedCandidateLocation, TargetCenter, ECC_Visibility, LineTraceParams);
						const bool bHasLineOfSight = !bBlockingLineTrace || LineTraceHit.GetActor() == TargetActor;

						FNPCApproachCandidate Candidate;
						Candidate.Location = ProjectedCandidateLocation;
						Candidate.PathLength = PathLength;
						Candidate.bHasLineOfSight = bHasLineOfSight;
						Candidate.Score = ScoreApproachCandidate(ProjectedCandidateLocation, OwnerLocation, TargetCenter, TargetForward, DesiredDistance, bHasLineOfSight, PathLength);
						Candidates.Add(Candidate);
						bCandidateAccepted = true;
						++RejectStats.Valid;
					}
				}
			}

			if (bDrawDebugApproachCandidates)
			{
				DrawDebugSphere(World, ProjectedCandidateLocation, 18.0f, 8, bCandidateAccepted ? FColor::Green : FColor::Red, false, 5.0f);
			}
		}
	}

	if (bPrintApproachCandidateRejectReasons)
	{
		const FString StatsMessage = FString::Printf(
			TEXT("Approach resolve for %s | NavProjectFailed=%d PathFailed=%d BlockedByOverlap=%d InsideBounds=%d TooClose=%d Valid=%d"),
			*GetApproachDebugTargetName(TargetActor),
			RejectStats.NavProjectFailed,
			RejectStats.PathFailed,
			RejectStats.BlockedByOverlap,
			RejectStats.InsideBounds,
			RejectStats.TooClose,
			RejectStats.Valid);
		UE_LOG(LogTemp, Warning, TEXT("%s"), *StatsMessage);
		PrintDebugMessage(StatsMessage, RejectStats.Valid > 0 ? FColor::Green : FColor::Red);
	}

	if (Candidates.Num() <= 0)
	{
		return false;
	}

	Candidates.Sort([](const FNPCApproachCandidate& Left, const FNPCApproachCandidate& Right)
	{
		return Left.Score > Right.Score;
	});

	int32 SelectedIndex = 0;
	if (bRandomizeAmongTopApproachCandidates)
	{
		const int32 TopCandidateCount = FMath::Clamp(TopApproachCandidateCount, 1, Candidates.Num());
		SelectedIndex = FMath::RandRange(0, TopCandidateCount - 1);
	}

	OutApproachLocation = Candidates[SelectedIndex].Location;

	if (bDrawDebugApproachCandidates)
	{
		DrawDebugSphere(World, OutApproachLocation, 28.0f, 12, FColor::Blue, false, 5.0f);
		DrawDebugLine(World, OutApproachLocation, TargetCenter, FColor::Blue, false, 5.0f, 0, 2.0f);
	}

	return true;
}

float UNPCMoveToTableComponent::ScoreApproachCandidate(const FVector& CandidateLocation, const FVector& OwnerLocation, const FVector& ApproachTargetLocation, const FVector& TargetForward, float DesiredDistance, bool bHasLineOfSight, float PathLength) const
{
	const float DistanceToTarget = FVector::Dist2D(CandidateLocation, ApproachTargetLocation);
	const float DistanceError = FMath::Abs(DistanceToTarget - DesiredDistance);
	FVector DirectionFromTarget = CandidateLocation - ApproachTargetLocation;
	DirectionFromTarget.Z = 0.0f;
	if (!DirectionFromTarget.IsNearlyZero())
	{
		DirectionFromTarget.Normalize();
	}

	float Score = 1000.0f;
	Score -= PathLength * 0.35f;
	Score -= DistanceError * 3.0f;
	Score -= FVector::Dist2D(CandidateLocation, OwnerLocation) * 0.05f;
	if (bHasLineOfSight)
	{
		Score += 150.0f;
	}

	const float FrontDot = FVector::DotProduct(DirectionFromTarget, TargetForward);
	Score += FMath::Max(FrontDot, 0.0f) * 120.0f;

	if (DistanceToTarget < DesiredDistance * 0.75f)
	{
		Score -= 250.0f;
	}

	Score += FMath::FRandRange(0.0f, 12.0f);
	return Score;
}

bool UNPCMoveToTableComponent::IsApproachLocationFree(const FVector& Location, const AActor* TargetActor) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(NPCApproachLocationFree), false);
	if (AActor* Owner = GetOwner())
	{
		QueryParams.AddIgnoredActor(Owner);
	}
	if (TargetActor)
	{
		QueryParams.AddIgnoredActor(TargetActor);
	}

	const FCollisionShape CollisionShape = FCollisionShape::MakeCapsule(ApproachCollisionRadius, ApproachCollisionHalfHeight);
	return !World->OverlapBlockingTestByChannel(Location, FQuat::Identity, ECC_Pawn, CollisionShape, QueryParams);
}

float UNPCMoveToTableComponent::CalculateRemainingPathDistance() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return 0.0f;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	if (CurrentPathPoints.Num() > 0 && CurrentPathPoints.IsValidIndex(CurrentPathIndex))
	{
		float RemainingDistance = FVector::Dist(OwnerLocation, CurrentPathPoints[CurrentPathIndex]);
		for (int32 PointIndex = CurrentPathIndex; PointIndex < CurrentPathPoints.Num() - 1; ++PointIndex)
		{
			RemainingDistance += FVector::Dist(CurrentPathPoints[PointIndex], CurrentPathPoints[PointIndex + 1]);
		}

		return RemainingDistance;
	}

	return FVector::Dist(OwnerLocation, TargetLocation);
}

float UNPCMoveToTableComponent::GetDesiredMoveSpeed(float RemainingDistance)
{
	const float EffectiveRemainingDistance = FMath::Max(RemainingDistance, 0.0f);
	const float EffectiveWalkSpeed = WalkSpeed > KINDA_SMALL_NUMBER ? WalkSpeed : 90.0f;
	const float EffectiveRunSpeed = RunSpeed > KINDA_SMALL_NUMBER ? RunSpeed : 220.0f;
	const float EffectiveRunStartDistance = RunStartDistance > KINDA_SMALL_NUMBER ? RunStartDistance : 500.0f;
	const float EffectiveWalkResumeDistance = WalkResumeDistance > KINDA_SMALL_NUMBER
		? FMath::Min(WalkResumeDistance, EffectiveRunStartDistance)
		: 250.0f;
	const float EffectiveSlowDownDistance = SlowDownDistance > KINDA_SMALL_NUMBER ? SlowDownDistance : 140.0f;
	const float EffectiveMinApproachSpeed = MinApproachSpeed > KINDA_SMALL_NUMBER ? MinApproachSpeed : 55.0f;

	if (!bUseDynamicMoveSpeed)
	{
		bIsRunning = false;
		return EffectiveWalkSpeed;
	}

	if (!bIsRunning && EffectiveRemainingDistance >= EffectiveRunStartDistance)
	{
		bIsRunning = true;
	}
	else if (bIsRunning && EffectiveRemainingDistance <= EffectiveWalkResumeDistance)
	{
		bIsRunning = false;
	}

	float DesiredSpeed = bIsRunning ? EffectiveRunSpeed : EffectiveWalkSpeed;
	if (EffectiveRemainingDistance <= EffectiveSlowDownDistance)
	{
		bIsRunning = false;
		const float Alpha = FMath::Clamp(EffectiveRemainingDistance / EffectiveSlowDownDistance, 0.0f, 1.0f);
		const float LowSpeed = FMath::Max(EffectiveMinApproachSpeed, 0.0f);
		const float HighSpeed = FMath::Max(EffectiveWalkSpeed, LowSpeed);
		DesiredSpeed = FMath::Lerp(LowSpeed, HighSpeed, Alpha);
	}

	return FMath::Max(DesiredSpeed, 0.0f);
}

void UNPCMoveToTableComponent::UpdateFollowPlayer(AActor* Owner, float DeltaTime)
{
	if (!bIsFollowingPlayer || !Owner)
	{
		return;
	}

	FollowUpdateTimer += DeltaTime;
	FollowRepathTimer += DeltaTime;
	FollowDebugTimer += DeltaTime;

	const float EffectiveUpdateInterval = FMath::Max(FollowUpdateInterval, 0.05f);
	if (FollowUpdateTimer < EffectiveUpdateInterval)
	{
		return;
	}
	FollowUpdateTimer = 0.0f;

	AActor* PlayerActor = FollowPlayerActor.Get();
	if (!PlayerActor)
	{
		PlayerActor = FindPlayerActor();
		FollowPlayerActor = PlayerActor;
	}

	if (!PlayerActor)
	{
		const FString Message = TEXT("Player actor not found for follow");
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
		PrintDebugMessage(Message, FColor::Red);
		StopFollowingPlayer();
		return;
	}

	const float EffectiveDesiredDistance = FollowDesiredDistance > KINDA_SMALL_NUMBER
		? FollowDesiredDistance
		: 160.0f;
	const float EffectiveStopDistance = FollowStopDistance > KINDA_SMALL_NUMBER
		? FMath::Max(FollowStopDistance, EffectiveDesiredDistance)
		: 170.0f;
	const float EffectiveStartDistance = FollowStartDistance > KINDA_SMALL_NUMBER
		? FMath::Max(FollowStartDistance, EffectiveStopDistance)
		: 280.0f;
	const float EffectiveMinRepathInterval = FollowMinRepathInterval > KINDA_SMALL_NUMBER
		? FollowMinRepathInterval
		: 1.2f;
	const float EffectivePlayerMoveThreshold = FollowRepathPlayerMoveThreshold > KINDA_SMALL_NUMBER
		? FollowRepathPlayerMoveThreshold
		: 180.0f;
	const FVector PlayerLocation = PlayerActor->GetActorLocation();
	const float DistanceToPlayer = FVector::Dist2D(Owner->GetActorLocation(), PlayerLocation);
	const float PlayerMovedDistance = LastFollowPlayerLocation.IsNearlyZero()
		? TNumericLimits<float>::Max()
		: FVector::Dist2D(PlayerLocation, LastFollowPlayerLocation);
	bool bWillRepath = false;
	auto PrintFollowDebugIfNeeded = [&](bool bDidRepath)
	{
		if (bPrintFollowDebug && FollowDebugTimer >= 0.5f)
		{
			PrintDebugMessage(
				FString::Printf(
					TEXT("Follow debug | Distance=%.1f InProgress=%s PlayerMoved=%.1f RepathTimer=%.1f Repath=%s"),
					DistanceToPlayer,
					bFollowMoveInProgress ? TEXT("true") : TEXT("false"),
					PlayerMovedDistance,
					FollowRepathTimer,
					bDidRepath ? TEXT("true") : TEXT("false")),
				FColor::Yellow);
			FollowDebugTimer = 0.0f;
		}
	};

	if (DistanceToPlayer <= EffectiveStopDistance)
	{
		if (bFollowMoveInProgress)
		{
			ClearMovementState();
			bFollowMoveInProgress = false;
			PrintDebugMessage(TEXT("Follow: close enough, stop moving"), FColor::Yellow);
		}
		PrintFollowDebugIfNeeded(false);
		return;
	}

	if (!bFollowMoveInProgress && DistanceToPlayer < EffectiveStartDistance)
	{
		PrintFollowDebugIfNeeded(false);
		return;
	}

	if (bFollowMoveInProgress)
	{
		if (PlayerMovedDistance < EffectivePlayerMoveThreshold)
		{
			PrintFollowDebugIfNeeded(false);
			return;
		}

		if (FollowRepathTimer < EffectiveMinRepathInterval)
		{
			PrintFollowDebugIfNeeded(false);
			return;
		}
	}

	if (!bFollowMoveInProgress && DistanceToPlayer > EffectiveStartDistance)
	{
		bWillRepath = true;
	}
	else if (bFollowMoveInProgress)
	{
		bWillRepath = true;
	}

	if (!bWillRepath)
	{
		PrintFollowDebugIfNeeded(false);
		return;
	}

	FollowRepathTimer = 0.0f;
	LastFollowPlayerLocation = PlayerLocation;
	const bool bStartedMove = MoveNearActor(PlayerActor, EffectiveDesiredDistance);
	if (bStartedMove)
	{
		CurrentMoveReason = TEXT("follow_player");
		CurrentMoveTargetID = TEXT("player");
		LastFollowGoalLocation = TargetLocation;
		bFollowMoveInProgress = true;
		PrintDebugMessage(TEXT("Follow: repath to player"), FColor::Green);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Follow: repath to player failed"));
		bFollowMoveInProgress = false;
	}

	PrintFollowDebugIfNeeded(bStartedMove);
}

FString UNPCMoveToTableComponent::PickGuideLine(const TArray<FString>& Variants, const FString& FallbackLine, FString& LastLine)
{
	FString SelectedLine;
	if (Variants.Num() <= 0)
	{
		SelectedLine = FallbackLine;
	}
	else if (Variants.Num() == 1)
	{
		SelectedLine = Variants[0];
	}
	else
	{
		TArray<int32> CandidateIndexes;
		for (int32 Index = 0; Index < Variants.Num(); ++Index)
		{
			if (!Variants[Index].Equals(LastLine, ESearchCase::CaseSensitive))
			{
				CandidateIndexes.Add(Index);
			}
		}

		if (CandidateIndexes.Num() > 0)
		{
			SelectedLine = Variants[CandidateIndexes[FMath::RandRange(0, CandidateIndexes.Num() - 1)]];
		}
		else
		{
			SelectedLine = Variants[FMath::RandRange(0, Variants.Num() - 1)];
		}
	}

	LastLine = SelectedLine;
	return SelectedLine;
}

void UNPCMoveToTableComponent::UpdateGuidePlayer(AActor* Owner, float DeltaTime)
{
	if (!bIsGuidingPlayer || MovementMode != ENPCMovementMode::GuidePlayer || !Owner)
	{
		return;
	}

	if (GuidePlayerPhase == EGuidePlayerPhase::ApproachingPlayer)
	{
		return;
	}

	if (GuidePlayerPhase != EGuidePlayerPhase::GuidingToTarget
		&& GuidePlayerPhase != EGuidePlayerPhase::PausedForPlayer)
	{
		return;
	}

	GuideCheckTimer += DeltaTime;
	const float EffectiveCheckInterval = GuideCheckInterval > KINDA_SMALL_NUMBER
		? GuideCheckInterval
		: 0.5f;
	if (GuideCheckTimer < EffectiveCheckInterval)
	{
		return;
	}
	GuideCheckTimer = 0.0f;

	AActor* PlayerActor = GuidePlayerActor.Get();
	if (!PlayerActor)
	{
		PlayerActor = FindPlayerActor();
		GuidePlayerActor = PlayerActor;
	}

	if (!PlayerActor)
	{
		if (bGuidePausedForPlayer)
		{
			bGuidePausedForPlayer = false;
			StartGuideMovementToSavedTarget();
		}
		return;
	}

	const float DistanceToPlayer = FVector::Dist2D(Owner->GetActorLocation(), PlayerActor->GetActorLocation());
	const float EffectiveMaxDistance = GuidePlayerMaxDistance > KINDA_SMALL_NUMBER
		? GuidePlayerMaxDistance
		: 450.0f;
	const float EffectiveResumeDistance = GuidePlayerResumeDistance > KINDA_SMALL_NUMBER
		? FMath::Min(GuidePlayerResumeDistance, EffectiveMaxDistance)
		: 280.0f;

	if (GuidePlayerPhase == EGuidePlayerPhase::GuidingToTarget
		&& DistanceToPlayer > EffectiveMaxDistance
		&& !bGuidePausedForPlayer)
	{
		ClearMovementState();
		bGuidePausedForPlayer = true;
		GuidePlayerPhase = EGuidePlayerPhase::PausedForPlayer;
		CurrentMoveSpeed = 0.0f;
		CurrentMoveVelocity = FVector::ZeroVector;
		SetOwnerComponentVelocity(Owner, CurrentMoveVelocity);
		const FString SuggestedLine = PickGuideLine(GuidePausedLineVariants, GuidePausedLine, LastGuidePausedLine);
		OnGuidePausedForPlayer.Broadcast(TEXT("guide_paused"), GuideTargetID, SuggestedLine);
		UE_LOG(LogTemp, Warning, TEXT("Guide paused: waiting for player"));
		UE_LOG(LogTemp, Warning, TEXT("Broadcast OnGuidePausedForPlayer: %s"), *GuideTargetID);
		PrintDebugMessage(TEXT("Guide paused: waiting for player"), FColor::Yellow);
		return;
	}

	if (GuidePlayerPhase == EGuidePlayerPhase::PausedForPlayer
		&& bGuidePausedForPlayer
		&& DistanceToPlayer <= EffectiveResumeDistance)
	{
		bGuidePausedForPlayer = false;
		GuidePlayerPhase = EGuidePlayerPhase::GuidingToTarget;
		const FString SuggestedLine = PickGuideLine(GuideResumedLineVariants, GuideResumedLine, LastGuideResumedLine);
		OnGuideResumed.Broadcast(TEXT("guide_resumed"), GuideTargetID, SuggestedLine);
		UE_LOG(LogTemp, Warning, TEXT("Broadcast OnGuideResumed: %s"), *GuideTargetID);
		if (StartGuideMovementToSavedTarget())
		{
			UE_LOG(LogTemp, Warning, TEXT("Guide resumed: player caught up"));
			PrintDebugMessage(TEXT("Guide resumed: player caught up"), FColor::Green);
		}
	}
}

bool UNPCMoveToTableComponent::StartGuideMovementToSavedTarget()
{
	if (!bIsGuidingPlayer || MovementMode != ENPCMovementMode::GuidePlayer)
	{
		return false;
	}

	const FString ResumeTargetID = GuideTargetID.TrimStartAndEnd();
	if (ResumeTargetID.IsEmpty())
	{
		FailMoveToTable(TEXT("Guide target missing"));
		return false;
	}

	const bool bPreviousPreserveGuideMove = bPreserveMovementModeForGuideMove;
	bPreserveMovementModeForGuideMove = true;
	GuidePlayerPhase = EGuidePlayerPhase::GuidingToTarget;
	bGuidePausedForPlayer = false;

	bool bMoveStarted = false;
	if (GuideTargetType.Equals(TEXT("table"), ESearchCase::IgnoreCase))
	{
		bMoveStarted = MoveToTableByID(ResumeTargetID);
	}
	else if (GuideTargetType.Equals(TEXT("scene_tag"), ESearchCase::IgnoreCase))
	{
		bMoveStarted = MoveToSceneTargetByTag(ResumeTargetID);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Guide target type missing: %s"), *GuideTargetType);
		bPreserveMovementModeForGuideMove = bPreviousPreserveGuideMove;
		StopGuidingPlayer();
		return false;
	}

	bPreserveMovementModeForGuideMove = bPreviousPreserveGuideMove;
	if (bMoveStarted)
	{
		if (!bGuideStartedEventBroadcasted)
		{
			bGuideStartedEventBroadcasted = true;
			OnGuideStarted.Broadcast(TEXT("guide_started"), ResumeTargetID, GuideStartedLine);
			UE_LOG(LogTemp, Warning, TEXT("Broadcast OnGuideStarted: %s"), *ResumeTargetID);
		}
		UE_LOG(LogTemp, Warning, TEXT("Guide started moving to saved target: %s"), *ResumeTargetID);
		PrintDebugMessage(FString::Printf(TEXT("Guide started moving to saved target: %s"), *ResumeTargetID), FColor::Green);
	}
	return bMoveStarted;
}

void UNPCMoveToTableComponent::FinishMoveToTable(AActor* Owner)
{
	if (Owner && bFaceTableOnArrival)
	{
		FaceOwnerTowardLocation(Owner, TargetTableLocation, 0.0f, false);
	}

	const FName CompletedMoveReason = CurrentMoveReason;
	const FString CompletedMoveTargetID = CurrentMoveTargetID;
	const bool bCompletedFollowMove = bIsFollowingPlayer
		&& (CompletedMoveTargetID.Equals(TEXT("player"), ESearchCase::IgnoreCase)
			|| CompletedMoveReason == FName(TEXT("near_player"))
			|| CompletedMoveReason == FName(TEXT("player"))
			|| CompletedMoveReason == FName(TEXT("follow_player")));
	const bool bCompletedGuideMove = bIsGuidingPlayer && MovementMode == ENPCMovementMode::GuidePlayer;
	const EGuidePlayerPhase CompletedGuidePhase = GuidePlayerPhase;
	ClearMovementState();
	if (bCompletedFollowMove)
	{
		bFollowMoveInProgress = false;
	}
	else if (bCompletedGuideMove && CompletedGuidePhase == EGuidePlayerPhase::ApproachingPlayer)
	{
		UE_LOG(LogTemp, Warning, TEXT("Guide start: reached player, now guiding to target"));
		PrintDebugMessage(TEXT("Guide start: reached player, now guiding to target"), FColor::Green);
		StartGuideMovementToSavedTarget();
		return;
	}
	else if (bCompletedGuideMove)
	{
		if (CompletedGuidePhase == EGuidePlayerPhase::GuidingToTarget)
		{
			if (!bGuideArrivedEventBroadcasted)
			{
				bGuideArrivedEventBroadcasted = true;
				const FString SuggestedLine = PickGuideLine(GuideArrivedLineVariants, GuideArrivedLine, LastGuideArrivedLine);
				OnGuideArrived.Broadcast(TEXT("guide_arrived"), GuideTargetID, SuggestedLine);
				UE_LOG(LogTemp, Warning, TEXT("Broadcast OnGuideArrived: %s"), *GuideTargetID);
			}
			UE_LOG(LogTemp, Warning, TEXT("Guide arrived at target"));
			PrintDebugMessage(TEXT("Guide arrived at target"), FColor::Cyan);
		}
		StopGuidingPlayer();
	}
	else if (MovementMode == ENPCMovementMode::CommandedMove
		|| MovementMode == ENPCMovementMode::SeekPlayer)
	{
		SetMovementMode(ENPCMovementMode::Idle);
	}
	PrintDebugMessage(FString::Printf(TEXT("Arrived %s: %s"), *CompletedMoveReason.ToString(), *CompletedMoveTargetID), FColor::Cyan);
	OnNPCMoveCompleted.Broadcast(CompletedMoveReason, CompletedMoveTargetID, true);
}

void UNPCMoveToTableComponent::FailMoveToTable(const FString& Message)
{
	const FName FailedMoveReason = CurrentMoveReason;
	const FString FailedMoveTargetID = CurrentMoveTargetID;
	const bool bFailedFollowMove = bIsFollowingPlayer
		&& (FailedMoveTargetID.Equals(TEXT("player"), ESearchCase::IgnoreCase)
			|| FailedMoveReason == FName(TEXT("near_player"))
			|| FailedMoveReason == FName(TEXT("player"))
			|| FailedMoveReason == FName(TEXT("follow_player")));
	const bool bFailedGuideMove = bIsGuidingPlayer && MovementMode == ENPCMovementMode::GuidePlayer;
	ClearMovementState();
	if (bFailedFollowMove)
	{
		bFollowMoveInProgress = false;
	}
	else if (bFailedGuideMove)
	{
		StopGuidingPlayer();
	}
	else if (MovementMode == ENPCMovementMode::CommandedMove
		|| MovementMode == ENPCMovementMode::SeekPlayer)
	{
		SetMovementMode(ENPCMovementMode::Idle);
	}
	PrintDebugMessage(Message, FColor::Red);
	OnNPCMoveCompleted.Broadcast(FailedMoveReason, FailedMoveTargetID, false);
}

void UNPCMoveToTableComponent::ClearMovementState()
{
	bIsMovingToTable = false;
	CurrentMoveVelocity = FVector::ZeroVector;
	CurrentMoveSpeed = 0.0f;
	bIsRunning = false;
	RemainingMoveDistance = 0.0f;
	MoveSpeedDebugTimer = 0.0f;
	CurrentPathPoints.Reset();
	CurrentPathIndex = 0;
	SetOwnerComponentVelocity(GetOwner(), CurrentMoveVelocity);
}

void UNPCMoveToTableComponent::FaceOwnerTowardDirection(AActor* Owner, const FVector& Direction, float DeltaTime, bool bInterpolate) const
{
	if (!Owner)
	{
		return;
	}

	FVector FlatDirection = Direction;
	FlatDirection.Z = 0.0f;
	if (FlatDirection.IsNearlyZero())
	{
		return;
	}

	const FRotator CurrentRotation = Owner->GetActorRotation();
	FRotator DesiredRotation(CurrentRotation.Pitch, FlatDirection.Rotation().Yaw + FacingYawOffsetDegrees, CurrentRotation.Roll);
	DesiredRotation.Normalize();

	const FRotator NewRotation = bInterpolate
		? FMath::RInterpTo(CurrentRotation, DesiredRotation, DeltaTime, 5.0f)
		: DesiredRotation;
	Owner->SetActorRotation(NewRotation);
}

void UNPCMoveToTableComponent::FaceOwnerTowardLocation(AActor* Owner, const FVector& Location, float DeltaTime, bool bInterpolate) const
{
	if (!Owner)
	{
		return;
	}

	FaceOwnerTowardDirection(Owner, Location - Owner->GetActorLocation(), DeltaTime, bInterpolate);
}

void UNPCMoveToTableComponent::SetOwnerComponentVelocity(AActor* Owner, const FVector& Velocity) const
{
	if (!Owner)
	{
		return;
	}

	if (USceneComponent* RootComponent = Owner->GetRootComponent())
	{
		RootComponent->ComponentVelocity = Velocity;
	}
}

bool UNPCMoveToTableComponent::IsBetterTableCandidate(const ATableMarkerActor* Candidate, const ATableMarkerActor* CurrentBest, const AActor* ReferenceActor) const
{
	if (!Candidate)
	{
		return false;
	}

	if (!CurrentBest)
	{
		return true;
	}

	const float CandidateDistanceSq = GetDistanceSquaredToReference(Candidate, ReferenceActor);
	const float BestDistanceSq = GetDistanceSquaredToReference(CurrentBest, ReferenceActor);
	if (CandidateDistanceSq < BestDistanceSq - KINDA_SMALL_NUMBER)
	{
		return true;
	}

	if (FMath::IsNearlyEqual(CandidateDistanceSq, BestDistanceSq, 1.0f))
	{
		return Candidate->GetPriority() > CurrentBest->GetPriority();
	}

	return false;
}

float UNPCMoveToTableComponent::GetDistanceSquaredToReference(const ATableMarkerActor* TableMarker, const AActor* ReferenceActor) const
{
	if (!TableMarker || !ReferenceActor)
	{
		return 0.0f;
	}

	return FVector::DistSquared(TableMarker->GetActorLocation(), ReferenceActor->GetActorLocation());
}

float UNPCMoveToTableComponent::GetDistanceSquaredToLocation(const FVector& Location, const AActor* ReferenceActor) const
{
	if (!ReferenceActor)
	{
		return 0.0f;
	}

	return FVector::DistSquared(Location, ReferenceActor->GetActorLocation());
}

bool UNPCMoveToTableComponent::IsBetterSceneCandidate(int32 CandidatePriority, float CandidateDistanceSq, bool bHasCurrentBest, int32 CurrentBestPriority, float CurrentBestDistanceSq) const
{
	if (!bHasCurrentBest)
	{
		return true;
	}

	if (CandidatePriority != CurrentBestPriority)
	{
		return CandidatePriority > CurrentBestPriority;
	}

	return CandidateDistanceSq < CurrentBestDistanceSq - KINDA_SMALL_NUMBER;
}

bool UNPCMoveToTableComponent::DoesHintMatchTableName(const FString& TargetHint, const ATableMarkerActor* TableMarker) const
{
	if (!TableMarker || TargetHint.IsEmpty())
	{
		return false;
	}

	const FString TableName = TableMarker->GetTableName().ToString().TrimStartAndEnd();
	if (TableName.IsEmpty())
	{
		return false;
	}

	return TableName.Contains(*TargetHint, ESearchCase::IgnoreCase)
		|| TargetHint.Contains(*TableName, ESearchCase::IgnoreCase);
}

bool UNPCMoveToTableComponent::DoesHintMatchTableTags(const FString& TargetHint, const ATableMarkerActor* TableMarker) const
{
	if (!TableMarker || TargetHint.IsEmpty())
	{
		return false;
	}

	for (const FString& TableTag : TableMarker->GetTableTags())
	{
		if (TableTag.TrimStartAndEnd().Equals(TargetHint, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

bool UNPCMoveToTableComponent::DoesTagMatchStringArray(const FString& TargetTag, const TArray<FString>& Tags) const
{
	const FString TrimmedTargetTag = TargetTag.TrimStartAndEnd();
	if (TrimmedTargetTag.IsEmpty())
	{
		return false;
	}

	for (const FString& Tag : Tags)
	{
		if (Tag.TrimStartAndEnd().Equals(TrimmedTargetTag, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

void UNPCMoveToTableComponent::PrintDebugMessage(const FString& Message, const FColor& Color) const
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, Color, Message);
	}
}
