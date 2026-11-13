// Copyright Epic Games, Inc. All Rights Reserved.

#include "CafePlanPanelWidget.h"

#include "NPCAutonomyComponent.h"
#include "Misc/ScopeExit.h"
#include "Styling/CoreStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr float PanelPadding = 18.0f;
}

TSharedRef<SWidget> UCafePlanPanelWidget::RebuildWidget()
{
	ContentBox = SNew(SVerticalBox);
	Rows.Reset();

	return SNew(SBox)
		.WidthOverride(PanelWidth)
		.MinDesiredHeight(720.0f)
		.Visibility(EVisibility::SelfHitTestInvisible)
		[
			SNew(SBorder)
				.Padding(PanelPadding)
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.72f))
				.Visibility(EVisibility::SelfHitTestInvisible)
				[
					ContentBox.ToSharedRef()
				]
		];
}

void UCafePlanPanelWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Rows.Reset();
	ContentBox.Reset();
}

void UCafePlanPanelWidget::SetRow(
	int32 RowIndex,
	const FString& Text,
	const FLinearColor& Color,
	int32 FontSize,
	float TopPadding)
{
	if (!ContentBox.IsValid())
	{
		return;
	}

	while (Rows.Num() <= RowIndex)
	{
		FPanelRow& NewRow = Rows.AddDefaulted_GetRef();
		ContentBox->AddSlot()
			.AutoHeight()
			[
				SAssignNew(NewRow.Box, SBox)
					.Visibility(EVisibility::Collapsed)
					[
						// A fixed wrap width lays the text out correctly on its first frame; AutoWrapText needs the
						// previous frame's geometry, which a new row does not have.
						SAssignNew(NewRow.Text, STextBlock)
							.AutoWrapText(false)
							.WrapTextAt(UCafePlanPanelWidget::PanelWidth - 2.0f * PanelPadding)
					]
			];
	}

	FPanelRow& Row = Rows[RowIndex];
	if (!Row.Box.IsValid() || !Row.Text.IsValid())
	{
		return;
	}
	if (Row.LastTopPadding != TopPadding)
	{
		Row.Box->SetPadding(FMargin(0.0f, TopPadding, 0.0f, 2.0f));
		Row.LastTopPadding = TopPadding;
	}
	if (Row.LastFontSize != FontSize)
	{
		Row.Text->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", FontSize));
		Row.LastFontSize = FontSize;
	}
	if (!Row.LastText.Equals(Text, ESearchCase::CaseSensitive))
	{
		Row.Text->SetText(FText::FromString(Text));
		Row.LastText = Text;
	}
	Row.Text->SetColorAndOpacity(FSlateColor(Color));
	Row.Box->SetVisibility(EVisibility::HitTestInvisible);
}

