// Copyright Epic Games, Inc. All Rights Reserved.

#include "NPCMemoryComponent.h"

#include "NPCPersonaProfile.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UNPCMemoryComponent::UNPCMemoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	MaxMemoryEntries = 64;
	bLogMemoryEvents = true;
	bPersistMemories = true;
	MemoryFileName = LLMNPCPersona::MemoryFileName();
	MemoryFilePath = TEXT("");
	RecallHalfLifeHours = 24.0f;
	NoCueBaselineRelevance = 0.35f;
	ImportanceStrengthWeight = 0.65f;
	bRunRecallValidationWhenPlayerSits = false;
	LastRecalledMemoryID = TEXT("");
}

void UNPCMemoryComponent::BeginPlay()
{
	Super::BeginPlay();
	MemoryFilePath = FPaths::ProjectSavedDir() / TEXT("NPCMemory") / MemoryFileName;
	if (bPersistMemories)
	{
		LoadMemoriesFromDisk();
	}
}

FString UNPCMemoryComponent::RecordMemoryEvent(
	FName EventTag,
	const FString& Summary,
	FName LocationTag,
	const TArray<FName>& CueTags,
	float Valence,
	float Arousal,
	float Importance,
	bool bProtected)
{
	FNPCMemoryEntry Memory;
	Memory.MemoryID = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	Memory.EventTag = EventTag;
	Memory.Summary = Summary.TrimStartAndEnd();
	Memory.LocationTag = LocationTag;
	Memory.CueTags = CueTags;
	Memory.CueTags.RemoveAll([](FName CueTag)
	{
		return CueTag.IsNone();
	});
	Memory.Valence = FMath::Clamp(Valence, -1.0f, 1.0f);
	Memory.Arousal = FMath::Clamp(Arousal, 0.0f, 1.0f);
	Memory.Importance = FMath::Clamp(Importance, 0.0f, 1.0f);
	Memory.CreatedAtUtc = FDateTime::UtcNow();
	Memory.LastRecalledAtUtc = FDateTime();
	Memory.RecallCount = 0;
	Memory.bProtected = bProtected;

	Memories.Add(Memory);
	PruneToCapacity();
	if (bPersistMemories)
	{
		SaveMemoriesToDisk();
	}

	if (bLogMemoryEvents)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[NPCMemory] recorded. ID=%s Event=%s Importance=%.2f Valence=%.2f Arousal=%.2f Summary=%s"),
			*Memory.MemoryID,
			*Memory.EventTag.ToString(),
			Memory.Importance,
			Memory.Valence,
			Memory.Arousal,
			*Memory.Summary);
	}

	OnMemoryRecorded.Broadcast(Memory);
	return Memory.MemoryID;
}

bool UNPCMemoryComponent::MarkMemoryRecalled(const FString& MemoryID)
{
	FNPCMemoryEntry* Memory = Memories.FindByPredicate([&MemoryID](const FNPCMemoryEntry& Candidate)
	{
		return Candidate.MemoryID.Equals(MemoryID, ESearchCase::CaseSensitive);
	});
	if (!Memory)
	{
		return false;
	}

	Memory->LastRecalledAtUtc = FDateTime::UtcNow();
	++Memory->RecallCount;
	if (bPersistMemories)
	{
		SaveMemoriesToDisk();
	}
	return true;
}

