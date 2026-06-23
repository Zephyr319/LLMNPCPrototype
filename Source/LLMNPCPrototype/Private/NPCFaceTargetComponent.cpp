// Copyright Epic Games, Inc. All Rights Reserved.

#include "NPCFaceTargetComponent.h"

#include "GameFramework/Actor.h"
#include "NPCMoveToTableComponent.h"

UNPCFaceTargetComponent::UNPCFaceTargetComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	FocusTarget = nullptr;
	bIsFacingTarget = false;
	bYawOnly = true;
	TurnSpeed = 8.0f;
	FacingYawOffsetDegrees = 0.0f;
	bSkipWhileMoving = true;
	MovingSpeedThreshold = 5.0f;
}

void UNPCFaceTargetComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsFacingTarget)
	{
		return;
	}

	if (!IsValid(FocusTarget))
	{
		return;
	}

	if (bSkipWhileMoving && ShouldSkipFacingForMovement())
	{
		return;
	}

	FaceActor(FocusTarget, DeltaTime, true, false);
}

void UNPCFaceTargetComponent::StartFacingActor(AActor* TargetActor)
{
	if (!IsValid(TargetActor))
	{
		return;
	}

	FocusTarget = TargetActor;
	bIsFacingTarget = true;
}

void UNPCFaceTargetComponent::StopFacingActor()
{
	FocusTarget = nullptr;
	bIsFacingTarget = false;
}

void UNPCFaceTargetComponent::FaceActorOnce(AActor* TargetActor)
{
	if (!IsValid(TargetActor))
	{
		return;
	}

	FaceActor(TargetActor, 0.0f, false, true);
}

bool UNPCFaceTargetComponent::IsFacingActor() const
{
	return bIsFacingTarget && IsValid(FocusTarget);
}

bool UNPCFaceTargetComponent::ShouldSkipFacingForMovement() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	const UNPCMoveToTableComponent* MoveComponent = Owner->FindComponentByClass<UNPCMoveToTableComponent>();
	return MoveComponent && MoveComponent->CurrentMoveSpeed > MovingSpeedThreshold;
}

void UNPCFaceTargetComponent::FaceActor(AActor* TargetActor, float DeltaTime, bool bInterpolate, bool bForceYawOnly) const
{
	AActor* Owner = GetOwner();
	if (!Owner || !IsValid(TargetActor))
	{
		return;
	}

	const FRotator CurrentRotation = Owner->GetActorRotation();
	const bool bUseYawOnly = bForceYawOnly || bYawOnly;

	FVector Direction = TargetActor->GetActorLocation() - Owner->GetActorLocation();
	if (bUseYawOnly)
	{
		Direction.Z = 0.0f;
	}

	if (Direction.IsNearlyZero())
	{
		return;
	}

	FRotator DesiredRotation = Direction.Rotation();
	DesiredRotation.Yaw += FacingYawOffsetDegrees;

	if (bUseYawOnly)
	{
		DesiredRotation.Pitch = CurrentRotation.Pitch;
		DesiredRotation.Roll = CurrentRotation.Roll;
	}

	DesiredRotation.Normalize();

	const FRotator NewRotation = bInterpolate
		? FMath::RInterpTo(CurrentRotation, DesiredRotation, DeltaTime, TurnSpeed)
		: DesiredRotation;
	Owner->SetActorRotation(NewRotation);
}
