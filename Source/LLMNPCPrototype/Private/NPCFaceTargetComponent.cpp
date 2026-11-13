// Copyright Epic Games, Inc. All Rights Reserved.

#include "NPCFaceTargetComponent.h"

#include "GameFramework/Actor.h"
#include "CafeServiceManagerActor.h"
#include "Engine/World.h"
#include "NPCActionQueueComponent.h"
#include "NPCAutonomyComponent.h"
#include "VoicevoxTTSClient.h"
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

	if (bSpeechFacingActive)
	{
		const UVoicevoxTTSClient* VoiceSource = SpeechVoiceSource.Get();
		const bool bVoiceActive = VoiceSource && VoiceSource->GetWorld() == GetWorld()
			&& VoiceSource->HasPendingVoicevoxSpeech();
		if (!SpeechFacingTarget.IsValid() || !GetWorld())
		{
			StopSpeechFacing(TEXT("target_unavailable"));
		}
		else if (!bVoiceActive && GetWorld()->GetTimeSeconds() >= SpeechFacingFallbackUntil)
		{
			StopSpeechFacing(TEXT("speech_finished_or_subtitle_timeout"));
		}
		else
		{
			const FString BlockReason = GetSpeechFacingBlockReason();
			const FString State = BlockReason.IsEmpty() ? TEXT("facing_player") : BlockReason;
			if (LastSpeechFacingState != State)
			{
				LastSpeechFacingState = State;
				UE_LOG(LogTemp, Log, TEXT("[NPCFacing] speech state=%s Owner=%s VoiceActive=%s"),
					*State, *GetNameSafe(GetOwner()), bVoiceActive ? TEXT("true") : TEXT("false"));
			}
			if (BlockReason.IsEmpty())
			{
				FaceActor(SpeechFacingTarget.Get(), DeltaTime, true, true);
			}
			// A legacy gaze request must not bypass seated/task/movement protection.
			return;
		}
	}

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

void UNPCFaceTargetComponent::StartSpeechFacing(
	AActor* TargetActor, UVoicevoxTTSClient* VoiceSource, float SubtitleSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !GetOwner() || !IsValid(TargetActor) || TargetActor->GetWorld() != World)
	{
		return;
	}

	const bool bReplacesSpeech = bSpeechFacingActive;
	SpeechFacingTarget = TargetActor;
	SpeechVoiceSource = IsValid(VoiceSource) && VoiceSource->GetWorld() == World ? VoiceSource : nullptr;
	SpeechFacingFallbackUntil = World->GetTimeSeconds() + FMath::Clamp(SubtitleSeconds, 3.0f, 18.0f);
	bSpeechFacingActive = true;
	LastSpeechFacingState.Reset();
	SpeechAutonomyComponent = GetOwner()->FindComponentByClass<UNPCAutonomyComponent>();
	if (ACafeServiceManagerActor* Manager = ACafeServiceManagerActor::FindForWorld(this, GetOwner()))
	{
		if (IsValid(Manager->AutonomyComponent))
		{
			SpeechAutonomyComponent = Manager->AutonomyComponent;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[NPCFacing] speech request. Owner=%s Target=%s VoiceSource=%s SubtitleSeconds=%.1f Replaced=%s"),
		*GetNameSafe(GetOwner()), *GetNameSafe(TargetActor), *GetNameSafe(SpeechVoiceSource.Get()),
		SubtitleSeconds, bReplacesSpeech ? TEXT("true") : TEXT("false"));
}

FString UNPCFaceTargetComponent::GetSpeechFacingBlockReason() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return TEXT("owner_unavailable");
	}
	// Speech never competes with navigation, even if legacy gaze allows it.
	if (ShouldSkipFacingForMovement())
	{
		return TEXT("moving");
	}
	const UNPCActionQueueComponent* Queue = Owner->FindComponentByClass<UNPCActionQueueComponent>();
	if (Queue && Queue->IsNPCSeated())
	{
		return TEXT("seated_keep_seat_rotation");
	}
	if (const UNPCAutonomyComponent* Autonomy = SpeechAutonomyComponent.Get())
	{
		return Autonomy->GetSpeechBodyRotationBlockReason();
	}
	if (Queue && Queue->HasActiveQueuedAnimation())
	{
		return TEXT("queued_animation");
	}
	return FString();
}

void UNPCFaceTargetComponent::StopSpeechFacing(const TCHAR* Reason)
{
	bSpeechFacingActive = false;
	SpeechFacingTarget.Reset();
	SpeechVoiceSource.Reset();
	SpeechAutonomyComponent.Reset();
	SpeechFacingFallbackUntil = 0.0;
	LastSpeechFacingState.Reset();
	UE_LOG(LogTemp, Log, TEXT("[NPCFacing] speech request released. Owner=%s Reason=%s"),
		*GetNameSafe(GetOwner()), Reason);
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
	return (bIsFacingTarget && IsValid(FocusTarget))
		|| (bSpeechFacingActive && SpeechFacingTarget.IsValid());
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