bool UNPCMemoryComponent::TryRecallBestMemory(
	const TArray<FName>& CurrentCueTags,
	float AttentionAvailability,
	bool bForceRecall,
	FNPCMemoryEntry& OutMemory,
	FNPCRecallScore& OutScore)
{
	int32 BestMemoryIndex = INDEX_NONE;
	FNPCRecallScore BestScore;

	for (int32 Index = 0; Index < Memories.Num(); ++Index)
	{
		const FNPCRecallScore CandidateScore = CalculateRecallScore(
			Memories[Index],
			CurrentCueTags,
			AttentionAvailability);
		if (BestMemoryIndex == INDEX_NONE
			|| CandidateScore.RecallProbability > BestScore.RecallProbability)
		{
			BestMemoryIndex = Index;
			BestScore = CandidateScore;
		}
	}

	if (BestMemoryIndex == INDEX_NONE)
	{
		return false;
	}

	const float RandomRoll = FMath::FRand();
	const bool bRecallTriggered = bForceRecall || RandomRoll <= BestScore.RecallProbability;
	BestScore.RandomRoll = RandomRoll;
	BestScore.bTriggered = bRecallTriggered;
	BestScore.bForced = bForceRecall;
	LastRecallScore = BestScore;
	OutMemory = Memories[BestMemoryIndex];
	OutScore = BestScore;
	OnMemoryRecallEvaluated.Broadcast(Memories[BestMemoryIndex], BestScore, bRecallTriggered);
	if (!bRecallTriggered)
	{
		if (bLogMemoryEvents)
		{
			UE_LOG(
				LogTemp,
				Verbose,
				TEXT("[NPCMemory] recall not triggered. Event=%s Probability=%.3f Roll=%.3f"),
				*Memories[BestMemoryIndex].EventTag.ToString(),
				BestScore.RecallProbability,
				RandomRoll);
		}
		return false;
	}

	FNPCMemoryEntry& SelectedMemory = Memories[BestMemoryIndex];
	SelectedMemory.LastRecalledAtUtc = FDateTime::UtcNow();
	++SelectedMemory.RecallCount;
	LastRecalledMemoryID = SelectedMemory.MemoryID;
	OutMemory = SelectedMemory;
	OutScore = BestScore;
	if (bPersistMemories)
	{
		SaveMemoriesToDisk();
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[NPCMemory] recalled. Event=%s Probability=%.3f Attention=%.3f Cue=%.3f Strength=%.3f Time=%.3f Forced=%s"),
		*SelectedMemory.EventTag.ToString(),
		BestScore.RecallProbability,
		BestScore.AttentionAvailability,
		BestScore.CueRelevance,
		BestScore.MemoryStrength,
		BestScore.TimeFactor,
		bForceRecall ? TEXT("true") : TEXT("false"));

	OnMemoryRecalled.Broadcast(SelectedMemory, BestScore);
	return true;
}

FNPCRecallScore UNPCMemoryComponent::CalculateRecallScore(
	const FNPCMemoryEntry& Memory,
	const TArray<FName>& CurrentCueTags,
	float AttentionAvailability) const
{
	FNPCRecallScore Score;
	Score.AttentionAvailability = FMath::Clamp(AttentionAvailability, 0.0f, 1.0f);
	Score.CueRelevance = CalculateCueRelevance(Memory.CueTags, CurrentCueTags);

	const float EmotionalIntensity = FMath::Clamp(
		(FMath::Abs(Memory.Valence) + Memory.Arousal) * 0.5f,
		0.0f,
		1.0f);
	const float ImportanceWeight = FMath::Clamp(ImportanceStrengthWeight, 0.0f, 1.0f);
	Score.MemoryStrength = FMath::Clamp(
		Memory.Importance * ImportanceWeight + EmotionalIntensity * (1.0f - ImportanceWeight),
		0.0f,
		1.0f);

	const FTimespan MemoryAge = FDateTime::UtcNow() - Memory.CreatedAtUtc;
	const double AgeHours = FMath::Max(MemoryAge.GetTotalHours(), 0.0);
	const double SafeHalfLifeHours = FMath::Max(static_cast<double>(RecallHalfLifeHours), 0.01);
	Score.TimeFactor = static_cast<float>(FMath::Pow(0.5, AgeHours / SafeHalfLifeHours));

	Score.RecallProbability = FMath::Clamp(
		Score.AttentionAvailability
			* Score.CueRelevance
			* Score.MemoryStrength
			* Score.TimeFactor,
		0.0f,
		1.0f);
	return Score;
}

bool UNPCMemoryComponent::FindMemoryByID(const FString& MemoryID, FNPCMemoryEntry& OutMemory) const
{
	const FNPCMemoryEntry* Memory = Memories.FindByPredicate([&MemoryID](const FNPCMemoryEntry& Candidate)
	{
		return Candidate.MemoryID.Equals(MemoryID, ESearchCase::CaseSensitive);
	});
	if (!Memory)
	{
		return false;
	}

	OutMemory = *Memory;
	return true;
}

