// Copyright Epic Games, Inc. All Rights Reserved.

#include "NPCResearchLoggerComponent.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NPCAutonomyComponent.h"
#include "NPCEmotionComponent.h"
#include "NPCMemoryComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString SerializeJsonObject(const TSharedRef<FJsonObject>& Object)
	{
		FString Json;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		FJsonSerializer::Serialize(Object, Writer);
		return Json;
	}

	TSharedPtr<FJsonObject> ParsePayloadObject(const FString& PayloadJson)
	{
		TSharedPtr<FJsonObject> Payload;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PayloadJson);
		if (!FJsonSerializer::Deserialize(Reader, Payload) || !Payload.IsValid())
		{
			Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("raw_value"), PayloadJson);
		}
		return Payload;
	}

	TWeakObjectPtr<UNPCResearchLoggerComponent> GActiveResearchLogger;

	TArray<TSharedPtr<FJsonValue>> NamesToJsonValues(const TArray<FName>& Names)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FName Name : Names)
		{
			Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
		}
		return Values;
	}
}

UNPCResearchLoggerComponent::UNPCResearchLoggerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bLoggingEnabled = true;
	OutputDirectoryName = TEXT("ResearchLogs");
}

void UNPCResearchLoggerComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!bLoggingEnabled)
	{
		return;
	}

	SessionID = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	const FString Directory = FPaths::ProjectSavedDir() / OutputDirectoryName;
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Directory);
	LogFilePath = Directory / FString::Printf(
		TEXT("npc_agent_%s_%s.jsonl"),
		*FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")),
		*SessionID.Left(8));

	if (AActor* Owner = GetOwner())
	{
		if (UNPCMemoryComponent* Memory = Owner->FindComponentByClass<UNPCMemoryComponent>())
		{
			Memory->OnMemoryRecorded.AddDynamic(this, &UNPCResearchLoggerComponent::HandleMemoryRecorded);
			Memory->OnMemoryRecallEvaluated.AddDynamic(this, &UNPCResearchLoggerComponent::HandleMemoryRecallEvaluated);
		}
		if (UNPCEmotionComponent* Emotion = Owner->FindComponentByClass<UNPCEmotionComponent>())
		{
			Emotion->OnEmotionStateChanged.AddDynamic(this, &UNPCResearchLoggerComponent::HandleEmotionStateChanged);
		}
		if (UNPCAutonomyComponent* Autonomy = Owner->FindComponentByClass<UNPCAutonomyComponent>())
		{
			Autonomy->OnAutonomyDecisionMade.AddDynamic(this, &UNPCResearchLoggerComponent::HandleDecisionMade);
			Autonomy->OnAutonomyActivityStarted.AddDynamic(this, &UNPCResearchLoggerComponent::HandleActivityStarted);
			Autonomy->OnAutonomyActivityFinished.AddDynamic(this, &UNPCResearchLoggerComponent::HandleActivityFinished);
		}
	}

	GActiveResearchLogger = this;
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("map"), GetWorld() ? GetWorld()->GetMapName() : TEXT("unknown"));
	Payload->SetNumberField(TEXT("log_schema_version"), 2);
	RecordStructuredEvent(TEXT("session_started"), SerializeJsonObject(Payload));
	UE_LOG(LogTemp, Log, TEXT("[NPCResearch] session log started. Path=%s"), *LogFilePath);
}

void UNPCResearchLoggerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bLoggingEnabled && !LogFilePath.IsEmpty())
	{
		if (!TrialID.IsEmpty())
		{
			EndTrial(TEXT("session_ended_before_trial_end"));
		}
		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetNumberField(TEXT("end_play_reason"), static_cast<int32>(EndPlayReason));
		RecordStructuredEvent(TEXT("session_ended"), SerializeJsonObject(Payload));
	}

	if (AActor* Owner = GetOwner())
	{
		if (UNPCMemoryComponent* Memory = Owner->FindComponentByClass<UNPCMemoryComponent>())
		{
			Memory->OnMemoryRecorded.RemoveDynamic(this, &UNPCResearchLoggerComponent::HandleMemoryRecorded);
			Memory->OnMemoryRecallEvaluated.RemoveDynamic(this, &UNPCResearchLoggerComponent::HandleMemoryRecallEvaluated);
		}
		if (UNPCEmotionComponent* Emotion = Owner->FindComponentByClass<UNPCEmotionComponent>())
		{
			Emotion->OnEmotionStateChanged.RemoveDynamic(this, &UNPCResearchLoggerComponent::HandleEmotionStateChanged);
		}
		if (UNPCAutonomyComponent* Autonomy = Owner->FindComponentByClass<UNPCAutonomyComponent>())
		{
			Autonomy->OnAutonomyDecisionMade.RemoveDynamic(this, &UNPCResearchLoggerComponent::HandleDecisionMade);
			Autonomy->OnAutonomyActivityStarted.RemoveDynamic(this, &UNPCResearchLoggerComponent::HandleActivityStarted);
			Autonomy->OnAutonomyActivityFinished.RemoveDynamic(this, &UNPCResearchLoggerComponent::HandleActivityFinished);
		}
	}
	if (GActiveResearchLogger.Get() == this)
	{
		GActiveResearchLogger.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

UNPCResearchLoggerComponent* UNPCResearchLoggerComponent::GetActiveLogger()
{
	return GActiveResearchLogger.Get();
}

void UNPCResearchLoggerComponent::RecordGlobalEvent(const FString& EventType, const FString& PayloadJson)
{
	if (UNPCResearchLoggerComponent* Logger = GActiveResearchLogger.Get())
	{
		Logger->RecordStructuredEvent(EventType, PayloadJson);
	}
}

void UNPCResearchLoggerComponent::BeginTrial(const FString& InTrialID, const FString& InCondition)
{
	if (!TrialID.IsEmpty())
	{
		EndTrial(TEXT("superseded_by_new_trial"));
	}
	TrialID = InTrialID.TrimStartAndEnd();
	TrialCondition = InCondition.TrimStartAndEnd();
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("map"), GetWorld() ? GetWorld()->GetMapName() : TEXT("unknown"));
	RecordStructuredEvent(TEXT("trial_started"), SerializeJsonObject(Payload));
	UE_LOG(LogTemp, Warning, TEXT("[Trial] started. Trial=%s Condition=%s"), *TrialID, *TrialCondition);
}

void UNPCResearchLoggerComponent::EndTrial(const FString& Note)
{
	if (TrialID.IsEmpty())
	{
		return;
	}
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("note"), Note);
	RecordStructuredEvent(TEXT("trial_ended"), SerializeJsonObject(Payload));
	UE_LOG(LogTemp, Warning, TEXT("[Trial] ended. Trial=%s Note=%s"), *TrialID, *Note);
	TrialID.Reset();
}

int32 UNPCResearchLoggerComponent::BeginInteraction()
{
	return ++InteractionID;
}

