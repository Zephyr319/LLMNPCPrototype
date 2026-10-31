// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "CafeServiceManagerActor.generated.h"

class ATableMarkerActor;
class UNPCAutonomyComponent;
class UNPCActionQueueComponent;
class UNPCEmotionComponent;
class UNPCMemoryComponent;
class UNPCMoveToTableComponent;
class UNPCResearchLoggerComponent;
class UPlayerSeatComponent;
struct FNPCMemoryEntry;
struct FNPCRecallScore;

UENUM(BlueprintType)
enum class ECafeServiceState : uint8
{
	Idle,
	MaidApproachingPlayer,
	ReceptionGreeting,
	WaitingForReceptionResponse,
	SelectingSeat,
	GuidingToSeat,
	WaitingForPlayerSeated,
	WaitingForOrder,
	OrderReceived,
	GoingToCounter,
	DeliveringOrder,
	Served
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCafeServiceStateChangedSignature, ECafeServiceState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMaidSpeechRequestedSignature, FText, SpeechText);

UCLASS()
class ACafeServiceManagerActor : public AActor
{
	GENERATED_BODY()

public:
	ACafeServiceManagerActor();

	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cafe Service|Autonomy")
	UNPCAutonomyComponent* AutonomyComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cafe Service|Memory")
	UNPCMemoryComponent* MemoryComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cafe Service|Emotion")
	UNPCEmotionComponent* EmotionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cafe Service|Research")
	UNPCResearchLoggerComponent* ResearchLoggerComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cafe Service")
	ECafeServiceState CurrentState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service")
	AActor* MaidNPCActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cafe Service")
	AActor* CurrentPlayerActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cafe Service")
	FString TargetTableID;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cafe Service")
	int32 CurrentPartySize;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cafe Service")
	bool bCurrentWantsDining;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service")
	float MaidApproachDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service")
	float ReceptionGreetingDelaySeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Departure", meta = (ClampMin = "50.0"))
	float DepartureDistanceThreshold;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Departure", meta = (ClampMin = "0.5"))
	float DepartureConfirmationSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Departure")
	bool bRequireOrderServedBeforeDeparture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Departure")
	FText MealCompletionQuestion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Departure", meta = (ClampMin = "50.0"))
	float MealCompletionApproachDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Departure", meta = (ClampMin = "0.0"))
	float MealCompletionApproachDelaySeconds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cafe Service|Departure")
	bool bAwaitingMealCompletionConfirmation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Social Initiative")
	bool bEnablePostServiceSocialInitiative;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Social Initiative", meta = (ClampMin = "50.0"))
	float SocialApproachRadius;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Social Initiative", meta = (ClampMin = "50.0"))
	float SocialApproachResetRadius;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Social Initiative", meta = (ClampMin = "0.0"))
	float SocialApproachRearmSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Social Initiative", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float SocialLookDotThreshold;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Social Initiative", meta = (ClampMin = "0.1"))
	float SocialLookHoldSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Social Initiative", meta = (ClampMin = "0.0"))
	float SocialOpportunityCooldownSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Social Initiative", meta = (ClampMin = "0.0"))
	float MinimumSecondsAfterOrderServedForSocialOpportunity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Social Initiative", meta = (ClampMin = "0.0"))
	float MinimumQuietSecondsBeforeSocialOpportunity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Social Initiative|Memory")
	bool bPreferMemoryTopicsForSocialInitiative;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Social Initiative|Memory", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SocialMemoryMinimumImportance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cafe Service|Social Initiative|Memory", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SocialMemoryAttentionAvailability;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cafe Service")
	bool bServiceStarted;

	UPROPERTY(BlueprintAssignable, Category = "Cafe Service")
	FCafeServiceStateChangedSignature OnCafeServiceStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Cafe Service")
	FMaidSpeechRequestedSignature OnMaidSpeechRequested;

	UFUNCTION(BlueprintCallable, Category = "Cafe Service")
	void StartServiceForPlayer(AActor* PlayerActor);

	UFUNCTION(BlueprintCallable, Category = "Cafe Service")
	void SubmitReceptionResponse(int32 PartySize, bool bWantsDining);

	UFUNCTION(BlueprintCallable, Category = "Cafe Service")
	void ResetService();

	UFUNCTION(BlueprintCallable, Category = "Cafe Service")
	bool CancelReceptionForExplicitNPCCommand(const FString& Reason = TEXT("explicit_npc_command"));

	UFUNCTION(BlueprintCallable, Category = "Cafe Service|Departure")
	bool SubmitMealCompletionConfirmation(bool bFinished);

	UFUNCTION(BlueprintCallable, Category = "Cafe Service|Social Initiative")
	bool TriggerSeatedConversationOpportunity();

private:
	UPROPERTY(Transient)
	UNPCMoveToTableComponent* MaidMoveComponent;

	UPROPERTY(Transient)
	UPlayerSeatComponent* PlayerSeatComponent;

	UPROPERTY(Transient)
	UNPCActionQueueComponent* MaidActionQueueComponent;

	FTimerHandle ReceptionGreetingTimerHandle;
	FTimerHandle DepartureEvaluationTimerHandle;
	FTimerHandle MealCompletionApproachDelayTimerHandle;

	FText PendingReceptionGreetingText;
	FString PendingDepartureSeatID;
	FString PendingDepartureTableID;
	FVector PendingDepartureOrigin;
	double OutsideTableAreaSinceWorldTime;
	bool bOrderServedForCurrentVisit;
	bool bDepartureConfirmedForCurrentVisit;
	bool bMaidApproachingForMealConfirmation;
	bool bSocialApproachConsumed;
	float SocialLookHeldSeconds;
	double SocialAwaySinceWorldTime;
	double LastSocialOpportunityWorldTime;
	double LastOrderServedWorldTime;
	FString LastServedItem;
	FString LastSocialTopicMemoryID;

	ATableMarkerActor* FindBestAvailableTable(int32 PartySize) const;

	UFUNCTION()
	void HandleMaidMoveCompleted(FName MoveReason, FString TargetID, bool bSuccess);

	UFUNCTION()
	void HandlePlayerSeated(FString SeatID, FString TableID);

	UFUNCTION()
	void HandlePlayerLeftSeat(FString SeatID, FString TableID);

	UFUNCTION()
	void HandleOrderStarted(FString TableID, FString Item);

	UFUNCTION()
	void HandleOrderServed(FString TableID, FString Item);

	void StartDepartureEvaluation();

	void EvaluatePendingDeparture();

	void ClearPendingDeparture();

	void ScheduleMealCompletionConfirmation();

	void BeginMealCompletionConfirmation();

	void ConfirmPlayerDeparture();

	void UpdatePostServiceSocialOpportunity(float DeltaSeconds);

	bool IsPlayerLookingAtMaid(float& OutLookDot) const;

	FString BuildSocialOpportunityContext(float DistanceToMaid, float LookDot, bool bSeatedConversation = false);

	bool SelectSocialTopicMemory(FNPCMemoryEntry& OutMemory, FNPCRecallScore& OutScore);

	const ATableMarkerActor* FindTableByID(const FString& TableID) const;

	void SetState(ECafeServiceState NewState);

	void RequestReceptionGreetingAfterDelay(const FText& SpeechText);

	void HandleReceptionGreetingDelayFinished();

	void RequestMaidSpeech(const FText& SpeechText);

	void PrintDebugMessage(const FString& Message, const FColor& Color) const;

	FString GetStateName(ECafeServiceState State) const;
};
