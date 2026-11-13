// Copyright Epic Games, Inc. All Rights Reserved.

// Console commands for repeatable interruption trials. They only mark trials, switch the experiment
// condition and inject player text through the normal dialogue path; they hold no state of their own.

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NPCAutonomyComponent.h"
#include "NPCDialogueUIControllerComponent.h"
#include "NPCResearchLoggerComponent.h"
#include "UObject/UObjectIterator.h"

namespace
{
	void PrintExperimentLine(const FString& Line)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Experiment] %s"), *Line);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Orange, Line);
		}
	}

	bool ParseExperimentCondition(const FString& Text, ENPCExperimentCondition& OutCondition)
	{
		const FString Lower = Text.TrimStartAndEnd().ToLower();
		if (Lower == TEXT("baseline"))
		{
			OutCondition = ENPCExperimentCondition::Baseline;
			return true;
		}
		if (Lower == TEXT("full"))
		{
			OutCondition = ENPCExperimentCondition::Full;
			return true;
		}
		if (Lower == TEXT("custom"))
		{
			OutCondition = ENPCExperimentCondition::Custom;
			return true;
		}
		return false;
	}

	UNPCDialogueUIControllerComponent* FindDialogueController(UWorld* World)
	{
		for (TObjectIterator<UNPCDialogueUIControllerComponent> It; It; ++It)
		{
			UNPCDialogueUIControllerComponent* Controller = *It;
			if (Controller && Controller->GetWorld() == World && !Controller->IsTemplate())
			{
				return Controller;
			}
		}
		return nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GCafeConditionCommand(
	TEXT("Cafe.Condition"),
	TEXT("Cafe.Condition <baseline|full|custom> : switch the experiment condition (no argument prints the current switches)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UNPCAutonomyComponent* Autonomy = UNPCAutonomyComponent::GetPrimaryAutonomyComponent();
		if (!Autonomy)
		{
			PrintExperimentLine(TEXT("autonomy component not found (start PIE first)"));
			return;
		}
		if (Args.Num() >= 1)
		{
			ENPCExperimentCondition Condition = ENPCExperimentCondition::Custom;
			if (!ParseExperimentCondition(Args[0], Condition))
			{
				PrintExperimentLine(TEXT("usage: Cafe.Condition <baseline|full|custom>"));
				return;
			}
			Autonomy->ApplyExperimentCondition(Condition);
		}
		PrintExperimentLine(Autonomy->DescribeExperimentSwitchesJson());
	}));

static FAutoConsoleCommandWithWorldAndArgs GCafeTrialBeginCommand(
	TEXT("Cafe.Trial.Begin"),
	TEXT("Cafe.Trial.Begin <TrialID> [baseline|full|custom] : mark the start of a trial; later log events carry this id."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UNPCAutonomyComponent* Autonomy = UNPCAutonomyComponent::GetPrimaryAutonomyComponent();
		UNPCResearchLoggerComponent* Logger = UNPCResearchLoggerComponent::GetActiveLogger();
		if (!Autonomy || !Logger || Args.Num() < 1)
		{
			PrintExperimentLine(TEXT("usage: Cafe.Trial.Begin <TrialID> [baseline|full|custom] (PIE must be running)"));
			return;
		}
		ENPCExperimentCondition Condition = Autonomy->ExperimentCondition;
		if (Args.Num() >= 2 && !ParseExperimentCondition(Args[1], Condition))
		{
			PrintExperimentLine(TEXT("unknown condition; use baseline, full or custom"));
			return;
		}
		Autonomy->ExperimentCondition = Condition;
		Logger->BeginTrial(Args[0], Autonomy->GetExperimentConditionName());
		// Applied after the trial id is set so the switch values are logged inside the trial.
		Autonomy->ApplyExperimentCondition(Condition);
		PrintExperimentLine(FString::Printf(TEXT("trial %s started, condition=%s"), *Args[0], *Autonomy->GetExperimentConditionName()));
	}));

static FAutoConsoleCommandWithWorldAndArgs GCafeTrialEndCommand(
	TEXT("Cafe.Trial.End"),
	TEXT("Cafe.Trial.End [note] : mark the end of the current trial."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UNPCResearchLoggerComponent* Logger = UNPCResearchLoggerComponent::GetActiveLogger();
		if (!Logger || Logger->TrialID.IsEmpty())
		{
			PrintExperimentLine(TEXT("no trial is running"));
			return;
		}
		const FString EndedTrial = Logger->TrialID;
		Logger->EndTrial(FString::Join(Args, TEXT(" ")));
		PrintExperimentLine(FString::Printf(TEXT("trial %s ended"), *EndedTrial));
	}));

static FAutoConsoleCommandWithWorldAndArgs GCafeSayCommand(
	TEXT("Cafe.Say"),
	TEXT("Cafe.Say <text> : send a player utterance through the same path as the dialogue Send button."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const FString Text = FString::Join(Args, TEXT(" ")).TrimStartAndEnd();
		UNPCDialogueUIControllerComponent* Controller = FindDialogueController(World);
		if (Text.IsEmpty() || !Controller)
		{
			PrintExperimentLine(TEXT("usage: Cafe.Say <text> (PIE must be running and the dialogue UI initialised)"));
			return;
		}
		PrintExperimentLine(FString::Printf(TEXT("say: %s"), *Text));
		Controller->SubmitPlayerMessage(Text);
	}));
