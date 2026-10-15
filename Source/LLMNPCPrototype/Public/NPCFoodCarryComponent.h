// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "NPCFoodCarryComponent.generated.h"

class AActor;
class USceneComponent;
class USkeletalMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FFoodCarryStateChangedSignature, bool, bCarryingFood, const FString&, ItemName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FFoodPlacedSignature, AActor*, FoodActor, const FString&, ItemName);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class LLMNPCPROTOTYPE_API UNPCFoodCarryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCFoodCarryComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Food Carry")
	TSubclassOf<AActor> DefaultFoodActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Food Carry")
	TMap<FString, TSubclassOf<AActor>> FoodActorClasses;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Food Carry")
	FName HandSocketName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Food Carry")
	FName PreferredMeshComponentName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Food Carry")
	FVector HandAttachLocationOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Food Carry")
	FRotator HandAttachRotationOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Food Carry")
	FVector TablePlaceOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Food Carry")
	FVector CounterPlaceOffset;

	UPROPERTY(BlueprintReadOnly, Category = "Food Carry")
	AActor* CarriedFoodActor;

	UPROPERTY(BlueprintReadOnly, Category = "Food Carry")
	FString CarriedFoodItemName;

	UPROPERTY(BlueprintReadOnly, Category = "Food Carry")
	bool bIsCarryingFood;

	UPROPERTY(BlueprintReadOnly, Category = "Food Carry")
	AActor* LastPlacedFoodActor;

	UPROPERTY(BlueprintReadOnly, Category = "Food Carry")
	AActor* LastPlacedTableActor;

	UPROPERTY(BlueprintReadOnly, Category = "Food Carry")
	FString LastPlacedFoodItemName;

	UPROPERTY(BlueprintAssignable, Category = "Food Carry")
	FFoodCarryStateChangedSignature OnFoodCarryStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Food Carry")
	FFoodPlacedSignature OnFoodPlaced;

	UFUNCTION(BlueprintCallable, Category = "Food Carry")
	bool SpawnAndAttachFoodToHand(const FString& ItemName);

	UFUNCTION(BlueprintCallable, Category = "Food Carry")
	bool PlaceCarriedFoodOnTable(AActor* TableActor);

	UFUNCTION(BlueprintCallable, Category = "Food Carry")
	bool PickUpPlacedFoodFromTable(AActor* TableActor);

	UFUNCTION(BlueprintCallable, Category = "Food Carry")
	bool PlaceCarriedFoodAtCounter();

	UFUNCTION(BlueprintCallable, Category = "Food Carry")
	void ClearCarriedFood();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Food Carry")
	bool IsCarryingFood() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Food Carry")
	bool ShouldUseCarryWalk() const;

private:
	TSubclassOf<AActor> ResolveFoodActorClass(const FString& ItemName) const;
	USkeletalMeshComponent* ResolveOwnerMesh() const;
	USceneComponent* FindFoodCarryPoint() const;
	bool ResolveTablePlacementTransform(AActor* TableActor, FTransform& OutTransform) const;
	void SetFoodCollisionEnabled(AActor* FoodActor, ECollisionEnabled::Type CollisionType) const;
	void BroadcastCarryStateChanged();
};
