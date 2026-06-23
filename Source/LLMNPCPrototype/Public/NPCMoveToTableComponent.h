// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCMoveToTableComponent.generated.h"

class ATableMarkerActor;
class AFurnitureMarkerActor;
class ACafeTargetPointActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FNPCMoveCompletedSignature, FName, MoveReason, FString, TargetID, bool, bSuccess);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UNPCMoveToTableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCMoveToTableComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement")
	float MoveSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement")
	float AcceptanceRadius;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement")
	float StopDistanceFromTable;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Table Movement")
	bool bIsMovingToTable;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Table Movement")
	float CurrentMoveSpeed;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Table Movement")
	FVector CurrentMoveVelocity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement")
	float FacingYawOffsetDegrees;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement")
	bool bFaceMovementDirection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement")
	bool bFaceTableOnArrival;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement")
	bool bUseNavigationPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Table Movement")
	TArray<FVector> CurrentPathPoints;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Table Movement")
	int32 CurrentPathIndex;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement")
	bool bStopOnCollisionHit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Approach Location")
	bool bUseAutomaticApproachLocation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Approach Location")
	int32 ApproachSampleCount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Approach Location")
	int32 ApproachRingCount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Approach Location")
	float DefaultApproachDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Approach Location")
	float ApproachDistanceStep;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Approach Location")
	float ApproachCollisionRadius;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Approach Location")
	float ApproachCollisionHalfHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Approach Location")
	bool bRandomizeAmongTopApproachCandidates;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Approach Location")
	int32 TopApproachCandidateCount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Approach Location")
	bool bDrawDebugApproachCandidates;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Approach Location")
	bool bPrintApproachCandidateRejectReasons;

	UPROPERTY(BlueprintAssignable, Category = "Table Movement")
	FNPCMoveCompletedSignature OnNPCMoveCompleted;

	UFUNCTION(BlueprintCallable, Category = "Table Movement")
	bool MoveToTableByID(const FString& TargetTableID);

	UFUNCTION(BlueprintCallable, Category = "Table Movement")
	bool MoveToTableByHint(const FString& TargetHint, AActor* ReferenceActor);

	UFUNCTION(BlueprintCallable, Category = "Table Movement")
	bool MoveNearActor(AActor* TargetActor, float StopDistance = 120.0f);

	UFUNCTION(BlueprintCallable, Category = "Scene Target Movement")
	bool MoveToFurnitureByID(const FString& FurnitureID);

	UFUNCTION(BlueprintCallable, Category = "Scene Target Movement")
	bool MoveToFurnitureByTag(const FString& FurnitureTag);

	UFUNCTION(BlueprintCallable, Category = "Scene Target Movement")
	bool MoveToFurnitureByType(const FString& FurnitureType);

	UFUNCTION(BlueprintCallable, Category = "Scene Target Movement")
	bool MoveToTargetPointByID(const FString& TargetID);

	UFUNCTION(BlueprintCallable, Category = "Scene Target Movement")
	bool MoveToTargetPointByTag(const FString& TargetTag);

	UFUNCTION(BlueprintCallable, Category = "Scene Target Movement")
	bool MoveToSceneTargetByID(const FString& TargetID);

	UFUNCTION(BlueprintCallable, Category = "Scene Target Movement")
	bool MoveToSceneTargetByTag(const FString& TargetTag);

	UFUNCTION(BlueprintCallable, Category = "Table Movement")
	void StopMovingToTable();

private:
	FVector TargetLocation;

	FVector TargetTableLocation;

	FString CurrentTargetTableID;

	FName CurrentMoveReason;

	FString CurrentMoveTargetID;

	bool StartMovingToTable(const ATableMarkerActor* TableMarker, bool bRequireNavigationPath = false);

	bool StartMovingToFurniture(AFurnitureMarkerActor* FurnitureMarker);

	bool StartMovingToFurnitureLocation(AFurnitureMarkerActor* FurnitureMarker, const FVector& MoveLocation);

	bool StartMovingToTargetPoint(const ACafeTargetPointActor* TargetPoint);

	bool StartMovingToLocation(const FVector& MoveLocation, const FVector& FaceLocation, const FName& MoveReason, const FString& TargetID, const FString& DebugMessage, bool bRequireNavigationPath);

	bool BuildNavigationPathToTarget(AActor* Owner);

	bool ProjectTargetLocationToNavigation(AActor* Owner);

	bool ProjectLocationToNavigation(AActor* Owner, const FVector& InLocation, FVector& OutLocation) const;

	bool FindNavigationPathToLocation(AActor* Owner, const FVector& InTargetLocation, TArray<FVector>* OutPathPoints, float* OutPathLength) const;

	bool ResolveFurnitureMoveLocation(AFurnitureMarkerActor* FurnitureMarker, FVector& OutMoveLocation);

	bool ResolveApproachLocationForActor(AActor* TargetActor, const FString& TargetType, FVector& OutApproachLocation);

	float ScoreApproachCandidate(const FVector& CandidateLocation, const FVector& OwnerLocation, const FVector& ApproachTargetLocation, const FVector& TargetForward, float DesiredDistance, bool bHasLineOfSight, float PathLength) const;

	bool IsApproachLocationFree(const FVector& Location, const AActor* TargetActor = nullptr) const;

	void FinishMoveToTable(AActor* Owner);

	void FailMoveToTable(const FString& Message);

	void ClearMovementState();

	void FaceOwnerTowardDirection(AActor* Owner, const FVector& Direction, float DeltaTime, bool bInterpolate) const;

	void FaceOwnerTowardLocation(AActor* Owner, const FVector& Location, float DeltaTime, bool bInterpolate) const;

	void SetOwnerComponentVelocity(AActor* Owner, const FVector& Velocity) const;

	bool IsBetterTableCandidate(const ATableMarkerActor* Candidate, const ATableMarkerActor* CurrentBest, const AActor* ReferenceActor) const;

	float GetDistanceSquaredToReference(const ATableMarkerActor* TableMarker, const AActor* ReferenceActor) const;

	float GetDistanceSquaredToLocation(const FVector& Location, const AActor* ReferenceActor) const;

	bool IsBetterSceneCandidate(int32 CandidatePriority, float CandidateDistanceSq, bool bHasCurrentBest, int32 CurrentBestPriority, float CurrentBestDistanceSq) const;

	bool DoesHintMatchTableName(const FString& TargetHint, const ATableMarkerActor* TableMarker) const;

	bool DoesHintMatchTableTags(const FString& TargetHint, const ATableMarkerActor* TableMarker) const;

	bool DoesTagMatchStringArray(const FString& TargetTag, const TArray<FString>& Tags) const;

	void PrintDebugMessage(const FString& Message, const FColor& Color) const;
};
