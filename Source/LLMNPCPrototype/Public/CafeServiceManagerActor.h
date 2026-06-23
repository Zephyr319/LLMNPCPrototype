// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CafeServiceManagerActor.generated.h"

class ATableMarkerActor;
class UNPCMoveToTableComponent;
class UPlayerSeatComponent;

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

private:
	UPROPERTY(Transient)
	UNPCMoveToTableComponent* MaidMoveComponent;

	UPROPERTY(Transient)
	UPlayerSeatComponent* PlayerSeatComponent;

	ATableMarkerActor* FindBestAvailableTable(int32 PartySize) const;

	UFUNCTION()
	void HandleMaidMoveCompleted(FName MoveReason, FString TargetID, bool bSuccess);

	UFUNCTION()
	void HandlePlayerSeated(FString SeatID, FString TableID);

	void SetState(ECafeServiceState NewState);

	void RequestMaidSpeech(const FText& SpeechText);

	void PrintDebugMessage(const FString& Message, const FColor& Color) const;

	FString GetStateName(ECafeServiceState State) const;
};
