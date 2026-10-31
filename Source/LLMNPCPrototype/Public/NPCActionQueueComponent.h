// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "NPCActionQueueComponent.generated.h"

class AActor;
class AFurnitureMarkerActor;
class UAnimInstance;
class UAnimMontage;
class UAnimSequenceBase;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UNPCFaceTargetComponent;
class UNPCFoodCarryComponent;
class UNPCMoveToTableComponent;

UENUM(BlueprintType)
enum class ENPCQueuedActionType : uint8
{
	None,
	MoveToTableID,
	MoveToSceneTag,
	MoveToFurnitureTag,
	MoveToFurnitureID,
	MoveToSeatID,
	MoveNearPlayer,
	Wait,
	Animation,
	AttachHeldProp,
	RemoveHeldProp,
	PickUpPlacedFood,
	PlaceCarriedFoodAtCounter,
	FacePlayer,
	EnterSeat,
	ExitSeat,
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

	UAnimSequenceBase* Animation = nullptr;

	FName AnimationSlotName = FName(TEXT("DefaultSlot"));

	float AnimationPlayRate = 1.0f;

	int32 AnimationLoopCount = 1;

	UStaticMesh* HeldPropMesh = nullptr;

	FName HeldPropSocketName = FName(TEXT("RightHand"));

