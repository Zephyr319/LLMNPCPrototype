// Copyright Epic Games, Inc. All Rights Reserved.

#include "NPCMoveToTableComponent.h"

#include "CafeTargetPointActor.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "FurnitureMarkerActor.h"
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
}

UNPCMoveToTableComponent::UNPCMoveToTableComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	MoveSpeed = 120.0f;
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
}

void UNPCMoveToTableComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsMovingToTable)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		FailMoveToTable(TEXT("NPC movement owner missing"));
		return;
	}

	if (DeltaTime <= KINDA_SMALL_NUMBER)
	{
		CurrentMoveVelocity = FVector::ZeroVector;
		CurrentMoveSpeed = 0.0f;
		SetOwnerComponentVelocity(Owner, CurrentMoveVelocity);
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

	const FVector DesiredLocation = FMath::VInterpConstantTo(OldLocation, MoveTarget, DeltaTime, MoveSpeed);
	FHitResult SweepHit;
	Owner->SetActorLocation(DesiredLocation, true, &SweepHit);

	const FVector NewLocation = Owner->GetActorLocation();
	const FVector DeltaLocation = NewLocation - OldLocation;
	CurrentMoveVelocity = DeltaLocation / DeltaTime;
	CurrentMoveSpeed = CurrentMoveVelocity.Size2D();
	SetOwnerComponentVelocity(Owner, CurrentMoveVelocity);

	if (SweepHit.bBlockingHit && bStopOnCollisionHit)
	{
		FailMoveToTable(TEXT("NPC movement blocked"));
		return;
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
	return true;
}

bool UNPCMoveToTableComponent::MoveToFurnitureByID(const FString& FurnitureID)
{
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

void UNPCMoveToTableComponent::FinishMoveToTable(AActor* Owner)
{
	if (Owner && bFaceTableOnArrival)
	{
		FaceOwnerTowardLocation(Owner, TargetTableLocation, 0.0f, false);
	}

	const FName CompletedMoveReason = CurrentMoveReason;
	const FString CompletedMoveTargetID = CurrentMoveTargetID;
	ClearMovementState();
	PrintDebugMessage(FString::Printf(TEXT("Arrived %s: %s"), *CompletedMoveReason.ToString(), *CompletedMoveTargetID), FColor::Cyan);
	OnNPCMoveCompleted.Broadcast(CompletedMoveReason, CompletedMoveTargetID, true);
}

void UNPCMoveToTableComponent::FailMoveToTable(const FString& Message)
{
	const FName FailedMoveReason = CurrentMoveReason;
	const FString FailedMoveTargetID = CurrentMoveTargetID;
	ClearMovementState();
	PrintDebugMessage(Message, FColor::Red);
	OnNPCMoveCompleted.Broadcast(FailedMoveReason, FailedMoveTargetID, false);
}

void UNPCMoveToTableComponent::ClearMovementState()
{
	bIsMovingToTable = false;
	CurrentMoveVelocity = FVector::ZeroVector;
	CurrentMoveSpeed = 0.0f;
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
