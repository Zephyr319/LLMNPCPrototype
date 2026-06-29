// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LLMDialogueWidgetBase.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLLMDialogueWidgetSendMessageRequested, const FString&, Message);

UCLASS(Blueprintable)
class LLMNPCPROTOTYPE_API ULLMDialogueWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	ULLMDialogueWidgetBase(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintAssignable, Category = "NPC Dialogue UI")
	FLLMDialogueWidgetSendMessageRequested OnSendMessageRequested;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Dialogue UI")
	float ResponseWrapTextAt;

	UFUNCTION(BlueprintCallable, Category = "NPC Dialogue UI")
	void ShowSubtitleText(const FString& Text);

	UFUNCTION(BlueprintCallable, Category = "NPC Dialogue UI")
	void ShowFullDialogueMode();

	UFUNCTION(BlueprintCallable, Category = "NPC Dialogue UI")
	void ShowFullDialogueInputControlsDelayed();

	UFUNCTION(BlueprintCallable, Category = "NPC Dialogue UI")
	void ShowResponseOnlyMode();

	UFUNCTION(BlueprintCallable, Category = "NPC Dialogue UI")
	void HideDialogueMode();

	UFUNCTION(BlueprintCallable, Category = "NPC Dialogue UI")
	void SetResponseText(const FString& Text);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Dialogue UI")
	FString GetInputMessage() const;

	UFUNCTION(BlueprintCallable, Category = "NPC Dialogue UI")
	void ClearInputMessage();

	UFUNCTION(BlueprintCallable, Category = "NPC Dialogue UI")
	void ClearInputTextBox();

	UFUNCTION(BlueprintCallable, Category = "NPC Dialogue UI")
	void ClearInputTextBoxSimple();

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "NPC Dialogue UI")
	UTextBlock* ResponseText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "NPC Dialogue UI")
	UEditableTextBox* InputTextBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "NPC Dialogue UI")
	UButton* SendButton;

private:
	UFUNCTION()
	void HandleSendButtonClicked();

	void ApplyResponseTextWrapSettings() const;
	void SetInputControlsVisibility(ESlateVisibility InVisibility) const;
};
