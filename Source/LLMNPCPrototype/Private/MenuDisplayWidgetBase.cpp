// Copyright Epic Games, Inc. All Rights Reserved.

#include "MenuDisplayWidgetBase.h"

#include "Components/TextBlock.h"

UMenuDisplayWidgetBase::UMenuDisplayWidgetBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MenuTitle = TEXT("メニュー");
	MenuText = TEXT("カレーライス\nラーメン\nサラダ\nコーヒー");
	MenuHint = TEXT("Fキーで閉じる");
}

void UMenuDisplayWidgetBase::NativePreConstruct()
{
	Super::NativePreConstruct();

	ApplyMenuTextToBoundWidgets();
}

void UMenuDisplayWidgetBase::SetMenuText(const FString& InMenuText)
{
	MenuText = InMenuText;
	ApplyMenuTextToBoundWidgets();
}

void UMenuDisplayWidgetBase::ApplyMenuTextToBoundWidgets() const
{
	if (MenuTitleText)
	{
		MenuTitleText->SetText(FText::FromString(MenuTitle));
	}

	if (MenuBodyText)
	{
		MenuBodyText->SetText(FText::FromString(MenuText));
	}

	if (HintText)
	{
		HintText->SetText(FText::FromString(MenuHint));
	}
}
