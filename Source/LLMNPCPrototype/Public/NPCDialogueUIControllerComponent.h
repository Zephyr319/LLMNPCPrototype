// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "NPCDialogueUIControllerComponent.generated.h"

class APlayerController;
class UUserWidget;

UENUM(BlueprintType)
enum class ENPCDialogueUIMode : uint8
{
	Hidden,
	SubtitleOnly,
	FullDialogue,
	ResponseOnly
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNPCDialogueUISendRequestedSignature, const FString&, Message);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class LLMNPCPROTOTYPE_API UNPCDialogueUIControllerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCDialogueUIControllerComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Dialogue UI")
	TSubclassOf<UUserWidget> DialogueWidgetClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Dialogue UI")
	UUserWidget* DialogueWidget;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Dialogue UI")
	ENPCDialogueUIMode CurrentMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Dialogue UI")
	float DefaultSubtitleDuration;

	UPROPERTY(BlueprintAssignable, Category = "NPC Dialogue UI")
	FNPCDialogueUISendRequestedSignature OnDialogueSendRequested;

	UFUNCTION(BlueprintCallable, Category = "NPC Dialogue UI")
	void InitializeDialogueUI(APlayerController* PlayerController);

	UFUNCTION(BlueprintCallable, Category = "NPC Dialogue UI")
	void ToggleDialogue();

	UFUNCTION(BlueprintCallable, Category = "NPC Dialogue UI")
	void ShowFullDialogue();

	UFUNCTION(BlueprintCallable, Category = "NPC Dialogue UI")
	void ShowResponseOnly();

	UFUNCTION(BlueprintCallable, Category = "NPC Dialogue UI")
	void HideDialogue();

	UFUNCTION(BlueprintCallable, Category = "NPC Dialogue UI")
	void ShowSubtitle(const FString& Text, float Duration = -1.0f);

	UFUNCTION(BlueprintCallable, Category = "NPC Dialogue UI")
	void SetDialogueWidget(UUserWidget* Widget);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Dialogue UI")
	UUserWidget* GetDialogueWidget() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Dialogue UI")
	ENPCDialogueUIMode GetCurrentMode() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Dialogue UI")
	bool IsFullDialogueOpen() const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	APlayerController* CachedPlayerController;

	UPROPERTY(Transient)
	FString LastDisplayedResponseText;

	FTimerHandle SubtitleHideTimerHandle;

	bool EnsureWidgetCreated();
	void EnsureWidgetInViewport();
	void BindDialogueWidgetEvents();
	void ClearSubtitleHideTimer();
	void ApplyGameAndUIInputMode();
	void ApplyGameOnlyInputMode();
	void ShowFullDialogueInputControlsDelayed();

	class ULLMDialogueWidgetBase* GetDialogueWidgetBase() const;

	UFUNCTION()
	void HandleWidgetSendMessageRequested(const FString& Message);

	UFUNCTION()
	void HandleSubtitleHideTimerFinished();
};
