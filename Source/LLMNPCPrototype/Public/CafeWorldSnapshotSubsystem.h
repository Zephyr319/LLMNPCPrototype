// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CafeWorldSnapshot.h"
#include "CafeWorldSnapshotSubsystem.generated.h"

// Demand-driven read model of the cafe world.
// - No ticking, no cached FCafeWorldSnapshot, no writable mirror of gameplay state.
// - Actors/Components stay the single source of truth; every capture re-reads them.
// - Game thread only. Nothing here calls the LLM or mutates an Actor/Component.
UCLASS()
class LLMNPCPROTOTYPE_API UCafeWorldSnapshotSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	static UCafeWorldSnapshotSubsystem* Get(const UObject* WorldContextObject);

	// Optional manager selects the service/NPC view; null resolves the first manager in this world.
	UFUNCTION(BlueprintCallable, Category = "Cafe World Snapshot")
	FCafeWorldSnapshot CaptureSnapshot(ACafeServiceManagerActor* ServiceManager = nullptr);

	// Captures fresh state and serialises the whole snapshot (logging / experiment records, not for the LLM).
	UFUNCTION(BlueprintCallable, Category = "Cafe World Snapshot")
	FString CaptureJson(bool bPretty = false, ACafeServiceManagerActor* ServiceManager = nullptr);

	// Captures fresh state and projects a compact, purpose-specific context for LLM prompts.
	UFUNCTION(BlueprintCallable, Category = "Cafe World Snapshot")
	FString CaptureContext(ECafeSnapshotPurpose Purpose, const FString& FocusEntityId = TEXT(""),
		int32 MaxEntities = 6, ACafeServiceManagerActor* ServiceManager = nullptr);

	// Captures fresh state, prints a readable summary to the log and returns it.
	UFUNCTION(BlueprintCallable, Category = "Cafe World Snapshot")
	FString CaptureDebugLog(ACafeServiceManagerActor* ServiceManager = nullptr);

private:
	// Metadata only. Revision advances when the observed (non-volatile) state digest differs from the
	// previous capture, so changes that revert between two captures are not counted.
	FString LastStateDigest;
	int64 ObservedRevision = 0;
};
