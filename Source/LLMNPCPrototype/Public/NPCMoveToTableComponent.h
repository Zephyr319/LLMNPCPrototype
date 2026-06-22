// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCMoveToTableComponent.generated.h"

class ATableMarkerActor;

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

	UFUNCTION(BlueprintCallable, Category = "Table Movement")
	bool MoveToTableByID(const FString& TargetTableID);

	UFUNCTION(BlueprintCallable, Category = "Table Movement")
	bool MoveToTableByHint(const FString& TargetHint, AActor* ReferenceActor);

	UFUNCTION(BlueprintCallable, Category = "Table Movement")
	void StopMovingToTable();

private:
	FVector TargetLocation;

	FVector TargetTableLocation;

	FString CurrentTargetTableID;

	bool StartMovingToTable(const ATableMarkerActor* TableMarker);

	bool BuildNavigationPathToTarget(AActor* Owner);

	void FinishMoveToTable(AActor* Owner);

	void ClearMovementState();

	void FaceOwnerTowardDirection(AActor* Owner, const FVector& Direction, float DeltaTime, bool bInterpolate) const;

	void FaceOwnerTowardLocation(AActor* Owner, const FVector& Location, float DeltaTime, bool bInterpolate) const;

	void SetOwnerComponentVelocity(AActor* Owner, const FVector& Velocity) const;

	bool IsBetterTableCandidate(const ATableMarkerActor* Candidate, const ATableMarkerActor* CurrentBest, const AActor* ReferenceActor) const;

	float GetDistanceSquaredToReference(const ATableMarkerActor* TableMarker, const AActor* ReferenceActor) const;

	bool DoesHintMatchTableName(const FString& TargetHint, const ATableMarkerActor* TableMarker) const;

	bool DoesHintMatchTableTags(const FString& TargetHint, const ATableMarkerActor* TableMarker) const;

	void PrintDebugMessage(const FString& Message, const FColor& Color) const;
};
