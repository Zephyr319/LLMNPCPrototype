// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PlantMoistureBarWidget.generated.h"

class SProgressBar;

// Code-only world-space bar shown above a waterable plant. No Widget Blueprint asset is needed: the Slate
// content is built in RebuildWidget, and AWaterablePlantActor pushes the values in.
UCLASS()
class LLMNPCPROTOTYPE_API UPlantMoistureBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Fraction 0..1 and the fill colour. Safe to call before the Slate widget exists (applied on rebuild).
	void SetBarState(float NewFraction01, const FLinearColor& NewFillColor);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TSharedPtr<SProgressBar> ProgressBarWidget;
	float CurrentFraction = 1.0f;
	FLinearColor CurrentFillColor = FLinearColor(0.25f, 0.8f, 0.3f, 1.0f);
};
