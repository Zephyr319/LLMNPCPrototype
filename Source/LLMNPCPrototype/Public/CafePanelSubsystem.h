// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CafePanelSubsystem.generated.h"

class APlayerController;
class UCafePlanPanelWidget;

// Owns the hotkey-toggled UI panels (research/debug views). It binds its keys on the first player controller
// at runtime, so no level actor or Blueprint change is needed. Add a panel = add a widget class, a key and a
// Toggle function here.
//   P  - plan execution list (also: console command Cafe.Panel.Plan)
UCLASS()
class LLMNPCPROTOTYPE_API UCafePanelSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintCallable, Category = "Cafe Panels")
	void TogglePlanPanel();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Cafe Panels")
	bool IsPlanPanelVisible() const;

private:
	void BindHotkeys(APlayerController* Controller);

	UPROPERTY(Transient)
	UCafePlanPanelWidget* PlanPanel = nullptr;

	TWeakObjectPtr<APlayerController> BoundController;
	bool bPlanPanelVisible = false;
	float RefreshAccumulator = 0.0f;
};
