// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CafeTargetPointActor.generated.h"

class UArrowComponent;
class USceneComponent;

UCLASS()
class ACafeTargetPointActor : public AActor
{
	GENERATED_BODY()

public:
	ACafeTargetPointActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cafe Target Point")
	USceneComponent* SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cafe Target Point")
	UArrowComponent* Arrow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Target Point")
	FString TargetID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Target Point")
	FString TargetType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Target Point")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Target Point")
	TArray<FString> TargetTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Target Point")
	int32 Priority;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Target Point")
	bool bIsAvailable;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Cafe Target Point")
	FTransform GetTargetTransform() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Cafe Target Point")
	bool HasTagString(const FString& Tag) const;
};