TArray<FNPCMemoryEntry> UNPCMemoryComponent::GetRecentMemories(int32 MaximumCount) const
{
	const int32 Count = FMath::Clamp(MaximumCount, 0, Memories.Num());
	TArray<FNPCMemoryEntry> Result;
	Result.Reserve(Count);
	for (int32 Index = Memories.Num() - Count; Index < Memories.Num(); ++Index)
	{
		Result.Add(Memories[Index]);
	}
	return Result;
}

bool UNPCMemoryComponent::SaveMemoriesToDisk()
{
	if (!bPersistMemories)
	{
		return false;
	}
	if (MemoryFilePath.IsEmpty())
	{
		MemoryFilePath = FPaths::ProjectSavedDir() / TEXT("NPCMemory") / MemoryFileName;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*FPaths::GetPath(MemoryFilePath));

	TArray<TSharedPtr<FJsonValue>> MemoryValues;
	for (const FNPCMemoryEntry& Memory : Memories)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("memory_id"), Memory.MemoryID);
		Object->SetStringField(TEXT("event_tag"), Memory.EventTag.ToString());
		Object->SetStringField(TEXT("summary"), Memory.Summary);
		Object->SetStringField(TEXT("location_tag"), Memory.LocationTag.ToString());
		TArray<TSharedPtr<FJsonValue>> CueValues;
		for (const FName Cue : Memory.CueTags)
		{
			CueValues.Add(MakeShared<FJsonValueString>(Cue.ToString()));
		}
		Object->SetArrayField(TEXT("cue_tags"), CueValues);
		Object->SetNumberField(TEXT("valence"), Memory.Valence);
		Object->SetNumberField(TEXT("arousal"), Memory.Arousal);
		Object->SetNumberField(TEXT("importance"), Memory.Importance);
		Object->SetStringField(TEXT("created_at_utc"), Memory.CreatedAtUtc.ToIso8601());
		Object->SetStringField(TEXT("last_recalled_at_utc"),
			Memory.LastRecalledAtUtc.GetTicks() > 0 ? Memory.LastRecalledAtUtc.ToIso8601() : TEXT(""));
		Object->SetNumberField(TEXT("recall_count"), Memory.RecallCount);
		Object->SetBoolField(TEXT("protected"), Memory.bProtected);
		MemoryValues.Add(MakeShared<FJsonValueObject>(Object));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetArrayField(TEXT("memories"), MemoryValues);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	const bool bSaved = FFileHelper::SaveStringToFile(Json, *MemoryFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (!bSaved)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NPCMemory] failed to save memories. Path=%s"), *MemoryFilePath);
	}
	return bSaved;
}

