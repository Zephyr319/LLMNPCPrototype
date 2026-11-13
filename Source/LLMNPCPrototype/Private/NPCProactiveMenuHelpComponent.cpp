// Copyright Epic Games, Inc. All Rights Reserved.

#include "NPCProactiveMenuHelpComponent.h"

#include "CafeServiceManagerActor.h"
#include "NPCPersonaProfile.h"

#include "LLMClient.h"
#include "UObject/UnrealType.h"

UNPCProactiveMenuHelpComponent::UNPCProactiveMenuHelpComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	bUseStrategyMode = true;
	bStartInStrategyMode = true;
	bProactiveHelpOffered = false;
	HesitationThreshold = 0.5f;
	NPCName = LLMNPCPersona::NameJapanese();
	NPCRole = LLMNPCPersona::CompactDescriptionEnglish();
	PlayerEmotion = TEXT("unknown");
	UserConfusion = TEXT("unknown");
	UserBehaviorHint = TEXT("hesitating near the menu");
	HesitationScore = 0.0f;
	FixedProactiveHelpLine = TEXT("メニューで迷っていらっしゃいますか？ よろしければ、私のおすすめもご案内しますよ。");
	LLMClientRef = nullptr;
}

void UNPCProactiveMenuHelpComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!bStartInStrategyMode)
	{
		return;
	}

	bUseStrategyMode = true;
	AActor* OwnerActor = GetOwner();
	FBoolProperty* BlueprintFlag = OwnerActor
		? FindFProperty<FBoolProperty>(OwnerActor->GetClass(), TEXT("bUseStrategyMode"))
		: nullptr;
	if (BlueprintFlag)
	{
		BlueprintFlag->SetPropertyValue_InContainer(OwnerActor, true);
		UE_LOG(LogTemp, Log, TEXT("[StrategyMode] Started in Strategy Mode. Owner=%s (press M to switch)"), *OwnerActor->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[StrategyMode] Owner has no bUseStrategyMode variable; blueprint default is unchanged."));
	}
}

void UNPCProactiveMenuHelpComponent::TriggerProactiveMenuHelp()
{
	const ACafeServiceManagerActor* ServiceManager = ACafeServiceManagerActor::FindForWorld(this);
	const FCafeServiceSnapshot ServiceSnapshot = ServiceManager
		? ServiceManager->GetServiceSnapshot()
		: FCafeServiceSnapshot();
	if (ServiceSnapshot.bDiningServiceCompleted)
	{
		UE_LOG(LogTemp, Log, TEXT("[ProactiveHelp] suppressed because the current dining service is already complete."));
		return;
	}

	if (ServiceSnapshot.bMealCompletionConfirmationPending)
	{
		UE_LOG(LogTemp, Log, TEXT("[ProactiveHelp] suppressed while meal completion confirmation is pending."));
		return;
	}

	if (bProactiveHelpOffered)
	{
		return;
	}

	if (HesitationScore < HesitationThreshold)
	{
		return;
	}

	bProactiveHelpOffered = true;

	if (!LLMClientRef)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ProactiveHelp] LLMClientRef is null."));
		return;
	}

	if (bUseStrategyMode)
	{
		LLMClientRef->SendStrategyMessage(BuildProactiveHelpPrompt());
		return;
	}

	LLMClientRef->SpeakDirectText(FixedProactiveHelpLine);
}

void UNPCProactiveMenuHelpComponent::ResetProactiveMenuHelp()
{
	bProactiveHelpOffered = false;
}

void UNPCProactiveMenuHelpComponent::SetProactiveUserState(
	const FString& InPlayerEmotion,
	const FString& InUserConfusion,
	const FString& InUserBehaviorHint,
	float InHesitationScore)
{
	PlayerEmotion = InPlayerEmotion;
	UserConfusion = InUserConfusion;
	UserBehaviorHint = InUserBehaviorHint;
	HesitationScore = InHesitationScore;
}

void UNPCProactiveMenuHelpComponent::SetUseStrategyMode(bool bEnabled)
{
	bUseStrategyMode = bEnabled;
}

void UNPCProactiveMenuHelpComponent::SetLLMClientRef(ULLMClient* InLLMClientRef)
{
	LLMClientRef = InLLMClientRef;
}

FString UNPCProactiveMenuHelpComponent::BuildProactiveHelpPrompt() const
{
	return FString::Printf(
		TEXT("NPC Name: %s\n")
		TEXT("NPC Role: %s\n")
		TEXT("\n")
		TEXT("System event:\n")
		TEXT("The player is already in the table/menu area and appears to be hesitating.\n")
		TEXT("This is a proactive help event, not a spoken user request.\n")
		TEXT("\n")
		TEXT("User State:\n")
		TEXT("Player Emotion: %s\n")
		TEXT("User Confusion: %s\n")
		TEXT("User Behavior Hint: %s\n")
		TEXT("Hesitation Score: %.2f\n")
		TEXT("\n")
		TEXT("Service State:\n")
		TEXT("The customer is in the table area.\n")
		TEXT("Treat the customer as seated.\n")
		TEXT("Do not guide the customer to a table again.\n")
		TEXT("Do not say \"まずはお席へご案内します\".\n")
		TEXT("\n")
		TEXT("Task:\n")
		TEXT("Offer proactive menu help.\n")
		TEXT("Recommend one concrete dish if appropriate.\n")
		TEXT("Ask whether the customer needs help or wants to order.\n")
		TEXT("\n")
		TEXT("Expected behavior:\n")
		TEXT("decision: speak\n")
		TEXT("actions:\n")
		TEXT("- type: recommend\n")
		TEXT("  timing: before_speech\n")
		TEXT("  item: カレーライス\n")
		TEXT("  quantity: 0"),
		*NPCName,
		*NPCRole,
		*PlayerEmotion,
		*UserConfusion,
		*UserBehaviorHint,
		HesitationScore);
}