void UNPCResearchLoggerComponent::RecordStructuredEvent(const FString& EventType, const FString& PayloadJson)
{
	if (!bLoggingEnabled || LogFilePath.IsEmpty())
	{
		return;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
	Root->SetStringField(TEXT("session_id"), SessionID);
	Root->SetStringField(TEXT("event_type"), EventType.TrimStartAndEnd());
	Root->SetNumberField(TEXT("seq"), static_cast<double>(++EventSequence));
	Root->SetNumberField(TEXT("world_time"), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0);
	Root->SetStringField(TEXT("trial_id"), TrialID);
	Root->SetStringField(TEXT("condition"), TrialCondition);
	Root->SetNumberField(TEXT("interaction_id"), InteractionID);
	Root->SetObjectField(TEXT("payload"), ParsePayloadObject(PayloadJson));
	const FString Line = SerializeJsonObject(Root) + LINE_TERMINATOR;
	if (!FFileHelper::SaveStringToFile(
		Line,
		*LogFilePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(),
		FILEWRITE_Append | FILEWRITE_AllowRead))
	{
		UE_LOG(LogTemp, Warning, TEXT("[NPCResearch] failed to append event. Type=%s Path=%s"), *EventType, *LogFilePath);
	}
}

void UNPCResearchLoggerComponent::HandleMemoryRecorded(const FNPCMemoryEntry& Memory)
{
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("memory_id"), Memory.MemoryID);
	Payload->SetStringField(TEXT("event_tag"), Memory.EventTag.ToString());
	Payload->SetStringField(TEXT("summary"), Memory.Summary);
	Payload->SetStringField(TEXT("location"), Memory.LocationTag.ToString());
	Payload->SetArrayField(TEXT("cue_tags"), NamesToJsonValues(Memory.CueTags));
	Payload->SetNumberField(TEXT("valence"), Memory.Valence);
	Payload->SetNumberField(TEXT("arousal"), Memory.Arousal);
	Payload->SetNumberField(TEXT("importance"), Memory.Importance);
	RecordStructuredEvent(TEXT("memory_recorded"), SerializeJsonObject(Payload));
}

void UNPCResearchLoggerComponent::HandleMemoryRecallEvaluated(
	const FNPCMemoryEntry& Memory,
	const FNPCRecallScore& Score,
	bool bTriggered)
{
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("memory_id"), Memory.MemoryID);
	Payload->SetStringField(TEXT("event_tag"), Memory.EventTag.ToString());
	Payload->SetNumberField(TEXT("attention"), Score.AttentionAvailability);
	Payload->SetNumberField(TEXT("cue_relevance"), Score.CueRelevance);
	Payload->SetNumberField(TEXT("memory_strength"), Score.MemoryStrength);
	Payload->SetNumberField(TEXT("time_factor"), Score.TimeFactor);
	Payload->SetNumberField(TEXT("probability"), Score.RecallProbability);
	Payload->SetNumberField(TEXT("random_roll"), Score.RandomRoll);
	Payload->SetBoolField(TEXT("triggered"), bTriggered);
	Payload->SetBoolField(TEXT("forced"), Score.bForced);
	RecordStructuredEvent(TEXT("memory_recall_evaluated"), SerializeJsonObject(Payload));
}

void UNPCResearchLoggerComponent::HandleEmotionStateChanged(const FNPCEmotionState& EmotionState)
{
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("emotion"), EmotionState.EmotionLabel);
	Payload->SetNumberField(TEXT("short_term_valence"), EmotionState.ShortTermValence);
	Payload->SetNumberField(TEXT("arousal"), EmotionState.Arousal);
	Payload->SetNumberField(TEXT("mood_valence"), EmotionState.MoodValence);
	Payload->SetStringField(TEXT("expression_intent"), EmotionState.ExpressionIntent);
	Payload->SetStringField(TEXT("interpretation"), EmotionState.LastInterpretation);
	RecordStructuredEvent(TEXT("emotion_changed"), SerializeJsonObject(Payload));
}

void UNPCResearchLoggerComponent::HandleDecisionMade(const FString& ActivityID, const FString& Reason)
{
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("first_activity_id"), ActivityID);
	Payload->SetStringField(TEXT("reason"), Reason);
	RecordStructuredEvent(TEXT("plan_accepted"), SerializeJsonObject(Payload));
}

void UNPCResearchLoggerComponent::HandleActivityStarted(const FString& ActivityID, const FString& Reason)
{
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("activity_id"), ActivityID);
	Payload->SetStringField(TEXT("reason"), Reason);
	RecordStructuredEvent(TEXT("activity_started"), SerializeJsonObject(Payload));
}

void UNPCResearchLoggerComponent::HandleActivityFinished(const FString& ActivityID, bool bInterrupted)
{
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("activity_id"), ActivityID);
	Payload->SetBoolField(TEXT("interrupted"), bInterrupted);
	RecordStructuredEvent(TEXT("activity_finished"), SerializeJsonObject(Payload));
}
