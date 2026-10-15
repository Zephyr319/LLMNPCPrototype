// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCProactiveMenuHelpComponent.generated.h"

class ULLMClient;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class LLMNPCPROTOTYPE_API UNPCProactiveMenuHelpComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCProactiveMenuHelpComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Proactive Menu Help")
	bool bUseStrategyMode;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Proactive Menu Help")
	bool bProactiveHelpOffered;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Proactive Menu Help")
	float HesitationThreshold;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Proactive Menu Help")
	FString NPCName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Proactive Menu Help")
	FString NPCRole;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Proactive Menu Help")
	FString PlayerEmotion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Proactive Menu Help")
	FString UserConfusion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Proactive Menu Help")
	FString UserBehaviorHint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Proactive Menu Help")
	float HesitationScore;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Proactive Menu Help")
	FString FixedProactiveHelpLine;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Proactive Menu Help")
	ULLMClient* LLMClientRef;

	UFUNCTION(BlueprintCallable, Category = "NPC Proactive Menu Help")
	void TriggerProactiveMenuHelp();

	UFUNCTION(BlueprintCallable, Category = "NPC Proactive Menu Help")
	void ResetProactiveMenuHelp();

	UFUNCTION(BlueprintCallable, Category = "NPC Proactive Menu Help")
	void SetProactiveUserState(
		const FString& InPlayerEmotion,
		const FString& InUserConfusion,
		const FString& InUserBehaviorHint,
		float InHesitationScore);

	UFUNCTION(BlueprintCallable, Category = "NPC Proactive Menu Help")
	void SetUseStrategyMode(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "NPC Proactive Menu Help")
	void SetLLMClientRef(ULLMClient* InLLMClientRef);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Proactive Menu Help")
	FString BuildProactiveHelpPrompt() const;
};
