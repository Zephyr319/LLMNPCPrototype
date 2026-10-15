// Copyright Epic Games, Inc. All Rights Reserved.

#include "MenuUIControllerComponent.h"

#include "Blueprint/UserWidget.h"
#include "MenuDisplayWidgetBase.h"

TWeakObjectPtr<UMenuUIControllerComponent> UMenuUIControllerComponent::ActiveMenuUIController;

UMenuUIControllerComponent::UMenuUIControllerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	MenuWidget = nullptr;
	bMenuOpen = false;
	MenuText = TEXT("カレーライス\nラーメン\nサラダ\nコーヒー");
}

void UMenuUIControllerComponent::ToggleMenuUI(APlayerController* PlayerController)
{
	if (bMenuOpen)
	{
		HideMenuUI();
		return;
	}

	ShowMenuUI(PlayerController);
}

void UMenuUIControllerComponent::ShowMenuUI(APlayerController* PlayerController)
{
	if (!PlayerController)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MenuUI] PlayerController is null."));
		return;
	}

	if (!MenuWidget)
	{
		if (!MenuWidgetClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MenuUI] MenuWidgetClass is null."));
			return;
		}

		MenuWidget = CreateWidget<UUserWidget>(PlayerController, MenuWidgetClass);
		if (!MenuWidget)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MenuUI] Failed to create menu widget."));
			return;
		}

		MenuWidget->AddToViewport(900);
		ApplyMenuTextToWidget();
	}

	if (ActiveMenuUIController.IsValid() && ActiveMenuUIController.Get() != this)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MenuUI] Another menu was open. Closing previous menu first."));
		ActiveMenuUIController->HideMenuUI();
	}

	MenuWidget->SetVisibility(ESlateVisibility::Visible);
	bMenuOpen = true;
	ActiveMenuUIController = this;
	UE_LOG(LogTemp, Warning, TEXT("[MenuUI] Menu opened."));
}

void UMenuUIControllerComponent::HideMenuUI()
{
	if (MenuWidget)
	{
		MenuWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	bMenuOpen = false;
	if (ActiveMenuUIController.Get() == this)
	{
		ActiveMenuUIController.Reset();
	}

	UE_LOG(LogTemp, Warning, TEXT("[MenuUI] Menu closed."));
}

bool UMenuUIControllerComponent::IsMenuOpen() const
{
	return bMenuOpen;
}

bool UMenuUIControllerComponent::IsAnyMenuOpen()
{
	if (!ActiveMenuUIController.IsValid())
	{
		ActiveMenuUIController.Reset();
		return false;
	}

	if (!ActiveMenuUIController->bMenuOpen)
	{
		ActiveMenuUIController.Reset();
		return false;
	}

	return true;
}

void UMenuUIControllerComponent::HideActiveMenuUI()
{
	if (!ActiveMenuUIController.IsValid())
	{
		ActiveMenuUIController.Reset();
		return;
	}

	UMenuUIControllerComponent* ActiveMenu = ActiveMenuUIController.Get();
	if (!ActiveMenu || !ActiveMenu->bMenuOpen)
	{
		ActiveMenuUIController.Reset();
		return;
	}

	ActiveMenu->HideMenuUI();
	UE_LOG(LogTemp, Warning, TEXT("[MenuUI] Active menu closed by global toggle."));
}

void UMenuUIControllerComponent::SetMenuText(const FString& InMenuText)
{
	MenuText = InMenuText;
	ApplyMenuTextToWidget();
}

void UMenuUIControllerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ActiveMenuUIController.Get() == this)
	{
		ActiveMenuUIController.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void UMenuUIControllerComponent::ApplyMenuTextToWidget() const
{
	if (UMenuDisplayWidgetBase* MenuDisplayWidget = Cast<UMenuDisplayWidgetBase>(MenuWidget))
	{
		MenuDisplayWidget->SetMenuText(MenuText);
	}
}