void UCafePlanPanelWidget::RefreshContent()
{
	if (!ContentBox.IsValid())
	{
		return;
	}

	int32 UsedRows = 0;
	auto AddLine = [this, &UsedRows](const FString& Text, const FLinearColor& Color, int32 FontSize = 15, float TopPadding = 2.0f)
	{
		SetRow(UsedRows, Text, Color, FontSize, TopPadding);
		++UsedRows;
	};
	// Hide rows that this refresh did not use (runs on every return path below).
	ON_SCOPE_EXIT
	{
		for (int32 RowIndex = UsedRows; RowIndex < Rows.Num(); ++RowIndex)
		{
			if (Rows[RowIndex].Box.IsValid())
			{
				Rows[RowIndex].Box->SetVisibility(EVisibility::Collapsed);
			}
		}
	};

	const FLinearColor White(0.92f, 0.92f, 0.92f, 1.0f);
	const FLinearColor Dim(0.55f, 0.55f, 0.55f, 1.0f);
	const FLinearColor Green(0.4f, 0.9f, 0.5f, 1.0f);
	const FLinearColor Orange(1.0f, 0.7f, 0.2f, 1.0f);
	const FLinearColor Yellow(1.0f, 0.92f, 0.35f, 1.0f);
	const FLinearColor Cyan(0.45f, 0.85f, 1.0f, 1.0f);

	const UNPCAutonomyComponent* Autonomy = UNPCAutonomyComponent::GetPrimaryAutonomyComponent();
	if (!Autonomy)
	{
		AddLine(TEXT("计划执行列表 [P 关闭]"), Cyan, 20);
		AddLine(TEXT("没有找到 NPC 自主组件。"), Orange);
		return;
	}

	FNPCPlanPanelData Data;
	Autonomy->GetPlanPanelData(Data);

	AddLine(FString::Printf(TEXT("计划执行列表    [P 关闭]    条件: %s"), *Data.Condition), Cyan, 20);

	FString StatusText = TEXT("无计划");
	FLinearColor StatusColor = Dim;
	if (Data.Status == ENPCAutonomyPlanStatus::Active)
	{
		StatusText = TEXT("进行中");
		StatusColor = Green;
	}
	else if (Data.Status == ENPCAutonomyPlanStatus::Suspended)
	{
		StatusText = Data.bWaitingForRelease ? TEXT("已挂起（等待放行）") : TEXT("已挂起");
		StatusColor = Orange;
	}
	AddLine(FString::Printf(TEXT("计划状态: %s"), *StatusText), StatusColor, 18, 8.0f);
	AddLine(
		FString::Printf(
			TEXT("服务状态: %s    活动: %s%s"),
			*Data.ServiceState,
			Data.CurrentActivity.IsEmpty() ? TEXT("空闲") : *Data.CurrentActivity,
			Data.bDecisionPending ? TEXT("    (LLM 思考中)") : TEXT("")),
		White);
	if (Data.OrderWaitSilenceSeconds > 0.0)
	{
		AddLine(FString::Printf(TEXT("等点单沉默: %.0f 秒"), Data.OrderWaitSilenceSeconds), White);
	}

	if (Data.Status != ENPCAutonomyPlanStatus::None)
	{
		AddLine(FString::Printf(TEXT("目标: %s"), *Data.Goal), White, 15, 8.0f);
		AddLine(FString::Printf(TEXT("触发: %s    承诺度: %s"), *Data.Trigger, *Data.Commitment), Dim);
		for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
		{
			const bool bDone = Index < Data.CurrentStepIndex;
			const bool bCurrent = Index == Data.CurrentStepIndex;
			const TCHAR* Mark = bDone ? TEXT("[x]") : (bCurrent ? TEXT("[>]") : TEXT("[ ]"));
			AddLine(
				FString::Printf(TEXT("  %s %d. %s"), Mark, Index + 1, *Data.Steps[Index]),
				bDone ? Dim : (bCurrent ? Yellow : White));
		}
		if (Data.bHasPlanConcern)
		{
			AddLine(
				FString::Printf(
					TEXT("计划挂念: %.0f（保留线 %.0f）  %s"),
					Data.PlanConcern,
					Data.PlanKeepThreshold,
					*Data.PlanConcernBreakdown),
				Data.PlanConcern >= Data.PlanKeepThreshold ? Green : Orange);
		}
	}
	if (!Data.LastFailureReason.IsEmpty())
	{
		AddLine(FString::Printf(TEXT("上次步骤失败: %s"), *Data.LastFailureReason), Orange);
	}

	AddLine(
		FString::Printf(TEXT("挂念    压力 %.0f / 触发线 %.0f"), Data.Pressure, Data.TriggerPressure),
		Cyan,
		18,
		12.0f);
	const int32 ConcernCount = FMath::Min(Data.Concerns.Num(), 5);
	if (ConcernCount == 0)
	{
		AddLine(TEXT("  （没有挂念）"), Dim);
	}
	for (int32 Index = 0; Index < ConcernCount; ++Index)
	{
		const FNPCConcern& Concern = Data.Concerns[Index];
		AddLine(
			FString::Printf(
				TEXT("  %s/%s  %.0f%s  %s"),
				*Concern.Kind,
				*Concern.Id,
				Concern.Score,
				Concern.bUnfinished ? TEXT(" [未完成]") : TEXT(""),
				*Concern.Detail),
			Concern.bUnfinished ? Yellow : White);
	}

	AddLine(TEXT("最近事件"), Cyan, 18, 12.0f);
	if (Data.History.Num() == 0)
	{
		AddLine(TEXT("  （暂无）"), Dim);
	}
	const int32 FirstHistory = FMath::Max(Data.History.Num() - 8, 0);
	for (int32 Index = Data.History.Num() - 1; Index >= FirstHistory; --Index)
	{
		AddLine(TEXT("  ") + Data.History[Index], Index == Data.History.Num() - 1 ? White : Dim, 13);
	}
}
