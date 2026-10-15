// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MenuUIControllerComponent.generated.h"

class APlayerController;
class UUserWidget;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class LLMNPCPROTOTYPE_API UMenuUIControllerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMenuUIControllerComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu UI")
	TSubclassOf<UUserWidget> MenuWidgetClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Menu UI")
	UUserWidget* MenuWidget;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Menu UI")
	bool bMenuOpen;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu UI", meta = (MultiLine = "true"))
	FString MenuText;

	UFUNCTION(BlueprintCallable, Category = "Menu UI")
	void ToggleMenuUI(APlayerController* PlayerController);

	UFUNCTION(BlueprintCallable, Category = "Menu UI")
	void ShowMenuUI(APlayerController* PlayerController);

	UFUNCTION(BlueprintCallable, Category = "Menu UI")
	void HideMenuUI();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Menu UI")
	bool IsMenuOpen() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Menu UI")
	static bool IsAnyMenuOpen();

	UFUNCTION(BlueprintCallable, Category = "Menu UI")
	static void HideActiveMenuUI();

	UFUNCTION(BlueprintCallable, Category = "Menu UI")
	void SetMenuText(const FString& InMenuText);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ApplyMenuTextToWidget() const;

	static TWeakObjectPtr<UMenuUIControllerComponent> ActiveMenuUIController;
};
