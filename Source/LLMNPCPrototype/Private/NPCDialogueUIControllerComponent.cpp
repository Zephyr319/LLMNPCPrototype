// Copyright Epic Games, Inc. All Rights Reserved.

#include "NPCDialogueUIControllerComponent.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LLMDialogueWidgetBase.h"

UNPCDialogueUIControllerComponent::UNPCDialogueUIControllerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	DialogueWidget = nullptr;
	CurrentMode = ENPCDialogueUIMode::Hidden;
	DefaultSubtitleDuration = 4.0f;
	CachedPlayerController = nullptr;
	LastDisplayedResponseText = TEXT("");
}

void UNPCDialogueUIControllerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearSubtitleHideTimer();

	Super::EndPlay(EndPlayReason);
}

void UNPCDialogueUIControllerComponent::InitializeDialogueUI(APlayerController* PlayerController)
{
	CachedPlayerController = PlayerController;

	if (!CachedPlayerController && GetWorld())
	{
		CachedPlayerController = GetWorld()->GetFirstPlayerController();
	}

	if (EnsureWidgetCreated())
	{
		HideDialogue();
	}
}

void UNPCDialogueUIControllerComponent::ToggleDialogue()
{
	if (CurrentMode == ENPCDialogueUIMode::FullDialogue)
	{
		UE_LOG(LogTemp, Log, TEXT("[DialogueUI] ToggleDialogue: FullDialogue -> ResponseOnly"));
		ShowResponseOnly();
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[DialogueUI] ToggleDialogue: ResponseOnly/Hidden -> FullDialogue"));
	UE_LOG(LogTemp, Log, TEXT("[DialogueUI] Runtime widget rebuild disabled for stability"));
	ShowFullDialogue();
}

void UNPCDialogueUIControllerComponent::ShowFullDialogue()
{
	if (!EnsureWidgetCreated())
	{
		return;
	}

	ClearSubtitleHideTimer();
	CurrentMode = ENPCDialogueUIMode::FullDialogue;

	if (DialogueWidget)
	{
		DialogueWidget->SetVisibility(ESlateVisibility::Visible);
		DialogueWidget->SetIsEnabled(true);
	}

	if (ULLMDialogueWidgetBase* DialogueWidgetBase = GetDialogueWidgetBase())
	{
		DialogueWidgetBase->ShowFullDialogueMode();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			this,
			&UNPCDialogueUIControllerComponent::ShowFullDialogueInputControlsDelayed);
	}

	ApplyGameAndUIInputMode();
}

void UNPCDialogueUIControllerComponent::ShowResponseOnly()
{
	UE_LOG(LogTemp, Log, TEXT("[DialogueUI] ShowResponseOnly called"));

	if (!EnsureWidgetCreated())
	{
		return;
	}

	ClearSubtitleHideTimer();
	CurrentMode = ENPCDialogueUIMode::ResponseOnly;

	if (DialogueWidget)
	{
		DialogueWidget->SetVisibility(ESlateVisibility::Visible);
		DialogueWidget->SetIsEnabled(true);
	}

	if (ULLMDialogueWidgetBase* DialogueWidgetBase = GetDialogueWidgetBase())
	{
		DialogueWidgetBase->ShowResponseOnlyMode();
	}

	ApplyGameOnlyInputMode();
}

void UNPCDialogueUIControllerComponent::HideDialogue()
{
	ClearSubtitleHideTimer();
	CurrentMode = ENPCDialogueUIMode::Hidden;

	if (DialogueWidget)
	{
		DialogueWidget->SetVisibility(ESlateVisibility::Hidden);
	}

	if (ULLMDialogueWidgetBase* DialogueWidgetBase = GetDialogueWidgetBase())
	{
		DialogueWidgetBase->HideDialogueMode();
	}

	ApplyGameOnlyInputMode();
}

void UNPCDialogueUIControllerComponent::ShowSubtitle(const FString& Text, float Duration)
{
	const TCHAR* ModeText = TEXT("Unknown");
	switch (CurrentMode)
	{
	case ENPCDialogueUIMode::Hidden:
		ModeText = TEXT("Hidden");
		break;
	case ENPCDialogueUIMode::SubtitleOnly:
		ModeText = TEXT("SubtitleOnly");
		break;
	case ENPCDialogueUIMode::FullDialogue:
		ModeText = TEXT("FullDialogue");
		break;
	case ENPCDialogueUIMode::ResponseOnly:
		ModeText = TEXT("ResponseOnly");
		break;
	default:
		break;
	}

	UE_LOG(LogTemp, Warning, TEXT("[DialogueUI] ShowSubtitle called. Mode=%s, Text=%s"), ModeText, *Text);
	(void)Duration;

	const FString TrimmedText = Text.TrimStartAndEnd();
	if (TrimmedText.IsEmpty())
	{
		return;
	}

	LastDisplayedResponseText = TrimmedText;

	if (!EnsureWidgetCreated())
	{
		return;
	}

	if (CurrentMode == ENPCDialogueUIMode::FullDialogue)
	{
		ClearSubtitleHideTimer();

		if (ULLMDialogueWidgetBase* DialogueWidgetBase = GetDialogueWidgetBase())
		{
			DialogueWidgetBase->SetResponseText(TrimmedText);
		}
		return;
	}

	if (ULLMDialogueWidgetBase* DialogueWidgetBase = GetDialogueWidgetBase())
	{
		DialogueWidgetBase->SetResponseText(TrimmedText);
	}

	ShowResponseOnly();
}

