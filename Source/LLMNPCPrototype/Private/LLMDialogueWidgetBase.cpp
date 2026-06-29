// Copyright Epic Games, Inc. All Rights Reserved.

#include "LLMDialogueWidgetBase.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"

ULLMDialogueWidgetBase::ULLMDialogueWidgetBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ResponseWrapTextAt = 800.0f;
}

void ULLMDialogueWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	ApplyResponseTextWrapSettings();

	UE_LOG(LogTemp, Log, TEXT("[DialogueUI] ResponseText bound: %s"), ResponseText ? TEXT("true") : TEXT("false"));
	UE_LOG(LogTemp, Log, TEXT("[DialogueUI] InputTextBox bound: %s"), InputTextBox ? TEXT("true") : TEXT("false"));
	UE_LOG(LogTemp, Log, TEXT("[DialogueUI] SendButton bound: %s"), SendButton ? TEXT("true") : TEXT("false"));

	if (SendButton)
	{
		SendButton->OnClicked.RemoveDynamic(this, &ULLMDialogueWidgetBase::HandleSendButtonClicked);
		SendButton->OnClicked.AddDynamic(this, &ULLMDialogueWidgetBase::HandleSendButtonClicked);
		UE_LOG(LogTemp, Log, TEXT("[DialogueUI] SendButton OnClicked bound: true"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[DialogueUI] SendButton OnClicked bound: false"));
	}
}

void ULLMDialogueWidgetBase::ShowSubtitleText(const FString& Text)
{
	SetVisibility(ESlateVisibility::Visible);
	SetResponseText(Text);

	if (ResponseText)
	{
		ResponseText->SetVisibility(ESlateVisibility::Visible);
	}

	SetInputControlsVisibility(ESlateVisibility::Hidden);
}

void ULLMDialogueWidgetBase::ShowFullDialogueMode()
{
	SetVisibility(ESlateVisibility::Visible);
	SetIsEnabled(true);

	if (ResponseText)
	{
		ResponseText->SetVisibility(ESlateVisibility::Visible);
	}

	if (InputTextBox)
	{
		InputTextBox->SetVisibility(ESlateVisibility::Collapsed);
		InputTextBox->SetIsEnabled(true);
		InputTextBox->SetIsReadOnly(false);
		InputTextBox->SetText(FText::GetEmpty());

		const FString TextAfterClear = InputTextBox->GetText().ToString();
		UE_LOG(LogTemp, Log, TEXT("[DialogueUI] ShowFullDialogue pre-clear input text: '%s'"), *TextAfterClear);
	}

	if (SendButton)
	{
		SendButton->SetVisibility(ESlateVisibility::Collapsed);
		SendButton->SetIsEnabled(true);
	}
}

void ULLMDialogueWidgetBase::ShowFullDialogueInputControlsDelayed()
{
	if (InputTextBox)
	{
		InputTextBox->SetText(FText::GetEmpty());
		InputTextBox->SetVisibility(ESlateVisibility::Visible);
		InputTextBox->SetIsEnabled(true);
		InputTextBox->SetIsReadOnly(false);
		InputTextBox->SetKeyboardFocus();

		const FString TextAfterClear = InputTextBox->GetText().ToString();
		UE_LOG(LogTemp, Log, TEXT("[DialogueUI] ShowFullDialogue delayed visible input text: '%s'"), *TextAfterClear);
	}

	if (SendButton)
	{
		SendButton->SetVisibility(ESlateVisibility::Visible);
		SendButton->SetIsEnabled(true);
	}
}

void ULLMDialogueWidgetBase::ShowResponseOnlyMode()
{
	SetVisibility(ESlateVisibility::Visible);
	SetIsEnabled(true);

	if (ResponseText)
	{
		ResponseText->SetVisibility(ESlateVisibility::Visible);
	}

	SetInputControlsVisibility(ESlateVisibility::Collapsed);
}

void ULLMDialogueWidgetBase::HideDialogueMode()
{
	SetVisibility(ESlateVisibility::Hidden);
}

void ULLMDialogueWidgetBase::SetResponseText(const FString& Text)
{
	ApplyResponseTextWrapSettings();

	if (ResponseText)
	{
		ResponseText->SetText(FText::FromString(Text));
	}
}

FString ULLMDialogueWidgetBase::GetInputMessage() const
{
	return InputTextBox ? InputTextBox->GetText().ToString() : FString();
}

void ULLMDialogueWidgetBase::ClearInputMessage()
{
	ClearInputTextBoxSimple();
}

void ULLMDialogueWidgetBase::ClearInputTextBox()
{
	ClearInputTextBoxSimple();
}

void ULLMDialogueWidgetBase::ClearInputTextBoxSimple()
{
	if (!InputTextBox)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DialogueUI] ClearInputTextBoxSimple failed: InputTextBox is null"));
		return;
	}

	InputTextBox->SetText(FText::GetEmpty());
	const FString TextAfterClear = InputTextBox->GetText().ToString();
	UE_LOG(LogTemp, Log, TEXT("[DialogueUI] ClearInputTextBoxSimple after clear: '%s'"), *TextAfterClear);
}

void ULLMDialogueWidgetBase::HandleSendButtonClicked()
{
	UE_LOG(LogTemp, Log, TEXT("Dialogue SendButton clicked"));

	if (!InputTextBox)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DialogueUI] InputTextBox is not bound; cannot send dialogue message"));
		return;
	}

	const FString RawMessage = InputTextBox->GetText().ToString();
	UE_LOG(LogTemp, Log, TEXT("[DialogueUI] Input text: %s"), *RawMessage);

	const FString Message = RawMessage.TrimStartAndEnd();
	if (Message.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DialogueUI] InputTextBox is empty; dialogue message not sent"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[DialogueUI] Message before send: '%s'"), *Message);

	ClearInputTextBoxSimple();
	OnSendMessageRequested.Broadcast(Message);
}

void ULLMDialogueWidgetBase::ApplyResponseTextWrapSettings() const
{
	if (!ResponseText)
	{
		return;
	}

	ResponseText->SetAutoWrapText(true);
	ResponseText->SetWrapTextAt(ResponseWrapTextAt);
}

void ULLMDialogueWidgetBase::SetInputControlsVisibility(ESlateVisibility InVisibility) const
{
	if (InputTextBox)
	{
		InputTextBox->SetVisibility(InVisibility);
	}

	if (SendButton)
	{
		SendButton->SetVisibility(InVisibility);
	}
}
