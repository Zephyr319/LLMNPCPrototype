// Copyright Epic Games, Inc. All Rights Reserved.

#include "NPCMoveToTableComponent.h"

#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "TableMarkerActor.h"
#include "UObject/UnrealType.h"

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
		StopMovingToTable();
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
		ClearMovementState();
		PrintDebugMessage(TEXT("NPC movement blocked"), FColor::Red);
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
	UWorld* World = GetWorld();
	if (!World)
	{
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

	PrintDebugMessage(FString::Printf(TEXT("Table not found: %s"), *TargetTableID), FColor::Red);
	return false;
}

bool UNPCMoveToTableComponent::MoveToTableByHint(const FString& TargetHint, AActor* ReferenceActor)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FString TrimmedHint = TargetHint.TrimStartAndEnd();
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

	PrintDebugMessage(FString::Printf(TEXT("No table matched hint: %s"), *TargetHint), FColor::Red);
	return false;
}

void UNPCMoveToTableComponent::StopMovingToTable()
{
	ClearMovementState();
}

bool UNPCMoveToTableComponent::StartMovingToTable(const ATableMarkerActor* TableMarker)
{
	if (!TableMarker)
	{
		return false;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	TargetTableLocation = TableMarker->GetActorLocation();
	TargetLocation = TargetTableLocation - TableMarker->GetActorForwardVector() * StopDistanceFromTable;
	CurrentTargetTableID = TableMarker->GetTableID();
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

bool UNPCMoveToTableComponent::BuildNavigationPathToTarget(AActor* Owner)
{
	CurrentPathPoints.Reset();
	CurrentPathIndex = 0;

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
	FindPathParams.PathEnd = TargetLocation;
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

	CurrentPathPoints = *PathPoints;
	CurrentPathIndex = 1;
	return CurrentPathPoints.IsValidIndex(CurrentPathIndex);
}

void UNPCMoveToTableComponent::FinishMoveToTable(AActor* Owner)
{
	if (Owner && bFaceTableOnArrival)
	{
		FaceOwnerTowardLocation(Owner, TargetTableLocation, 0.0f, false);
	}

	ClearMovementState();
	PrintDebugMessage(FString::Printf(TEXT("Arrived table: %s"), *CurrentTargetTableID), FColor::Cyan);
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

void UNPCMoveToTableComponent::PrintDebugMessage(const FString& Message, const FColor& Color) const
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, Color, Message);
	}
}
