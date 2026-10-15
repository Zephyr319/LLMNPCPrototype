// Copyright Epic Games, Inc. All Rights Reserved.

#include "NPCEmotionComponent.h"

UNPCEmotionComponent::UNPCEmotionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	BaselineValence = 0.1f;
	BaselineArousal = 0.35f;
	ShortTermHalfLifeSeconds = 120.0f;
	MoodHalfLifeHours = 6.0f;
	CurrentState.ShortTermValence = BaselineValence;
	CurrentState.Arousal = BaselineArousal;
	CurrentState.MoodValence = BaselineValence;
}

void UNPCEmotionComponent::BeginPlay()
{
	Super::BeginPlay();
	CurrentState.UpdatedAtUtc = FDateTime::UtcNow();
}

void UNPCEmotionComponent::RefreshStateFromRealTime()
{
	const FDateTime Now = FDateTime::UtcNow();
	if (CurrentState.UpdatedAtUtc.GetTicks() <= 0)
	{
		CurrentState.UpdatedAtUtc = Now;
		return;
	}

	const double ElapsedSeconds = FMath::Max((Now - CurrentState.UpdatedAtUtc).GetTotalSeconds(), 0.0);
	if (ElapsedSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const double ShortHalfLife = FMath::Max(static_cast<double>(ShortTermHalfLifeSeconds), 1.0);
	const double MoodHalfLifeSeconds = FMath::Max(static_cast<double>(MoodHalfLifeHours) * 3600.0, 1.0);
	const float ShortRetention = static_cast<float>(FMath::Pow(0.5, ElapsedSeconds / ShortHalfLife));
	const float MoodRetention = static_cast<float>(FMath::Pow(0.5, ElapsedSeconds / MoodHalfLifeSeconds));

	CurrentState.ShortTermValence = BaselineValence
		+ (CurrentState.ShortTermValence - BaselineValence) * ShortRetention;
	CurrentState.Arousal = BaselineArousal
		+ (CurrentState.Arousal - BaselineArousal) * ShortRetention;
	CurrentState.MoodValence = BaselineValence
		+ (CurrentState.MoodValence - BaselineValence) * MoodRetention;
	CurrentState.UpdatedAtUtc = Now;
}

void UNPCEmotionComponent::ApplyAppraisal(
	const FString& EmotionLabel,
	float ValenceDelta,
	float ArousalDelta,
	float MoodDelta,
	const FString& ExpressionIntent,
	const FString& Interpretation)
{
	RefreshStateFromRealTime();

	CurrentState.ShortTermValence = FMath::Clamp(
		CurrentState.ShortTermValence + FMath::Clamp(ValenceDelta, -1.0f, 1.0f),
		-1.0f,
		1.0f);
	CurrentState.Arousal = FMath::Clamp(
		CurrentState.Arousal + FMath::Clamp(ArousalDelta, -1.0f, 1.0f),
		0.0f,
		1.0f);
	CurrentState.MoodValence = FMath::Clamp(
		CurrentState.MoodValence + FMath::Clamp(MoodDelta, -1.0f, 1.0f),
		-1.0f,
		1.0f);
	CurrentState.EmotionLabel = EmotionLabel.TrimStartAndEnd().IsEmpty()
		? TEXT("neutral")
		: EmotionLabel.TrimStartAndEnd();
	CurrentState.ExpressionIntent = ExpressionIntent.TrimStartAndEnd().IsEmpty()
		? TEXT("neutral")
		: ExpressionIntent.TrimStartAndEnd();
	CurrentState.LastInterpretation = Interpretation.TrimStartAndEnd();
	CurrentState.UpdatedAtUtc = FDateTime::UtcNow();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[NPCEmotion] appraisal applied. Emotion=%s Valence=%.2f Arousal=%.2f Mood=%.2f Expression=%s Interpretation=%s"),
		*CurrentState.EmotionLabel,
		CurrentState.ShortTermValence,
		CurrentState.Arousal,
		CurrentState.MoodValence,
		*CurrentState.ExpressionIntent,
		*CurrentState.LastInterpretation);

	OnEmotionStateChanged.Broadcast(CurrentState);
}

FNPCEmotionState UNPCEmotionComponent::GetCurrentState() const
{
	return CurrentState;
}
