// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MenuDisplayWidgetBase.generated.h"

class UTextBlock;

UCLASS(Blueprintable)
class LLMNPCPROTOTYPE_API UMenuDisplayWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	UMenuDisplayWidgetBase(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu UI")
	FString MenuTitle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu UI", meta = (MultiLine = "true"))
	FString MenuText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu UI")
	FString MenuHint;

	UFUNCTION(BlueprintCallable, Category = "Menu UI")
	void SetMenuText(const FString& InMenuText);

protected:
	virtual void NativePreConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Menu UI")
	UTextBlock* MenuTitleText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Menu UI")
	UTextBlock* MenuBodyText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Menu UI")
	UTextBlock* HintText;

private:
	void ApplyMenuTextToBoundWidgets() const;
};
