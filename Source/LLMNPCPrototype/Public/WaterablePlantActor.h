// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WaterablePlantActor.generated.h"

class AWaterablePlantActor;
class UArrowComponent;
class USceneComponent;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FWaterablePlantStateChangedSignature,
	AWaterablePlantActor*, Plant,
	float, WaterNeed,
	bool, bNeedsWater);

UCLASS(BlueprintType, Blueprintable)
class LLMNPCPROTOTYPE_API AWaterablePlantActor : public AActor
{
	GENERATED_BODY()

public:
	AWaterablePlantActor();

	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Waterable Plant")
	USceneComponent* SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Waterable Plant")
	UStaticMeshComponent* PlantMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Waterable Plant")
	UArrowComponent* FacingArrow;

	// Assign an already placed flower pot here. When unset, this actor and PlantMesh are used.
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Waterable Plant|Target")
	AActor* ObservedPlantActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Identity")
	FString PlantID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Identity")
	TArray<FString> PlantTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|State", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float WaterNeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|State", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float NeedsWaterThreshold;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Waterable Plant|State")
	bool bNeedsWater;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|State")
	bool bEnabled;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|State")
	bool bReserved;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Selection")
	int32 Priority;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Selection", meta = (ClampMin = "40.0"))
	float ApproachDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Selection")
	bool bOnlyTargetWhenNeedsWater;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Autonomy")
	bool bSyncAutonomyWorldState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Autonomy")
	FName AutonomyWorldStateTag;

	UPROPERTY(BlueprintAssignable, Category = "Waterable Plant")
	FWaterablePlantStateChangedSignature OnPlantStateChanged;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Waterable Plant")
	AActor* GetPlantTargetActor() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Waterable Plant")
	FVector GetPlantTargetLocation() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Waterable Plant")
	bool HasTagString(const FString& Tag) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Waterable Plant")
	bool NeedsWater() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Waterable Plant")
	bool IsAvailableForTag(const FString& Tag) const;

	UFUNCTION(BlueprintCallable, Category = "Waterable Plant")
	void SetWaterNeed(float NewWaterNeed, bool bMeaningfulChange = true);

	UFUNCTION(BlueprintCallable, Category = "Waterable Plant")
	void AddWaterNeed(float Amount, bool bMeaningfulChange = true);

	UFUNCTION(BlueprintCallable, Category = "Waterable Plant")
	void WaterPlant(float Amount = 100.0f, bool bMeaningfulChange = true);

	UFUNCTION(BlueprintCallable, Category = "Waterable Plant|Autonomy")
	void RefreshAutonomyWorldState(bool bMeaningfulChange = true) const;

protected:
	virtual void BeginPlay() override;

private:
	void RefreshDerivedState();
};
