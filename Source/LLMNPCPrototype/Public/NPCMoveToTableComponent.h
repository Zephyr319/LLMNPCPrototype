// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCMoveToTableComponent.generated.h"

class ATableMarkerActor;
class AFurnitureMarkerActor;
class ACafeTargetPointActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FNPCMoveCompletedSignature, FName, MoveReason, FString, TargetID, bool, bSuccess);

UENUM(BlueprintType)
enum class ENPCMovementMode : uint8
{
	Idle UMETA(DisplayName = "Idle"),
	FollowPlayer UMETA(DisplayName = "Follow Player"),
	CommandedMove UMETA(DisplayName = "Commanded Move"),
	GuidePlayer UMETA(DisplayName = "Guide Player"),
	SeekPlayer UMETA(DisplayName = "Seek Player")
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UNPCMoveToTableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCMoveToTableComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement")
	float MoveSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement|Dynamic Speed")
	bool bUseDynamicMoveSpeed = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement|Dynamic Speed")
	float WalkSpeed = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement|Dynamic Speed")
	float RunSpeed = 220.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement|Dynamic Speed")
	float RunStartDistance = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement|Dynamic Speed")
	float WalkResumeDistance = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement|Dynamic Speed")
	float SlowDownDistance = 140.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement|Dynamic Speed")
	float MinApproachSpeed = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement|Dynamic Speed")
	float SpeedInterpRate = 5.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Table Movement|Dynamic Speed")
	bool bIsRunning = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Table Movement|Dynamic Speed")
	float RemainingMoveDistance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table Movement|Dynamic Speed")
	bool bPrintMoveSpeedDebug = false;

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

	UPROPERTY(BlueprintReadOnly, Category = "NPC|Movement")
	ENPCMovementMode MovementMode = ENPCMovementMode::Idle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Movement")
	bool bExplicitCommandCancelsFollow = true;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Player")
	float PlayerApproachDistance = 140.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Player")
	float FollowStartDistance = 280.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Player")
	float FollowStopDistance = 170.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Player")
	float FollowDesiredDistance = 160.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Player")
	float FollowUpdateInterval = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Player")
	float FollowMinRepathInterval = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Player")
	float FollowRepathPlayerMoveThreshold = 180.0f;

	UPROPERTY(BlueprintReadOnly, Category = "NPC|Player")
	bool bIsFollowingPlayer = false;

	UPROPERTY(BlueprintReadOnly, Category = "NPC|Player")
	bool bFollowMoveInProgress = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Player")
	bool bPrintFollowDebug = false;

	UPROPERTY(BlueprintReadOnly, Category = "NPC|Guide")
	bool bIsGuidingPlayer = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Guide")
	float GuidePlayerMaxDistance = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Guide")
	float GuidePlayerResumeDistance = 280.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Guide")
	float GuideCheckInterval = 0.5f;

	UPROPERTY(BlueprintAssignable, Category = "Table Movement")
	FNPCMoveCompletedSignature OnNPCMoveCompleted;

	UFUNCTION(BlueprintCallable, Category = "Table Movement")
	bool MoveToTableByID(const FString& TargetTableID);

	UFUNCTION(BlueprintCallable, Category = "Table Movement")
	bool MoveToTableByHint(const FString& TargetHint, AActor* ReferenceActor);

	UFUNCTION(BlueprintCallable, Category = "Table Movement")
	bool MoveNearActor(AActor* TargetActor, float StopDistance = 120.0f);

	UFUNCTION(BlueprintCallable, Category = "NPC|Player")
	AActor* FindPlayerActor() const;

	UFUNCTION(BlueprintCallable, Category = "NPC|Player")
	bool MoveNearPlayer(float StopDistance = 140.0f);

	UFUNCTION(BlueprintCallable, Category = "NPC|Player")
	void StartFollowingPlayer(float DesiredDistance = 160.0f);

	UFUNCTION(BlueprintCallable, Category = "NPC|Player")
	void StopFollowingPlayer();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC|Player")
	bool IsFollowingPlayer() const;

	UFUNCTION(BlueprintCallable, Category = "NPC|Guide")
	bool StartGuideToTable(const FString& TableID);

	UFUNCTION(BlueprintCallable, Category = "NPC|Guide")
	bool StartGuideToSceneTag(const FString& TargetTag);

	UFUNCTION(BlueprintCallable, Category = "NPC|Guide")
	void StopGuidingPlayer();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC|Guide")
	bool IsGuidingPlayer() const;

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

	UFUNCTION(BlueprintCallable, Category = "NPC|Movement")
	void SetMovementMode(ENPCMovementMode NewMode);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC|Movement")
	ENPCMovementMode GetMovementMode() const;

	UFUNCTION(BlueprintCallable, Category = "NPC|Player")
	void CancelFollowingForExplicitCommand();

private:
	FVector TargetLocation;

	FVector TargetTableLocation;

	FString CurrentTargetTableID;

	FName CurrentMoveReason;

	FString CurrentMoveTargetID;

	TWeakObjectPtr<AActor> FollowPlayerActor;

	FVector LastFollowPlayerLocation = FVector::ZeroVector;

	FVector LastFollowGoalLocation = FVector::ZeroVector;

	float FollowUpdateTimer = 0.0f;

	float FollowRepathTimer = 0.0f;

	float FollowDebugTimer = 0.0f;

	bool bPrintedFollowModeSkipWarning = false;

	TWeakObjectPtr<AActor> GuidePlayerActor;

	float GuideCheckTimer = 0.0f;

	bool bGuidePausedForPlayer = false;

	FString GuideTargetType;

	FString GuideTargetID;

	bool bPreserveMovementModeForGuideMove = false;

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

	float CalculateRemainingPathDistance() const;

	float GetDesiredMoveSpeed(float RemainingDistance);

	void UpdateFollowPlayer(AActor* Owner, float DeltaTime);

	void UpdateGuidePlayer(AActor* Owner, float DeltaTime);

	bool ResumeGuideMove();

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

	float MoveSpeedDebugTimer = 0.0f;
};
