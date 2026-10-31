// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AssetReferenceReplacementLibrary.generated.h"

class UAnimSequenceBase;
class UAnimSequence;
class UNPCAutonomyActionSet;

/** Editor migration helpers used by project maintenance scripts. */
UCLASS()
class LLMNPCPROTOTYPE_API UAssetReferenceReplacementLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Editor Asset Migration", meta = (DevelopmentOnly))
	static int64 ReplaceObjectReferences(
		UObject* RootObject,
		const TArray<UObject*>& ObjectsToReplace,
		const TArray<UObject*>& ReplacementObjects);

	UFUNCTION(BlueprintCallable, Category = "Editor Asset Migration", meta = (DevelopmentOnly))
	static int64 ReplacePackageObjectReferences(
		UObject* ObjectInPackage,
		const TArray<UObject*>& ObjectsToReplace,
		const TArray<UObject*>& ReplacementObjects);

	UFUNCTION(BlueprintCallable, Category = "Editor Asset Migration", meta = (DevelopmentOnly))
	static int32 SetAutonomyActionAnimations(
		UNPCAutonomyActionSet* ActionSet,
		const TArray<FString>& ActivityIDs,
		const TArray<UAnimSequenceBase*>& Animations);

	UFUNCTION(BlueprintCallable, Category = "Editor Asset Migration", meta = (DevelopmentOnly))
	static bool ApplyBoneRotationOffset(
		UAnimSequence* Animation,
		FName BoneName,
		FRotator RotationOffset,
		bool bPreMultiply = true);

	UFUNCTION(BlueprintCallable, Category = "Editor Asset Migration", meta = (DevelopmentOnly))
	static bool CopyAnimationSequenceData(
		UAnimSequence* DestinationAnimation,
		UAnimSequence* SourceAnimation);
};
