// Copyright Epic Games, Inc. All Rights Reserved.

#include "CafePanelSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "CafePlanPanelWidget.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Stats/Stats.h"

namespace
{
	void ToggleCafePlanPanelFromConsole(UWorld* World)
	{
		if (World)
		{
			if (UCafePanelSubsystem* Panels = World->GetSubsystem<UCafePanelSubsystem>())
			{
				Panels->TogglePlanPanel();
			}
		}
	}

	FAutoConsoleCommandWithWorld CafePanelPlanCommand(
		TEXT("Cafe.Panel.Plan"),
		TEXT("Toggle the plan execution list panel (same as the P key)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&ToggleCafePlanPanelFromConsole));

	// True while a text box has keyboard focus: typing a "p" must not toggle a panel.
	bool IsTypingInTextBox()
	{
		if (!FSlateApplication::IsInitialized())
		{
			return false;
		}
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
		return Focused.IsValid() && Focused->GetType().ToString().Contains(TEXT("EditableText"));
	}
}

bool UCafePanelSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UCafePanelSubsystem::Deinitialize()
{
	if (PlanPanel)
	{
		PlanPanel->RemoveFromParent();
		PlanPanel = nullptr;
	}
	bPlanPanelVisible = false;
	Super::Deinitialize();
}

TStatId UCafePanelSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCafePanelSubsystem, STATGROUP_Tickables);
}

void UCafePanelSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (Controller && Controller->InputComponent && BoundController.Get() != Controller)
	{
		BindHotkeys(Controller);
	}

	if (bPlanPanelVisible && PlanPanel)
	{
		RefreshAccumulator += DeltaTime;
		if (RefreshAccumulator >= 0.2f)
		{
			RefreshAccumulator = 0.0f;
			PlanPanel->RefreshContent();
		}
	}
}

void UCafePanelSubsystem::BindHotkeys(APlayerController* Controller)
{
	FInputKeyBinding& PlanBinding = Controller->InputComponent->BindKey(EKeys::P, IE_Pressed, this, &UCafePanelSubsystem::TogglePlanPanel);
	// Consume the key: in the editor, P is "Show Navigation" (the green layer); if the game does not consume it,
	// the editor toggles the navmesh overlay as well.
	PlanBinding.bConsumeInput = true;
	BoundController = Controller;
	UE_LOG(LogTemp, Log, TEXT("[CafePanels] Hotkeys bound on %s (P = plan execution list)."), *Controller->GetName());
}

bool UCafePanelSubsystem::IsPlanPanelVisible() const
{
	return bPlanPanelVisible;
}

void UCafePanelSubsystem::TogglePlanPanel()
{
	if (!bPlanPanelVisible && IsTypingInTextBox())
	{
		return;
	}

	APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!Controller)
	{
		return;
	}

	if (bPlanPanelVisible)
	{
		if (PlanPanel)
		{
			PlanPanel->RemoveFromParent();
		}
		bPlanPanelVisible = false;
		UE_LOG(LogTemp, Log, TEXT("[CafePanels] Plan panel hidden."));
		return;
	}

	if (!PlanPanel)
	{
		PlanPanel = CreateWidget<UCafePlanPanelWidget>(Controller, UCafePlanPanelWidget::StaticClass());
		if (!PlanPanel)
		{
			UE_LOG(LogTemp, Warning, TEXT("[CafePanels] Could not create the plan panel widget."));
			return;
		}
		// Display only: never blocks the crosshair ray, the mouse or the keyboard.
		PlanPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	PlanPanel->AddToViewport(50);
	// Right edge of the screen, tall and narrow. Plain top-left placement computed from the viewport size
	// (pixels): the panel is UCafePlanPanelWidget::PanelWidth Slate units wide.
	const FVector2D ViewportPixels = UWidgetLayoutLibrary::GetViewportSize(this);
	const float DpiScale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), 0.1f);
	const FVector2D PanelPosition(
		ViewportPixels.X - (UCafePlanPanelWidget::PanelWidth + 24.0f) * DpiScale,
		50.0 * DpiScale);
	PlanPanel->SetPositionInViewport(PanelPosition, true);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[CafePanels] Plan panel placed at (%.0f, %.0f) px. Viewport=%.0fx%.0f DpiScale=%.2f"),
		PanelPosition.X,
		PanelPosition.Y,
		ViewportPixels.X,
		ViewportPixels.Y,
		DpiScale);
	PlanPanel->RefreshContent();
	RefreshAccumulator = 0.0f;
	bPlanPanelVisible = true;
	UE_LOG(LogTemp, Log, TEXT("[CafePanels] Plan panel shown."));
}
