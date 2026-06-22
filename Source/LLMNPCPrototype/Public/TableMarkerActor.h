// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TableMarkerActor.generated.h"

class UArrowComponent;
class UBoxComponent;
class USceneComponent;
class UPrimitiveComponent;

UCLASS()
class ATableMarkerActor : public AActor
{
	GENERATED_BODY()

public:
	ATableMarkerActor();

	const FString& GetTableID() const;
	const FText& GetTableName() const;
	bool IsTableOccupied() const;
	const TArray<FString>& GetTableTags() const;
	int32 GetSeatCount() const;
	int32 GetPriority() const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UBoxComponent* TriggerBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Table Marker")
	UBoxComponent* ObstacleBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UArrowComponent* Arrow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table")
	FString TableID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table")
	FText TableName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table")
	bool bIsOccupied;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table")
	TArray<FString> TableTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table")
	int32 SeatCount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table")
	int32 Priority;

private:
	UFUNCTION()
	void OnTriggerBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnTriggerBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	bool IsLocalPlayerPawn(AActor* OtherActor) const;
};
