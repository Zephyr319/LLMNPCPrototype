// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteractableInterface.h"
#include "WaterablePlantActor.generated.h"

class AWaterablePlantActor;
class UArrowComponent;
class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;
class UWidgetComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FWaterablePlantStateChangedSignature,
	AWaterablePlantActor*, Plant,
	float, WaterNeed,
	bool, bNeedsWater);

UCLASS(BlueprintType, Blueprintable)
class LLMNPCPROTOTYPE_API AWaterablePlantActor : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AWaterablePlantActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Waterable Plant")
	USceneComponent* SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Waterable Plant")
	UStaticMeshComponent* PlantMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Waterable Plant")
	UArrowComponent* FacingArrow;

	// Assign an already placed flower pot here. When unset, this actor and PlantMesh are used.
	// Invisible box around the target plant that blocks only the visibility channel, so the player's crosshair
	// ray (PlayerInteractionComponent) hits this actor and shows the watering prompt.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Waterable Plant|Player Watering")
	UBoxComponent* PlantInteractionBox;

	// Screen-space moisture bar floating above the plant (fed by GetMoisture()).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Waterable Plant|Moisture Bar")
	UWidgetComponent* MoistureBarComponent;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Waterable Plant|Target")
	AActor* ObservedPlantActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Moisture Bar")
	bool bShowMoistureBar;

	// Height of the bar above the top of the target plant's bounds (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Moisture Bar", meta = (ClampMin = "0.0"))
	float MoistureBarHeightOffset;

	// Bar size in screen pixels (width, height).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Moisture Bar")
	FVector2D MoistureBarDrawSize;

	// The bar is hidden when the camera is farther than this (cm). 0 = always visible.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Moisture Bar", meta = (ClampMin = "0.0"))
	float MoistureBarMaxDistance;

	// When true the bar only appears once moisture is below the full (clear) line.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Moisture Bar")
	bool bMoistureBarOnlyWhenNotFull;

	// --- Player watering: aim at the pot, press F to start, press F again (or walk away) to stop ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Player Watering")
	bool bAllowPlayerWatering;

	// Moisture added per second while the player waters (NPC default is 5).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Player Watering", meta = (ClampMin = "0.0"))
	float PlayerWateringMoisturePerSecond;

	// The player must be within this distance (cm, measured from the edge of the plant) to start,
	// and watering stops when they move farther away.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Player Watering", meta = (ClampMin = "0.0"))
	float PlayerWateringMaxDistance;

	// How far (cm) the crosshair hit box extends beyond the plant's bounds on every side. Bigger = easier to aim.
	// Read in BeginPlay, so restart PIE after changing it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Player Watering", meta = (ClampMin = "0.0"))
	float InteractionBoxPadding;

	// Prompt text shown while aiming at the plant (same "F ..." style as the seat prompt).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Player Watering")
	FText PlayerWaterPrompt;

	// Prompt text shown while the player is watering.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Player Watering")
	FText PlayerStopWaterPrompt;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Identity")
	FString PlantID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Identity")
	TArray<FString> PlantTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|State", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float WaterNeed;

	// Legacy threshold (WaterNeed units). The hysteresis state below no longer uses it; kept so existing
	// level instances and blueprints keep loading unchanged.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|State", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float NeedsWaterThreshold;

	// Moisture = 100 - WaterNeed. 100 is fully watered, 0 is dry. Moisture 0 has no gameplay consequence.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Moisture")
	bool bEnableMoistureDecay;

	// Moisture lost per second (before the Cafe.Plant.DecayScale console multiplier).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Moisture", meta = (ClampMin = "0.0"))
	float MoistureDecayPerSecond;

	// The plant enters the "needs water" state when moisture drops below this value.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Moisture", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float NeedsWaterEnterMoisture;

	// The "needs water" state is cleared only when moisture reaches this value (or it is cleared explicitly).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Moisture", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float NeedsWaterClearMoisture;

	// When true, BeginPlay replaces the placed WaterNeed with a deterministic per-plant staggered value
	// (moisture 69..100) so the plants do not all cross the threshold at the same moment.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waterable Plant|Moisture")
	bool bRandomizeInitialMoisture;

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

	// --- Continuous moisture API (moisture = 100 - WaterNeed) ---
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Waterable Plant|Moisture")
	float GetMoisture() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Waterable Plant|Moisture")
	float GetMoisture01() const;

	// full (>=clear line), ok, low (<enter), dry (<enter-20), critical (<enter-40)
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Waterable Plant|Moisture")
	FString GetMoistureTierName() const;

	// 0 when the plant does not need water; grows toward 1 as moisture falls below the enter line.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Waterable Plant|Moisture")
	float GetWaterUrgency01() const;

	// Raises moisture (clamped to 100). Used by NPC and player watering; does not interrupt the NPC.
	// Returns the new moisture.
	float AddMoisture(float Amount, const TCHAR* Source);

	// Explicitly dismisses the "needs water" state (e.g. the owner decided not to water). The plant becomes
	// needy again only after moisture falls 10 points below the current value (or below the enter line).
	void ClearNeedsWater(const FString& Reason);

	// --- Player watering (IInteractableInterface, driven by PlayerInteractionComponent / F key) ---
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual FText GetInteractionText_Implementation(AActor* Interactor) const override;
	virtual bool Interact_Implementation(AActor* Interactor) override;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Waterable Plant|Player Watering")
	bool IsPlayerWatering() const;

	void StopPlayerWatering(const TCHAR* Reason);

	// --- Reservation (one waterer at a time). Released on every NPC exit path. ---
	bool IsReserved() const;
	bool TryReserve(AActor* Waterer);
	void ReleaseReservation(AActor* Waterer);
	FString GetReservedByName() const;

protected:
	virtual void BeginPlay() override;

private:
	void RefreshDerivedState();
	void UpdateNeedsWaterState();
	void ApplyMoisture(float NewMoisture, bool bMeaningfulChange, const TCHAR* Source);
	int32 ComputeReminderTier(float Moisture) const;
	void NotifyMoistureReminder(int32 Tier, float Moisture) const;
	void RefreshTargetBounds();
	void PlaceMoistureBar();
	void UpdateMoistureBar();
	void StartPlayerWatering(AActor* Waterer);
	void UpdatePlayerWatering(float DeltaSeconds);
	double GetPlayerDistanceToPlant(const AActor* Player) const;
	bool IsPlayerSeated(const AActor* Player) const;

	FVector TargetBoundsOrigin = FVector::ZeroVector;
	FVector TargetBoundsExtent = FVector::ZeroVector;
	TWeakObjectPtr<AActor> PlayerWaterer;
	bool bPlayerWatering = false;
	double PlayerWateringStartedAt = 0.0;
	float PlayerWateringStartMoisture = 0.0f;

	float LastBarMoisture = -1000.0f;
	double BarWateringHighlightUntil = 0.0;

	TWeakObjectPtr<AActor> ReservedBy;
	bool bReservedByRuntime = false;

	// Highest reminder tier already reported (0 none, 1 below enter, 2 below enter-20, 3 below enter-40).
	int32 ReminderTier = 0;
	float LastBroadcastMoisture = -1000.0f;
	float DismissedReenterMoisture = TNumericLimits<float>::Max();
};
