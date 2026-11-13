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

	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Proactive Menu Help")
	bool bUseStrategyMode;

	// The NPC blueprint keeps its own bUseStrategyMode (toggled by the M key) whose default is false, which
	// means a fresh PIE session starts in plain chat and every strategy mechanism (orders, release, ...)
	// is silently off. When true, BeginPlay sets that blueprint variable to true so M starts in Strategy Mode.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Proactive Menu Help")
	bool bStartInStrategyMode;

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
