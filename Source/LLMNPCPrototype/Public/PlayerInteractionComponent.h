// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlayerInteractionComponent.generated.h"

class AFurnitureMarkerActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FInteractionFocusChangedSignature, AActor*, FocusedActor, FText, InteractionText, bool, bHasFocus);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UPlayerInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerInteractionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	float InteractionDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bUseCameraRay;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Debug")
	bool bDrawDebugInteractionRay;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	AActor* CurrentFocusedActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	FText CurrentInteractionText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	bool bHasFocusedInteractable;

	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FInteractionFocusChangedSignature OnInteractionFocusChanged;

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	bool TryInteract();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Interaction")
	FText GetCurrentInteractionText() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Interaction")
	bool HasFocusedInteractable() const;

private:
	void RefreshFocusedInteractable();

	void SetFocusedInteractable(AActor* NewFocusedActor, const FText& NewInteractionText, bool bNewHasFocusedInteractable);

	bool GetInteractionTrace(FVector& OutTraceStart, FVector& OutTraceEnd) const;

	AFurnitureMarkerActor* FindNearestSittableFurniture() const;

	void PrintDebugMessage(const FString& Message, const FColor& Color) const;
};
