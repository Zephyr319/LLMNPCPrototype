// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlantMoistureBarWidget.h"

#include "Styling/CoreStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Notifications/SProgressBar.h"

void UPlantMoistureBarWidget::SetBarState(float NewFraction01, const FLinearColor& NewFillColor)
{
	CurrentFraction = FMath::Clamp(NewFraction01, 0.0f, 1.0f);
	CurrentFillColor = NewFillColor;
	if (ProgressBarWidget.IsValid())
	{
		ProgressBarWidget->SetPercent(TOptional<float>(CurrentFraction));
		ProgressBarWidget->SetFillColorAndOpacity(FSlateColor(CurrentFillColor));
	}
}

TSharedRef<SWidget> UPlantMoistureBarWidget::RebuildWidget()
{
	ProgressBarWidget = SNew(SProgressBar)
		.Percent(TOptional<float>(CurrentFraction))
		.FillColorAndOpacity(FSlateColor(CurrentFillColor));

	return SNew(SBorder)
		.Padding(2.0f)
		.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
		.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.65f))
		[
			ProgressBarWidget.ToSharedRef()
		];
}

void UPlantMoistureBarWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	ProgressBarWidget.Reset();
}
