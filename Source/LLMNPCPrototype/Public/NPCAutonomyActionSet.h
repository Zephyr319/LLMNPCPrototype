// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NPCAutonomyTypes.h"
#include "NPCAutonomyActionSet.generated.h"

UCLASS(BlueprintType)
class LLMNPCPROTOTYPE_API UNPCAutonomyActionSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC Autonomy")
	TArray<FNPCAutonomyActivityDefinition> Activities;
};