	FTransform HeldPropRelativeTransform = FTransform::Identity;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FQueueSpeakRequestedSignature, FString, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FQueueGestureRequestedSignature, FName, GestureName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FActionQueueFinishedSignature, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPostSeatingMenuGuidanceFinishedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOrderStartedSignature, FString, TableID, FString, Item);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOrderServedSignature, FString, TableID, FString, Item);

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

	UPROPERTY(BlueprintAssignable, Category = "NPC Action Queue|Order")
	FOrderStartedSignature OnOrderStarted;

	UPROPERTY(BlueprintAssignable, Category = "NPC Action Queue|Order")
	FOrderServedSignature OnOrderServed;

	UPROPERTY(BlueprintAssignable, Category = "NPC Action Queue")
	FActionQueueFinishedSignature OnActionQueueFinished;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service|Timing")
	float BowBeforeReturnDuration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service|Timing")
	float BlockingServiceGesturePauseDuration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service|Timing")
	float BowGestureFallbackTimeout;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Gesture|Bow")
	float BowMinimumLockDurationSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Gesture|Bow")
	float BowMovementReleaseSafetyDelay;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service|Timing")
	float PutBillGestureFallbackTimeout;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service|Timing")
	float ServeFoodGestureFallbackTimeout;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service|Timing")
	float GuideSeatGestureFallbackTimeout;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service|Timing")
	float DefaultBlockingGestureFallbackTimeout;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Order")
	int32 DefaultOrderPrice;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Order")
	FString BillLineFormat;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Order")
	FString ServiceClosingLine;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service|Lines")
	FString BillFarewellLine;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Post Seating")
	float PostSeatingMenuGuidanceDelay;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Post Seating")
	float GuideArrivedSeatGestureDuration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Post Seating")
	float DuplicateGuideLineSuppressSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Action Queue|Post Seating")
	FName GuideArrivedSeatGestureName;

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

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue|Animation")
	bool PauseActiveQueuedAnimation();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue|Animation")
	bool ResumeActiveQueuedAnimation();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Action Queue|Animation")
	bool HasActiveQueuedAnimation() const;

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void EnqueueMoveToTableID(const FString& TableID);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void EnqueueMoveToSceneTag(const FString& TargetTag);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void EnqueueMoveToFurnitureTag(const FString& FurnitureTag);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void EnqueueMoveToFurnitureID(const FString& FurnitureID);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue|Seat")
	void EnqueueMoveToSeatID(const FString& FurnitureID);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void EnqueueMoveNearPlayer(float StopDistance = 140.0f);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void EnqueueWait(float Seconds);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void EnqueueAnimation(UAnimSequenceBase* Animation, FName SlotName = NAME_None, float PlayRate = 1.0f, float FallbackSeconds = 3.0f);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue|Animation")
	void EnqueueLoopedAnimation(UAnimSequenceBase* Animation, int32 LoopCount, FName SlotName = NAME_None, float PlayRate = 1.0f, float FallbackSeconds = 3.0f);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue|Seat")
	void EnqueueEnterSeat();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue|Seat")
	void EnqueueExitSeat();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue|Held Prop")
	void EnqueueAttachHeldProp(UStaticMesh* PropMesh, FName SocketName, const FTransform& RelativeTransform);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue|Held Prop")
	void EnqueueRemoveHeldProp();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void EnqueuePickUpPlacedFood(const FString& TableID);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void EnqueuePlaceCarriedFoodAtCounter();

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

	static void SetSuppressNextOrderConfirmSpeechForStrategyReply(bool bSuppress);

	static void SetSuppressNextGuideStartedSpeechForStrategyReply(bool bSuppress);

	static bool ConsumeSuppressNextGuideStartedSpeechForStrategyReply();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue|Order")
	void RunBillDropBowAndReturnToCounter();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue|Order")
	bool PlaceCarriedFoodOnCurrentServiceTable();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue|Post Seating")
	void RunPostSeatingMenuGuidance();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue|Post Seating")
	void GuideSeatPlayerOnce();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void SetCurrentServiceTableID(const FString& TableID);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void PrepareForNewReception();

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void SetPlayerInTableArea(bool bInArea, const FString& TableID);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void NotifyPlayerEnteredTableArea(const FString& TableID);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void NotifyPlayerLeftTableArea(const FString& TableID);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void NotifyPlayerDepartureConfirmed(const FString& TableID);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void SetMealCompletionConfirmationPending(bool bPending, const FString& TableID);

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void SetDiningServiceCompleted(bool bCompleted, const FString& TableID);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Action Queue")
	FString GetCurrentServiceTableID() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Action Queue")
	bool IsSeatedForService() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Action Queue")
	bool IsGuidePlayerInProgress() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Action Queue")
	bool IsFarewellBowInProgress() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Action Queue")
	bool IsActionQueuePausedForFarewellBow() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Action Queue")
	bool IsBowMovementLockActive() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Action Queue")
	bool IsBlockingGestureInProgress() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Action Queue")
	bool IsBusyForAutonomy() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Action Queue|Seat")
	bool IsNPCSeated() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Action Queue|Seat")
	FString GetNPCSeatID() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC Action Queue")
	FString GetCurrentBlockingGestureName() const;

	UFUNCTION(BlueprintCallable, Category = "NPC Action Queue")
	void NotifyServiceGestureFinished(const FString& GestureName);

	static bool IsLastKnownSeatedForService();

	static bool IsLastKnownPlayerInTableArea();

	static bool IsLastKnownGuidePlayerInProgress();

	static bool IsLastKnownSeatingGuidanceCompleted();

	static FString GetLastKnownCurrentServiceTableID();

	static FString GetLastKnownGuideTargetID();

	static bool IsLastKnownMealCompletionConfirmationPending();

	static FString GetLastKnownMealCompletionConfirmationTableID();

	static bool IsLastKnownDiningServiceCompleted();

	static FString GetLastKnownCompletedServiceTableID();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	TArray<FNPCQueuedAction> ActionQueue;

	FNPCQueuedAction CurrentAction;

	bool bWaitingForMove;

	bool bCurrentQueueHadFailure;

	bool bQueuedAnimationInProgress;

	bool bQueuedAnimationPaused;

	bool bPlayerInTableArea;

	bool bSeatingGuidanceCompleted;

	bool bPostSeatingMenuGuidanceDone;

	bool bPendingOrderFulfillment;

	bool bFarewellBowInProgress;

	bool bActionQueuePausedForFarewellBow;

	bool bBowMovementLockActive;

	bool bBowFinishNotifyReceived;

	bool bBowMinimumDurationElapsed;

	bool bBowSafetyDelayElapsed;

	bool bPendingReturnToCounterAfterBow;

	bool bBlockingGestureInProgress;

	FString CurrentBlockingGestureName;

	bool bBlockingGestureFinishHandled;

	bool bPendingFarewellBowAfterBillGesture;

	bool bPendingPostServeFlowAfterServeGesture;

	bool bSuppressNextOrderConfirmSpeech;

	FString PendingOrderReturnTableID;

	FString PendingOrderItem;

	FString PendingPreSeatingOrderItem;

	FString LastGuideLineText;

	double LastGuideLineTime;

	int32 PendingOrderPrice;

	FTimerHandle WaitTimerHandle;

	FTimerHandle QueuedAnimationTimerHandle;

	UPROPERTY(Transient)
	UAnimMontage* ActiveQueuedAnimationMontage;

	UPROPERTY(Transient)
	UAnimInstance* ActiveQueuedAnimationInstance;

	UPROPERTY(Transient)
	UStaticMeshComponent* ActiveHeldPropComponent;

	UPROPERTY(Transient)
	TObjectPtr<AFurnitureMarkerActor> ReservedSeat;

	bool bNPCIsSeated;

	TWeakObjectPtr<UAnimMontage> BlockingGestureMontageBeforeRequest;

	float BlockingGestureMontagePositionBeforeRequest;

	FName PendingGestureAnimationTimingProbe;

	FTimerHandle PostServePauseTimerHandle;

	FTimerHandle BillPresentationPauseTimerHandle;

	FTimerHandle ClosingBowPauseTimerHandle;

	FTimerHandle ReturnToCounterAfterClosingTimerHandle;

	FTimerHandle FarewellBowTimerHandle;

	FTimerHandle BlockingServiceGesturePauseTimerHandle;

	FTimerHandle BowMinimumDurationTimerHandle;

	FTimerHandle BowSafetyDelayTimerHandle;

	FTimerHandle PostSeatingMenuGuidanceTimerHandle;

	FTimerHandle PostSeatingMenuHelpEnableTimerHandle;

	UPROPERTY(Transient)
	UNPCMoveToTableComponent* MoveComponent;

	UPROPERTY(Transient)
	UNPCFaceTargetComponent* FaceTargetComponent;

	UPROPERTY(Transient)
	UNPCFoodCarryComponent* FoodCarryComponent;

	void CacheOwnerComponents();

	UNPCFoodCarryComponent* ResolveFoodCarryComponent();

	void CancelFollowingForExplicitCommand();

	void UpdateLastKnownServiceState() const;
	bool CancelReceptionForExplicitNPCCommand(const FString& Reason) const;

	void BindMoveCompletedEvent();

	void UnbindMoveCompletedEvent();

	void EnqueueAction(const FNPCQueuedAction& Action);

	void ExecuteNextQueuedAction();

	void ExecuteMoveAction();

	void ExecuteWaitAction();

	void ExecuteAnimationAction();

	void ExecuteHeldPropAction();

	void ExecuteFoodCarryAction();

	void ExecuteFacePlayerAction();

	void ExecuteSeatAction();

	void CompleteCurrentAction(bool bSuccess, const FString& FailureReason);

	bool IsCurrentActionMove() const;

	AFurnitureMarkerActor* FindFurnitureByID(const FString& FurnitureID) const;

	bool ReserveSeat(const FString& FurnitureID);

	void ReleaseReservedSeat(bool bMoveToExitPoint);

	FString ResolveCurrentServiceTableID() const;

	int32 ResolveOrderPrice(const FString& OrderItem) const;

	AActor* FindPlayerActor() const;

	void FaceOwnerTowardActor(AActor* TargetActor) const;

	void PrintDebugMessage(const FString& Message, const FColor& Color) const;

	void LogActionQueueMoveRequest(const TCHAR* FunctionName, const FString& Target) const;

	bool IsReceptionPhaseForService() const;

	bool IsReceptionSpeechAction(const FString& ActionType) const;

	void ClearOrderFulfillmentTimers();

	void ClearPendingOrderState();

	void RunOrderFulfillmentToCurrentTableInternal(const FString& OrderItem, bool bSpeakOrderConfirm);

	void PickUpFoodForPendingOrder();

	AActor* FindServiceTableActorByID(const FString& TableID) const;

	void BroadcastServiceSpeech(const FString& Text);

	bool BroadcastGuideLine(const FString& Text);

	void BroadcastQueueGesture(FName GestureName);

	void StartOrderServingCompletionFlow();

	bool IsBlockingServiceGesture(const FString& GestureName) const;

	void BeginBlockingGesture(FName GestureName, float FallbackTimeout);

	float GetBlockingGestureFallbackTimeout(const FString& GestureName) const;

	void CompleteBlockingGesture(const FString& GestureName, bool bForced);

	bool BlockMovementIfBlockingGestureInProgress(const TCHAR* FunctionName, const FString& Target = FString(), bool bTargetIsCounter = false);

	void HandleBlockingGestureTimeout();

	bool BlockReturnToCounterIfFarewellBowInProgress();

	bool IsCounterTarget(const FString& Target) const;

	bool IsCounterMoveAction(const FNPCQueuedAction& Action) const;

	bool DoesQueuedActionListContainCounterMove() const;

	bool IsBowGestureName(const FString& GestureName) const;

	void StartBowMovementLock(FName GestureName);

	void MarkBowFinishNotifyReceived(FName GestureName);

	void MarkBowMinimumDurationElapsed();

	void MarkBowSafetyDelayElapsed();

	void TryReleaseBowMovementLock();

	void ForceReleaseBowMovementLockByTimeout();

	void UpdateBlockingGestureTimingFromStartedAnimation(FName GestureName);

	bool TryGetActiveGestureAnimationState(
		UAnimMontage*& OutMontage,
		float& OutPosition,
		float& OutRemainingSeconds,
		FString& OutAnimationName) const;

	void EnableFarewellBowLock();

	void ReleaseFarewellBowMovementLock();

	void ReturnToCounterAfterServingIfNeeded();

	void ScheduleReturnToCounterAfterClosingDelay();

	void HandleReturnToCounterAfterClosingDelayFinished();

	void HandleFarewellBowFinished();

	void ClearPostSeatingMenuGuidanceTimers();

	UFUNCTION()
	void HandleNPCMoveCompleted(FName MoveReason, FString TargetID, bool bSuccess);

	UFUNCTION()
	void HandleGuideArrived(FName EventName, FString TargetID, FString SuggestedLine);

	void HandleWaitTimerFinished();

	void HandleQueuedAnimationTimerFinished();

	void StopQueuedAnimation(float BlendOutSeconds = 0.2f);

	USkeletalMeshComponent* ResolveHeldPropAttachmentMesh(FName SocketName, FName& OutResolvedSocketName) const;

	void RemoveActiveHeldProp();

	void HandlePostServePauseTimerFinished();

	void HandleBillPresentationPauseTimerFinished();

	void HandleClosingBowPauseTimerFinished();

	void HandlePostSeatingMenuGuidanceTimerFinished();

	void HandlePostSeatingMenuHelpEnableTimerFinished();

	static bool bLastKnownSeatedForService;

	static bool bLastKnownPlayerInTableArea;

	static bool bLastKnownGuidePlayerInProgress;

	static bool bLastKnownSeatingGuidanceCompleted;

	static FString LastKnownCurrentServiceTableID;

	static FString LastKnownGuideTargetID;

	static bool bLastKnownMealCompletionConfirmationPending;

	static FString LastKnownMealCompletionConfirmationTableID;

	static bool bLastKnownDiningServiceCompleted;

	static FString LastKnownCompletedServiceTableID;

	static bool bSuppressNextOrderConfirmSpeechForStrategyReply;

	static bool bSuppressNextGuideStartedSpeechForStrategyReply;
};