void UNPCDialogueUIControllerComponent::SetDialogueWidget(UUserWidget* Widget)
{
	if (DialogueWidget == Widget)
	{
		return;
	}

	ClearSubtitleHideTimer();

	if (ULLMDialogueWidgetBase* OldDialogueWidgetBase = GetDialogueWidgetBase())
	{
		OldDialogueWidgetBase->OnSendMessageRequested.RemoveDynamic(
			this,
			&UNPCDialogueUIControllerComponent::HandleWidgetSendMessageRequested);
	}

	DialogueWidget = Widget;
	BindDialogueWidgetEvents();

	if (DialogueWidget)
	{
		EnsureWidgetInViewport();
		DialogueWidget->SetVisibility(ESlateVisibility::Hidden);
		DialogueWidget->SetIsEnabled(true);
		CurrentMode = ENPCDialogueUIMode::Hidden;
	}
}

UUserWidget* UNPCDialogueUIControllerComponent::GetDialogueWidget() const
{
	return DialogueWidget;
}

ENPCDialogueUIMode UNPCDialogueUIControllerComponent::GetCurrentMode() const
{
	return CurrentMode;
}

bool UNPCDialogueUIControllerComponent::IsFullDialogueOpen() const
{
	return CurrentMode == ENPCDialogueUIMode::FullDialogue;
}

bool UNPCDialogueUIControllerComponent::EnsureWidgetCreated()
{
	if (!CachedPlayerController && GetWorld())
	{
		CachedPlayerController = GetWorld()->GetFirstPlayerController();
	}

	if (!DialogueWidget)
	{
		if (!CachedPlayerController || !DialogueWidgetClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("[DialogueUI] Cannot create dialogue widget: missing player controller or widget class"));
			return false;
		}

		DialogueWidget = CreateWidget<UUserWidget>(CachedPlayerController, DialogueWidgetClass);
		if (!DialogueWidget)
		{
			UE_LOG(LogTemp, Warning, TEXT("[DialogueUI] Failed to create dialogue widget"));
			return false;
		}

		BindDialogueWidgetEvents();
		DialogueWidget->SetVisibility(ESlateVisibility::Hidden);
		DialogueWidget->SetIsEnabled(true);
		CurrentMode = ENPCDialogueUIMode::Hidden;
	}

	EnsureWidgetInViewport();
	return true;
}

void UNPCDialogueUIControllerComponent::EnsureWidgetInViewport()
{
	if (DialogueWidget && !DialogueWidget->IsInViewport())
	{
		DialogueWidget->AddToViewport(1000);
		DialogueWidget->SetIsEnabled(true);
	}
}

void UNPCDialogueUIControllerComponent::BindDialogueWidgetEvents()
{
	if (ULLMDialogueWidgetBase* DialogueWidgetBase = GetDialogueWidgetBase())
	{
		DialogueWidgetBase->OnSendMessageRequested.RemoveDynamic(
			this,
			&UNPCDialogueUIControllerComponent::HandleWidgetSendMessageRequested);
		DialogueWidgetBase->OnSendMessageRequested.AddDynamic(
			this,
			&UNPCDialogueUIControllerComponent::HandleWidgetSendMessageRequested);
	}
}

void UNPCDialogueUIControllerComponent::ClearSubtitleHideTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SubtitleHideTimerHandle);
	}
}

void UNPCDialogueUIControllerComponent::ApplyGameAndUIInputMode()
{
	if (!CachedPlayerController)
	{
		return;
	}

	FInputModeGameAndUI InputMode;
	if (DialogueWidget)
	{
		InputMode.SetWidgetToFocus(DialogueWidget->TakeWidget());
	}
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);

	CachedPlayerController->bShowMouseCursor = true;
	CachedPlayerController->bEnableClickEvents = true;
	CachedPlayerController->bEnableMouseOverEvents = true;
	CachedPlayerController->SetInputMode(InputMode);
}

void UNPCDialogueUIControllerComponent::ApplyGameOnlyInputMode()
{
	if (!CachedPlayerController)
	{
		return;
	}

	FInputModeGameOnly InputMode;
	CachedPlayerController->SetInputMode(InputMode);
	CachedPlayerController->bShowMouseCursor = false;
}

ULLMDialogueWidgetBase* UNPCDialogueUIControllerComponent::GetDialogueWidgetBase() const
{
	return Cast<ULLMDialogueWidgetBase>(DialogueWidget);
}

void UNPCDialogueUIControllerComponent::HandleWidgetSendMessageRequested(const FString& Message)
{
	const FString TrimmedMessage = Message.TrimStartAndEnd();
	if (TrimmedMessage.IsEmpty())
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Controller received dialogue message: %s"), *TrimmedMessage);
	OnDialogueSendRequested.Broadcast(TrimmedMessage);
	UE_LOG(LogTemp, Log, TEXT("[DialogueUI] Rebuild after send disabled for stability"));

	if (UWorld* World = GetWorld())
	{
		UE_LOG(LogTemp, Log, TEXT("[DialogueUI] Send received, switching to ResponseOnly next tick"));
		World->GetTimerManager().SetTimerForNextTick(
			this,
			&UNPCDialogueUIControllerComponent::ShowResponseOnly);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[DialogueUI] Cannot schedule ResponseOnly switch after send: world is null"));
	}
}

void UNPCDialogueUIControllerComponent::HandleSubtitleHideTimerFinished()
{
	if (CurrentMode == ENPCDialogueUIMode::SubtitleOnly)
	{
		ShowResponseOnly();
	}
}

void UNPCDialogueUIControllerComponent::ShowFullDialogueInputControlsDelayed()
{
	if (CurrentMode != ENPCDialogueUIMode::FullDialogue)
	{
		return;
	}

	if (ULLMDialogueWidgetBase* DialogueWidgetBase = GetDialogueWidgetBase())
	{
		DialogueWidgetBase->ShowFullDialogueInputControlsDelayed();
	}
}
