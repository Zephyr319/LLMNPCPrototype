// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CafePlanPanelWidget.generated.h"

class SBox;
class STextBlock;
class SVerticalBox;

// Code-only debug/research panel: the maid's current plan, its steps, what is on her mind and the recent plan
// events. Built with Slate in RebuildWidget (no Widget Blueprint asset) and refreshed by UCafePanelSubsystem.
UCLASS()
class LLMNPCPROTOTYPE_API UCafePlanPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Width of the panel in Slate units (used to place it against the right edge of the screen).
	static constexpr float PanelWidth = 560.0f;

	// Rebuilds the visible lines from the autonomy component. Cheap; called a few times per second while shown.
	void RefreshContent();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	// One reusable display row. Rows are created once and updated in place: rebuilding every text block on each
	// refresh made each new row lay out unwrapped for one frame and then wrap, so the panel flickered and jumped.
	struct FPanelRow
	{
		TSharedPtr<SBox> Box;
		TSharedPtr<STextBlock> Text;
		FString LastText;
		int32 LastFontSize = -1;
		float LastTopPadding = -1.0f;
	};

	// Updates (creating if needed) the row at RowIndex and makes it visible.
	void SetRow(int32 RowIndex, const FString& Text, const FLinearColor& Color, int32 FontSize, float TopPadding);

	TSharedPtr<SVerticalBox> ContentBox;
	TArray<FPanelRow> Rows;
};
