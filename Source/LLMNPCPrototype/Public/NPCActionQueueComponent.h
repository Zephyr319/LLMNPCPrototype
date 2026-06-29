// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "NPCActionQueueComponent.generated.h"

class UNPCFaceTargetComponent;
class UNPCMoveToTableComponent;

UENUM(BlueprintType)
enum class ENPCQueuedActionType : uint8
{
	None,
	MoveToTableID,
	MoveToSceneTag,
	MoveToFurnitureTag,
	MoveToFurnitureID,
	MoveNearPlayer,
	Wait,
	FacePlayer,
	Stop
};

USTRUCT()
struct FNPCQueuedAction
{
	GENERATED_BODY()

	ENPCQueuedActionType ActionType = ENPCQueuedActionType::None;

	FString Target;

	float WaitSeconds = 0.0f;

	float StopDistance = 140.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FQueueSpeakRequestedSignature, FString, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FQueueGestureRequestedSignature, FName, GestureName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPostSeatingMenuGuidanceFinishedSignature);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UNPCActionQueueComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCActionQueueComponent();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Action Queue")
	bool bIsRunningQueue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue")
	bool bStopQueueOnActionFailure;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue")
	bool bPrintActionQueueDebug;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue")
	FString CurrentServiceTableID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue")
	FString LastVisitedTableID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Order")
	float OrderPreparationSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Order")
	FString OrderConfirmLine;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Order")
	FString OrderServedLine;

	UPROPERTY(BlueprintAssignable, Category = "NPC Action Queue|Order")
	FQueueSpeakRequestedSignature OnQueueSpeakRequested;

	UPROPERTY(BlueprintAssignable, Category = "NPC Action Queue|Order")
	FQueueGestureRequestedSignature OnQueueGestureRequested;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Order")
	bool bReturnToCounterAfterServing;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Order")
	float PostServePauseSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Order")
	float BillPresentationPauseSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Order")
	float ClosingBowPauseSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Order")
	float ReturnToCounterAfterClosingDelaySeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Order")
	int32 DefaultOrderPrice;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Order")
	FString BillLineFormat;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Order")
	FString ServiceClosingLine;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Post Seating")
	float PostSeatingMenuGuidanceDelay;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Post Seating")
	float PostSeatingMenuHelpEnableDelay;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Post Seating")
	FString PostSeatingMenuGuidanceLine;

	UPROPERTY(BlueprintAssignable, Category = "NPC Action Queue|Post Seating")
	FPostSeatingMenuGuidanceFinishedSignature OnPostSeatingMenuGuidanceFinished;

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void ClearActionQueue();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void StopActionQueue();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void EnqueueMoveToTableID(const FString& TableID);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void EnqueueMoveToSceneTag(const FString& TargetTag);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void EnqueueMoveToFurnitureTag(const FString& FurnitureTag);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void EnqueueMoveToFurnitureID(const FString& FurnitureID);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void EnqueueMoveNearPlayer(float StopDistance = 140.0f);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void EnqueueWait(float Seconds);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void EnqueueFacePlayer();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void StartActionQueue();

	UFUNCTION(BlueprintCallable, Category = "NPC|Action Queue")
	void HandleStrategyAction(const FString& ActionType, const FString& Item, int32 Quantity);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void RunCounterAndReturnToTable(const FString& ReturnTableID);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void RunCounterAndReturnToPlayer();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void RunMoveNearPlayer(float StopDistance = 140.0f);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void RunStartFollowPlayer(float DesiredDistance = 160.0f);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void RunStopFollowPlayer();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void RunGuidePlayerToTable(const FString& TableID);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void RunGuidePlayerToSceneTag(const FString& TargetTag);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void RunStopGuidePlayer();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void RunTableRouteFromString(const FString& RouteString);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void RunActionSequenceFromJson(const FString& SequenceJson);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void RunCounterAndReturnToCurrentTable();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue|Order")
	void RunOrderFulfillmentToCurrentTable(const FString& OrderItem);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue|Post Seating")
	void RunPostSeatingMenuGuidance();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void SetCurrentServiceTableID(const FString& TableID);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Action Queue")
	FString GetCurrentServiceTableID() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	TArray<FNPCQueuedAction> ActionQueue;

	FNPCQueuedAction CurrentAction;

	bool bWaitingForMove;

	bool bPendingOrderFulfillment;

	FString PendingOrderReturnTableID;

	FString PendingOrderItem;

	int32 PendingOrderPrice;

	FTimerHandle WaitTimerHandle;

	FTimerHandle PostServePauseTimerHandle;

	FTimerHandle BillPresentationPauseTimerHandle;

	FTimerHandle ClosingBowPauseTimerHandle;

	FTimerHandle ReturnToCounterAfterClosingTimerHandle;

	FTimerHandle PostSeatingMenuGuidanceTimerHandle;

	FTimerHandle PostSeatingMenuHelpEnableTimerHandle;

	UPROPERTY(Transient)
	UNPCMoveToTableComponent* MoveComponent;

	UPROPERTY(Transient)
	UNPCFaceTargetComponent* FaceTargetComponent;

	void CacheOwnerComponents();

	void CancelFollowingForExplicitCommand();

	void BindMoveCompletedEvent();

	void UnbindMoveCompletedEvent();

	void EnqueueAction(const FNPCQueuedAction& Action);

	void ExecuteNextQueuedAction();

	void ExecuteMoveAction();

	void ExecuteWaitAction();

	void ExecuteFacePlayerAction();

	void CompleteCurrentAction(bool bSuccess, const FString& FailureReason);

	bool IsCurrentActionMove() const;

	FString ResolveCurrentServiceTableID() const;

	int32 ResolveOrderPrice(const FString& OrderItem) const;

	AActor* FindPlayerActor() const;

	void FaceOwnerTowardActor(AActor* TargetActor) const;

	void PrintDebugMessage(const FString& Message, const FColor& Color) const;

	void ClearOrderFulfillmentTimers();

	void ClearPendingOrderState();

	void BroadcastQueueGesture(FName GestureName);

	void StartOrderServingCompletionFlow();

	void ReturnToCounterAfterServingIfNeeded();

	void HandleReturnToCounterAfterClosingDelayFinished();

	void ClearPostSeatingMenuGuidanceTimers();

	UFUNCTION()
	void HandleNPCMoveCompleted(FName MoveReason, FString TargetID, bool bSuccess);

	void HandleWaitTimerFinished();

	void HandlePostServePauseTimerFinished();

	void HandleBillPresentationPauseTimerFinished();

	void HandleClosingBowPauseTimerFinished();

	void HandlePostSeatingMenuGuidanceTimerFinished();

	void HandlePostSeatingMenuHelpEnableTimerFinished();
};