bool UNPCMemoryComponent::LoadMemoriesFromDisk()
{
	if (!bPersistMemories)
	{
		return false;
	}
	if (MemoryFilePath.IsEmpty())
	{
		MemoryFilePath = FPaths::ProjectSavedDir() / TEXT("NPCMemory") / MemoryFileName;
	}

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *MemoryFilePath))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[NPCMemory] failed to parse memory file. Path=%s"), *MemoryFilePath);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* MemoryValues = nullptr;
	if (!Root->TryGetArrayField(TEXT("memories"), MemoryValues) || !MemoryValues)
	{
		return false;
	}

	TArray<FNPCMemoryEntry> LoadedMemories;
	for (const TSharedPtr<FJsonValue>& Value : *MemoryValues)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Object.IsValid())
		{
			continue;
		}
		FNPCMemoryEntry Memory;
		Object->TryGetStringField(TEXT("memory_id"), Memory.MemoryID);
		FString StringValue;
		if (Object->TryGetStringField(TEXT("event_tag"), StringValue))
		{
			Memory.EventTag = FName(*StringValue);
		}
		Object->TryGetStringField(TEXT("summary"), Memory.Summary);
		if (Object->TryGetStringField(TEXT("location_tag"), StringValue))
		{
			Memory.LocationTag = FName(*StringValue);
		}
		const TArray<TSharedPtr<FJsonValue>>* CueValues = nullptr;
		if (Object->TryGetArrayField(TEXT("cue_tags"), CueValues) && CueValues)
		{
			for (const TSharedPtr<FJsonValue>& CueValue : *CueValues)
			{
				FString Cue;
				if (CueValue.IsValid() && CueValue->TryGetString(Cue) && !Cue.IsEmpty())
				{
					Memory.CueTags.Add(FName(*Cue));
				}
			}
		}
		double Number = 0.0;
		if (Object->TryGetNumberField(TEXT("valence"), Number)) Memory.Valence = static_cast<float>(Number);
		if (Object->TryGetNumberField(TEXT("arousal"), Number)) Memory.Arousal = static_cast<float>(Number);
		if (Object->TryGetNumberField(TEXT("importance"), Number)) Memory.Importance = static_cast<float>(Number);
		if (Object->TryGetNumberField(TEXT("recall_count"), Number)) Memory.RecallCount = FMath::Max(FMath::RoundToInt(Number), 0);
		Object->TryGetBoolField(TEXT("protected"), Memory.bProtected);
		if (Object->TryGetStringField(TEXT("created_at_utc"), StringValue))
		{
			FDateTime::ParseIso8601(*StringValue, Memory.CreatedAtUtc);
		}
		if (Object->TryGetStringField(TEXT("last_recalled_at_utc"), StringValue) && !StringValue.IsEmpty())
		{
			FDateTime::ParseIso8601(*StringValue, Memory.LastRecalledAtUtc);
		}
		if (Memory.MemoryID.IsEmpty())
		{
			Memory.MemoryID = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		}
		if (Memory.CreatedAtUtc.GetTicks() <= 0)
		{
			Memory.CreatedAtUtc = FDateTime::UtcNow();
		}
		LoadedMemories.Add(Memory);
	}

	Memories = MoveTemp(LoadedMemories);
	PruneToCapacity();
	UE_LOG(LogTemp, Log, TEXT("[NPCMemory] loaded persistent memories. Count=%d Path=%s"), Memories.Num(), *MemoryFilePath);
	return true;
}

void UNPCMemoryComponent::PruneToCapacity()
{
	const int32 Capacity = FMath::Max(MaxMemoryEntries, 1);
	while (Memories.Num() > Capacity)
	{
		int32 RemovalIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Memories.Num(); ++Index)
		{
			if (Memories[Index].bProtected)
			{
				continue;
			}

			if (RemovalIndex == INDEX_NONE
				|| Memories[Index].Importance < Memories[RemovalIndex].Importance
				|| (FMath::IsNearlyEqual(Memories[Index].Importance, Memories[RemovalIndex].Importance)
					&& Memories[Index].CreatedAtUtc < Memories[RemovalIndex].CreatedAtUtc))
			{
				RemovalIndex = Index;
			}
		}

		if (RemovalIndex == INDEX_NONE)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[NPCMemory] capacity exceeded, but every memory is protected. Count=%d Capacity=%d"),
				Memories.Num(),
				Capacity);
			break;
		}

		Memories.RemoveAt(RemovalIndex);
	}
}

float UNPCMemoryComponent::CalculateCueRelevance(
	const TArray<FName>& MemoryCueTags,
	const TArray<FName>& CurrentCueTags) const
{
	const float Baseline = FMath::Clamp(NoCueBaselineRelevance, 0.0f, 1.0f);
	if (MemoryCueTags.IsEmpty() || CurrentCueTags.IsEmpty())
	{
		return Baseline;
	}

	int32 MatchingCueCount = 0;
	for (const FName MemoryCue : MemoryCueTags)
	{
		if (CurrentCueTags.Contains(MemoryCue))
		{
			++MatchingCueCount;
		}
	}

	const float MatchRatio = static_cast<float>(MatchingCueCount) / static_cast<float>(MemoryCueTags.Num());
	return Baseline + (1.0f - Baseline) * MatchRatio;
}
