// Copyright Epic Games, Inc. All Rights Reserved.

#include "CafeWorldSnapshotSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FurnitureMarkerActor.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "NPCActionQueueComponent.h"
#include "NPCAutonomyComponent.h"
#include "NPCEmotionComponent.h"
#include "NPCFaceTargetComponent.h"
#include "NPCFoodCarryComponent.h"
#include "NPCMemoryComponent.h"
#include "NPCMoveToTableComponent.h"
#include "PlayerSeatComponent.h"
#include "TableMarkerActor.h"
#include "WaterablePlantActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogCafeWorldSnapshot, Log, All);

namespace CafeSnapshotPrivate
{
	// Tag the cleanup flow raises in the autonomy world state (see CafeServiceManagerActor / Autonomy).
	static const FName CleanupWorldTag(TEXT("dining_area_needs_tidy"));

	FString RuntimePhaseName(ENPCAutonomyRuntimePhase Phase)
	{
		switch (Phase)
		{
		case ENPCAutonomyRuntimePhase::None: return TEXT("None");
		case ENPCAutonomyRuntimePhase::EmotionReaction: return TEXT("EmotionReaction");
		case ENPCAutonomyRuntimePhase::BuiltInQueue: return TEXT("BuiltInQueue");
		case ENPCAutonomyRuntimePhase::SocialSeatEntering: return TEXT("SocialSeatEntering");
		case ENPCAutonomyRuntimePhase::SocialSeatSession: return TEXT("SocialSeatSession");
		case ENPCAutonomyRuntimePhase::SocialSeatExiting: return TEXT("SocialSeatExiting");
		case ENPCAutonomyRuntimePhase::BlueprintPreMove: return TEXT("BlueprintPreMove");
		case ENPCAutonomyRuntimePhase::BlueprintExecuting: return TEXT("BlueprintExecuting");
		case ENPCAutonomyRuntimePhase::BlueprintReturnMove: return TEXT("BlueprintReturnMove");
		default: return TEXT("Unknown");
		}
	}

	// Maps a held item's attach socket to the hand it occupies. Unknown sockets block both hands
	// (conservative: an item we cannot place must not be reported as "free hands").
	void MarkHands(const FString& Socket, const FString& Item, FCafeBodySnapshot& Body)
	{
		const FString Label = FString::Printf(TEXT("holding:%s"), *Item);
		const FString Lower = Socket.ToLower();
		const bool bLeft = Lower.Contains(TEXT("left"));
		const bool bRight = Lower.Contains(TEXT("right"));
		if (bLeft || !bRight)
		{
			Body.LeftHand = Label;
		}
		if (bRight || !bLeft)
		{
			Body.RightHand = Label;
		}
	}
}

// Console: Cafe.Snapshot [log|json|pretty|planning|dialogue|interrupt] [FocusEntityId]
// Debug entry point only; it captures a fresh snapshot and prints it to the Output Log.
static FAutoConsoleCommandWithWorldAndArgs GCafeSnapshotConsoleCommand(
	TEXT("Cafe.Snapshot"),
	TEXT("Print the cafe world snapshot. Args: [log|json|pretty|planning|dialogue|interrupt] [FocusEntityId]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UCafeWorldSnapshotSubsystem* Subsystem = UCafeWorldSnapshotSubsystem::Get(World);
		if (!Subsystem)
		{
			UE_LOG(LogCafeWorldSnapshot, Warning, TEXT("Cafe.Snapshot: no snapshot subsystem (run it during PIE or in game)."));
			return;
		}

		const FString Mode = Args.Num() > 0 ? Args[0].ToLower() : FString(TEXT("log"));
		const FString Focus = Args.Num() > 1 ? Args[1] : FString();
		if (Mode == TEXT("json") || Mode == TEXT("pretty"))
		{
			UE_LOG(LogCafeWorldSnapshot, Log, TEXT("\n%s"), *Subsystem->CaptureJson(Mode == TEXT("pretty")));
		}
		else if (Mode == TEXT("planning") || Mode == TEXT("dialogue") || Mode == TEXT("interrupt"))
		{
			const ECafeSnapshotPurpose Purpose = Mode == TEXT("planning") ? ECafeSnapshotPurpose::Planning
				: Mode == TEXT("dialogue") ? ECafeSnapshotPurpose::PlayerDialogue
				: ECafeSnapshotPurpose::TaskInterruption;
			const FString Context = Subsystem->CaptureContext(Purpose, Focus);
			UE_LOG(LogCafeWorldSnapshot, Log, TEXT("[%s] chars=%d\n%s"), *Mode, Context.Len(), *Context);
		}
		else
		{
			const FString Summary = Subsystem->CaptureDebugLog();
			// Also show it in the game viewport so it is visible during a demo without the Output Log.
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(static_cast<uint64>(0xCAFE5A9), 20.0f, FColor::Cyan, Summary);
			}
		}
	}));

bool UCafeWorldSnapshotSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UCafeWorldSnapshotSubsystem* UCafeWorldSnapshotSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UCafeWorldSnapshotSubsystem>() : nullptr;
}

FCafeWorldSnapshot UCafeWorldSnapshotSubsystem::CaptureSnapshot(ACafeServiceManagerActor* ServiceManager)
{
	using namespace CafeSnapshotPrivate;

	FCafeWorldSnapshot Snapshot;
	UWorld* World = GetWorld();
	if (!World)
	{
		Snapshot.Diagnostics.Add(TEXT("world_missing"));
		return Snapshot;
	}

	Snapshot.CapturedAtUtc = FDateTime::UtcNow().ToIso8601();
	Snapshot.WorldTimeSeconds = World->GetTimeSeconds();
	Snapshot.WorldId = World->GetName();

	// ---- Resolve sources (read-only pointers; nothing is stored beyond this call) ----
	if (!ServiceManager)
	{
		ServiceManager = ACafeServiceManagerActor::FindForWorld(World);
	}

	AActor* NPCActor = ServiceManager ? ServiceManager->MaidNPCActor : nullptr;
	UNPCAutonomyComponent* Autonomy = ServiceManager ? ServiceManager->AutonomyComponent : nullptr;
	UNPCEmotionComponent* EmotionComp = ServiceManager ? ServiceManager->EmotionComponent : nullptr;
	UNPCMemoryComponent* MemoryComp = ServiceManager ? ServiceManager->MemoryComponent : nullptr;

	UNPCActionQueueComponent* Queue = nullptr;
	UNPCFoodCarryComponent* FoodCarry = nullptr;
	UNPCFaceTargetComponent* FaceTarget = nullptr;
	UNPCMoveToTableComponent* MoveComp = nullptr;
	if (NPCActor)
	{
		Queue = NPCActor->FindComponentByClass<UNPCActionQueueComponent>();
		FoodCarry = NPCActor->FindComponentByClass<UNPCFoodCarryComponent>();
		FaceTarget = NPCActor->FindComponentByClass<UNPCFaceTargetComponent>();
		MoveComp = NPCActor->FindComponentByClass<UNPCMoveToTableComponent>();
	}
	if (!Queue && ServiceManager)
	{
		Queue = ServiceManager->FindComponentByClass<UNPCActionQueueComponent>();
	}
	if (!MoveComp && ServiceManager)
	{
		MoveComp = ServiceManager->FindComponentByClass<UNPCMoveToTableComponent>();
	}

	AActor* PlayerActor = ServiceManager ? ServiceManager->CurrentPlayerActor : nullptr;
	if (!PlayerActor)
	{
		PlayerActor = UGameplayStatics::GetPlayerPawn(World, 0);
	}
	const UPlayerSeatComponent* PlayerSeat = PlayerActor ? PlayerActor->FindComponentByClass<UPlayerSeatComponent>() : nullptr;

	// ---- Service ----
	if (ServiceManager)
	{
		Snapshot.bServiceAvailable = true;
		Snapshot.ServiceManagerId = ServiceManager->GetName();
		Snapshot.Service = ServiceManager->GetServiceSnapshot();
	}
	else
	{
		Snapshot.Diagnostics.Add(TEXT("service_manager_missing"));
	}

	// ---- Player ----
	{
		FCafePlayerSnapshot& Player = Snapshot.Player;
		Player.bExists = PlayerActor != nullptr;
		Player.bInServiceScene = ServiceManager
			&& ServiceManager->CurrentPlayerActor
			&& ServiceManager->CurrentPlayerActor == PlayerActor;
		Player.ActorId = PlayerActor ? PlayerActor->GetName() : FString();
		Player.bInTableArea = Player.bInServiceScene && Snapshot.Service.bPlayerInTableArea;
		if (PlayerSeat)
		{
			Player.bSeatStateKnown = true;
			Player.bSeated = PlayerSeat->bIsSeated;
			Player.SeatId = PlayerSeat->bIsSeated ? PlayerSeat->CurrentSeatID : FString();
			Player.TableId = PlayerSeat->bIsSeated ? PlayerSeat->CurrentTableID : FString();
		}
		else if (PlayerActor)
		{
			Snapshot.Diagnostics.Add(TEXT("player_seat_component_missing"));
		}
		if (Player.TableId.IsEmpty() && Player.bInTableArea)
		{
			Player.TableId = Snapshot.Service.CurrentTableID;
		}

		if (PlayerActor && NPCActor)
		{
			FVector Forward = NPCActor->GetActorForwardVector();
			FVector ToPlayer = PlayerActor->GetActorLocation() - NPCActor->GetActorLocation();
			Forward.Z = 0.0f;
			ToPlayer.Z = 0.0f;
			Player.DistanceToNPC = ToPlayer.Size();
			if (Forward.Normalize() && ToPlayer.Normalize())
			{
				const float Dot = FMath::Clamp(FVector::DotProduct(Forward, ToPlayer), -1.0f, 1.0f);
				Player.AngleFromNPCForwardDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));
				Player.RearFactor = FMath::Clamp((1.0f - Dot) * 0.5f, 0.0f, 1.0f);
			}
		}

		const bool bPlayerMoving = PlayerActor
			&& PlayerActor->GetVelocity().SizeSquared2D() > FMath::Square(10.0f);
		Player.Pose = !Player.bExists ? TEXT("absent")
			: Player.bSeated ? TEXT("seated")
			: bPlayerMoving ? TEXT("walking")
			: TEXT("standing");

		if (!Player.bInServiceScene)
		{
			Player.Activity = TEXT("absent");
		}
		else if (Snapshot.Service.bGuideInProgress
			|| Snapshot.Service.State == ECafeServiceState::GuidingToSeat)
		{
			Player.Activity = TEXT("following_npc");
		}
		else if (Player.bSeated)
		{
			Player.Activity = TEXT("seated");
		}
		else if (bPlayerMoving)
		{
			Player.Activity = TEXT("walking");
		}
		else
		{
			// Posture is authoritative; the pushed PlayerContext only refines the standing case.
			const FString Reported = Autonomy ? Autonomy->PlayerContext.Activity : FString();
			const bool bReportedPosture = Reported.Equals(TEXT("seated"), ESearchCase::IgnoreCase)
				|| Reported.Equals(TEXT("walking"), ESearchCase::IgnoreCase)
				|| Reported.Equals(TEXT("absent"), ESearchCase::IgnoreCase);
			Player.Activity = (Reported.IsEmpty()
				|| Reported.Equals(TEXT("unknown"), ESearchCase::IgnoreCase)
				|| bReportedPosture)
				? TEXT("idle") : Reported;
		}

		if (!Player.bInServiceScene)
		{
			Player.Zone = TEXT("outside");
		}
		else
		{
			const FString ReportedZone = Autonomy ? Autonomy->PlayerContext.Zone.TrimStartAndEnd() : FString();
			if (!ReportedZone.IsEmpty() && !ReportedZone.Equals(TEXT("unknown"), ESearchCase::IgnoreCase))
			{
				Player.Zone = ReportedZone;
			}
			else if (Player.bInTableArea)
			{
				Player.Zone = Player.TableId.IsEmpty() ? TEXT("table_area") : Player.TableId;
			}
			else if (Snapshot.Service.PhaseName == TEXT("reception"))
			{
				Player.Zone = TEXT("entrance");
			}
			else
			{
				Player.Zone = TEXT("dining_area");
			}
		}
	}

	// ---- NPC ----
	{
		FCafeNPCSnapshot& NPC = Snapshot.NPC;
		NPC.bExists = NPCActor != nullptr;
		NPC.ActorId = NPCActor ? NPCActor->GetName() : FString();
		if (Autonomy)
		{
			NPC.bAutonomyAvailable = true;
			NPC.ActivityId = Autonomy->CurrentActivityID;
			NPC.bActivityRunning = Autonomy->bActivityRunning;
			if (!NPC.ActivityId.IsEmpty())
			{
				if (const FNPCAutonomyActivityDefinition* Def = Autonomy->FindActivityDefinition(NPC.ActivityId))
				{
					NPC.ActivityTarget = Def->TargetSceneTag;
				}
			}
			if (MoveComp && MoveComp->GetLastSelectedSceneTargetWorldTime() >= 0.0)
			{
				NPC.SelectedTargetId = MoveComp->GetLastSelectedSceneTargetID();
				NPC.SelectedTargetReason = MoveComp->GetLastSelectedSceneTargetReason().ToString();
				NPC.SelectedTargetAgeSeconds = static_cast<float>(World->GetTimeSeconds() - MoveComp->GetLastSelectedSceneTargetWorldTime());
			}
			NPC.RuntimePhase = RuntimePhaseName(Autonomy->RuntimePhase);
			NPC.Zone = Autonomy->LastKnownNPCZone.IsEmpty() ? FString(TEXT("unknown")) : Autonomy->LastKnownNPCZone;
			NPC.bDecisionPending = Autonomy->bDecisionPending;
			NPC.bAppraisalPending = Autonomy->bAppraisalPending;
			NPC.bAwaitingEmbodiedResponse = Autonomy->bAwaitingEmbodiedResponse;
			NPC.bResumeAfterReply = Autonomy->bResumePausedActivityAfterReply;
			NPC.Plan = Autonomy->ActivePlan;
			if (NPC.Plan.Steps.IsValidIndex(NPC.Plan.CurrentStepIndex))
			{
				NPC.CurrentPlanStep = NPC.Plan.Steps[NPC.Plan.CurrentStepIndex];
			}
			NPC.Motives = Autonomy->MotiveState;
			NPC.Resources = Autonomy->ResourceState;
		}
		else if (ServiceManager)
		{
			Snapshot.Diagnostics.Add(TEXT("autonomy_component_missing"));
		}
		NPC.bBusy = NPC.bActivityRunning || NPC.bAwaitingEmbodiedResponse
			|| (Queue && Queue->IsBusyForAutonomy());
	}

	// ---- Action queue / execution ----
	{
		FCafeActionQueueSnapshot& Exec = Snapshot.Execution;
		Exec.bAvailable = Queue != nullptr;
		if (Queue)
		{
			Exec.bRunning = Queue->bIsRunningQueue;
			Exec.PendingActionCount = Queue->ActionQueue.Num();
			Exec.bWaitingForMove = Queue->bWaitingForMove;
			Exec.bAnimationActive = Queue->HasActiveQueuedAnimation();
			Exec.bAnimationPaused = Queue->bQueuedAnimationPaused;
			Exec.bSeated = Queue->IsNPCSeated();
			Exec.ReservedSeatId = Queue->ReservedSeat ? Queue->ReservedSeat->FurnitureID : FString();

			// CurrentAction keeps its last value after the queue stops, so only report it while live.
			const bool bActionLive = Queue->bIsRunningQueue || Queue->bWaitingForMove || Queue->bQueuedAnimationInProgress;
			if (bActionLive)
			{
				const UEnum* ActionEnum = StaticEnum<ENPCQueuedActionType>();
				Exec.CurrentAction = ActionEnum
					? ActionEnum->GetNameStringByValue(static_cast<int64>(Queue->CurrentAction.ActionType))
					: FString(TEXT("Unknown"));
				Exec.Target = Queue->CurrentAction.Target;
				if (Exec.bAnimationActive && Queue->CurrentAction.Animation)
				{
					Exec.AnimationAsset = Queue->CurrentAction.Animation->GetName();
				}
			}

			if (IsValid(Queue->ActiveHeldPropComponent))
			{
				const UStaticMeshComponent* Prop = Queue->ActiveHeldPropComponent;
				Exec.HeldPropAsset = Prop->GetStaticMesh() ? Prop->GetStaticMesh()->GetName() : Prop->GetName();
				Exec.HeldPropSocket = Prop->GetAttachSocketName().ToString();
			}

			Exec.ServiceGesture = Queue->bBlockingGestureInProgress ? Queue->CurrentBlockingGestureName : FString();
			Exec.bPausedForServiceGesture = MoveComp && MoveComp->IsMovementPausedByServiceGesture();
			Exec.bMovementLocked = Queue->IsBowMovementLockActive() || Exec.bPausedForServiceGesture;
			Exec.bDeferredActionPending = Queue->bHasDeferredStrategyAction;
			if (Exec.bDeferredActionPending)
			{
				Exec.DeferredAction = FString::Printf(TEXT("%s:%s"),
					*Queue->DeferredStrategyActionType, *Queue->DeferredStrategyActionItem);
			}
		}
	}

	// ---- Body resources (derived from execution facts above) ----
	{
		FCafeBodySnapshot& Body = Snapshot.Body;
		const FCafeActionQueueSnapshot& Exec = Snapshot.Execution;
		Body.bExecutionStateKnown = Queue != nullptr;
		if (Body.bExecutionStateKnown)
		{
			Body.bMoving = Exec.bWaitingForMove
				|| (MoveComp && (MoveComp->bIsMovingToTable || MoveComp->IsFollowingPlayer() || MoveComp->IsGuidingPlayer()));
			Body.bFacingTarget = FaceTarget && FaceTarget->IsFacingActor();
			Body.Locomotion = Exec.bSeated ? TEXT("seated")
				: Exec.bMovementLocked ? TEXT("locked")
				: Body.bMoving ? TEXT("moving") : TEXT("idle");
			Body.UpperBody = !Exec.ServiceGesture.IsEmpty()
				? FString::Printf(TEXT("gesture:%s"), *Exec.ServiceGesture)
				: Exec.bAnimationActive ? (Exec.bAnimationPaused ? TEXT("animation_paused") : TEXT("animation"))
				: TEXT("free");
			Body.LeftHand = TEXT("free");
			Body.RightHand = TEXT("free");

			if (!Exec.HeldPropAsset.IsEmpty())
			{
				FCafeHeldPropSnapshot Prop;
				Prop.Source = TEXT("action_queue_prop");
				Prop.Item = Exec.HeldPropAsset;
				Prop.AssetOrActorId = Exec.HeldPropAsset;
				Prop.Socket = Exec.HeldPropSocket;
				Body.HeldProps.Add(Prop);
				MarkHands(Prop.Socket, Prop.Item, Body);
			}
		}
		if (FoodCarry && FoodCarry->IsCarryingFood())
		{
			FCafeHeldPropSnapshot Food;
			Food.Source = TEXT("food_carry");
			Food.Item = FoodCarry->CarriedFoodItemName;
			if (FoodCarry->CarriedFoodActor)
			{
				Food.AssetOrActorId = FoodCarry->CarriedFoodActor->GetName();
				Food.Socket = FoodCarry->CarriedFoodActor->GetAttachParentSocketName().ToString();
			}
			Body.HeldProps.Add(Food);
			MarkHands(Food.Socket, Food.Item, Body);
		}
	}

	// ---- Emotion / memory (stored values only; decay is applied by the component itself) ----
	if (EmotionComp)
	{
		Snapshot.bEmotionAvailable = true;
		Snapshot.Emotion = EmotionComp->GetCurrentState();
	}
	if (MemoryComp)
	{
		FCafeMemorySnapshot& Memory = Snapshot.Memory;
		Memory.bAvailable = true;
		Memory.Count = MemoryComp->Memories.Num();
		Memory.LastRecalledId = MemoryComp->LastRecalledMemoryID;
		FNPCMemoryEntry Entry;
		if (!Memory.LastRecalledId.IsEmpty() && MemoryComp->FindMemoryByID(Memory.LastRecalledId, Entry))
		{
			Memory.LastRecalledSummary = Entry.Summary;
		}
		Memory.LastAppraisedSummary = Autonomy ? Autonomy->LastAppraisedMemorySummary : FString();
	}

	// ---- Seats ----
	for (TActorIterator<AFurnitureMarkerActor> It(World); It; ++It)
	{
		const AFurnitureMarkerActor* Seat = *It;
		// Only furniture that can host someone is a "seat"; counters etc. are skipped.
		if (!Seat || !(Seat->bCanSit || Seat->bAllowNPCSit))
		{
			continue;
		}

		FCafeSeatSnapshot Out;
		Out.Id = Seat->FurnitureID.IsEmpty() ? Seat->GetName() : Seat->FurnitureID;
		Out.ActorId = Seat->GetName();
		Out.TableId = Seat->RelatedTableID;
		Out.bCanSit = Seat->bCanSit;
		Out.bAllowNPCSit = Seat->bAllowNPCSit;

		// The marker uses one flag for "reserved" and "occupied"; the holder's real posture splits them.
		const bool bHeld = Seat->bIsOccupied;
		if (bHeld && PlayerActor && Seat->IsReservedBy(PlayerActor))
		{
			Out.OccupantId = TEXT("player");
			Out.bOccupantSeated = PlayerSeat && PlayerSeat->bIsSeated && PlayerSeat->CurrentSeatID == Out.Id;
		}
		else if (bHeld && NPCActor && Seat->IsReservedBy(NPCActor))
		{
			Out.OccupantId = TEXT("npc");
			Out.bOccupantSeated = Queue && Queue->IsNPCSeated() && Queue->GetNPCSeatID() == Out.Id;
		}
		else if (bHeld)
		{
			Out.OccupantId = TEXT("unknown");
		}
		Out.bOccupied = bHeld && Out.bOccupantSeated;
		Out.bReserved = bHeld && !Out.bOccupantSeated;
		Out.bAvailable = Seat->bCanSit && !bHeld;
		Out.bAvailableForNPC = Seat->bAllowNPCSit && NPCActor && Seat->IsSeatAvailableFor(NPCActor);
		Snapshot.Seats.Add(MoveTemp(Out));
	}
	Snapshot.Seats.Sort([](const FCafeSeatSnapshot& A, const FCafeSeatSnapshot& B) { return A.Id < B.Id; });

	// ---- Tables ----
	const bool bCleanupTagRaised = Autonomy && Autonomy->GetWorldStateTag(CleanupWorldTag);
	for (TActorIterator<ATableMarkerActor> It(World); It; ++It)
	{
		const ATableMarkerActor* Table = *It;
		if (!Table)
		{
			continue;
		}

		FCafeTableSnapshot Out;
		Out.Id = Table->GetTableID().IsEmpty() ? Table->GetName() : Table->GetTableID();
		Out.ActorId = Table->GetName();
		Out.bMarkerOccupied = Table->IsTableOccupied();
		Out.Capacity = Table->GetSeatCount();
		for (const FCafeSeatSnapshot& Seat : Snapshot.Seats)
		{
			if (Seat.TableId == Out.Id && (Seat.bOccupied || Seat.bReserved))
			{
				++Out.OccupiedOrReservedSeats;
			}
		}
		Out.bOccupied = Out.bMarkerOccupied || Out.OccupiedOrReservedSeats > 0;

		// No per-table dirty flag exists: cleanup is inferred from the global tidy tag plus the table of
		// the last completed service. Anything we cannot attribute stays "unknown" instead of guessed.
		if (!Autonomy)
		{
			Out.CleanupEvidence = TEXT("autonomy_unavailable");
		}
		else if (!bCleanupTagRaised)
		{
			Out.bCleanupStateKnown = true;
			Out.CleanupEvidence = TEXT("tidy_tag_clear");
		}
		else if (!Snapshot.Service.CompletedServiceTableID.IsEmpty() && Snapshot.Service.CompletedServiceTableID == Out.Id)
		{
			Out.bCleanupStateKnown = true;
			Out.bNeedsCleanup = true;
			Out.CleanupEvidence = TEXT("tidy_tag_raised+completed_service_table");
		}
		else if (!Snapshot.Service.CompletedServiceTableID.IsEmpty())
		{
			Out.bCleanupStateKnown = true;
			Out.CleanupEvidence = TEXT("tidy_tag_raised_for_other_table");
		}
		else
		{
			Out.CleanupEvidence = TEXT("tidy_tag_raised_table_unattributed");
		}
		Snapshot.Tables.Add(MoveTemp(Out));
	}
	Snapshot.Tables.Sort([](const FCafeTableSnapshot& A, const FCafeTableSnapshot& B) { return A.Id < B.Id; });

	// ---- Plants ----
	// Tag aggregates are recomputed from the plants themselves so the result never lags the actor state.
	TMap<FName, bool> DerivedPlantTags;
	for (TActorIterator<AWaterablePlantActor> It(World); It; ++It)
	{
		const AWaterablePlantActor* Plant = *It;
		if (!Plant)
		{
			continue;
		}

		FCafePlantSnapshot Out;
		Out.Id = Plant->PlantID.IsEmpty() ? Plant->GetName() : Plant->PlantID;
		Out.ActorId = Plant->GetName();
		Out.WaterNeed = Plant->WaterNeed;
		Out.NeedsWaterThreshold = Plant->NeedsWaterThreshold;
		Out.Moisture = Plant->GetMoisture();
		Out.Tier = Plant->GetMoistureTierName();
		Out.Urgency = Plant->GetWaterUrgency01();
		Out.EnterMoisture = Plant->NeedsWaterEnterMoisture;
		Out.ClearMoisture = Plant->NeedsWaterClearMoisture;
		Out.ReservedBy = Plant->GetReservedByName();
		Out.bEnabled = Plant->bEnabled;
		Out.bCachedNeedsWater = Plant->bNeedsWater;
		// The plant owns the hysteresis state; read it instead of re-deriving it from a threshold.
		Out.bNeedsWater = Plant->NeedsWater();
		Out.bReserved = Plant->IsReserved();
		Out.bAvailable = Plant->bEnabled && !Out.bReserved && (!Plant->bOnlyTargetWhenNeedsWater || Out.bNeedsWater);
		Out.Priority = Plant->Priority;
		Out.bSyncWorldTag = Plant->bSyncAutonomyWorldState;
		Out.WorldTag = Plant->AutonomyWorldStateTag;

		if (Out.bSyncWorldTag && !Out.WorldTag.IsNone())
		{
			bool& bAny = DerivedPlantTags.FindOrAdd(Out.WorldTag, false);
			bAny = bAny || Out.bNeedsWater;
		}
		Snapshot.bAnyPlantNeedsWater = Snapshot.bAnyPlantNeedsWater || Out.bNeedsWater;
		Snapshot.Plants.Add(MoveTemp(Out));
	}
	Snapshot.Plants.Sort([](const FCafePlantSnapshot& A, const FCafePlantSnapshot& B) { return A.Id < B.Id; });

	// ---- World state tags ----
	if (Autonomy)
	{
		for (const TPair<FName, bool>& Pair : Autonomy->WorldStateTags)
		{
			if (Pair.Value)
			{
				Snapshot.ReportedWorldStateTags.Add(Pair.Key);
			}
		}
	}
	Snapshot.ReportedWorldStateTags.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });

	for (const FName& Tag : Snapshot.ReportedWorldStateTags)
	{
		// Plant-driven tags are authoritative from the plants; everything else is taken as reported.
		if (const bool* bDerived = DerivedPlantTags.Find(Tag))
		{
			if (!*bDerived)
			{
				Snapshot.Diagnostics.Add(FString::Printf(TEXT("world_tag_stale_true:%s"), *Tag.ToString()));
				continue;
			}
		}
		Snapshot.ActiveWorldStateTags.Add(Tag);
	}
	for (const TPair<FName, bool>& Pair : DerivedPlantTags)
	{
		if (Pair.Value && !Snapshot.ActiveWorldStateTags.Contains(Pair.Key))
		{
			if (Autonomy)
			{
				Snapshot.Diagnostics.Add(FString::Printf(TEXT("world_tag_missing_true:%s"), *Pair.Key.ToString()));
			}
			Snapshot.ActiveWorldStateTags.Add(Pair.Key);
		}
	}
	Snapshot.ActiveWorldStateTags.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });

	// ---- Revision ----
	const FString Digest = FCafeWorldSnapshotJson::StateDigest(Snapshot);
	if (Digest != LastStateDigest)
	{
		LastStateDigest = Digest;
		++ObservedRevision;
	}
	Snapshot.Revision = ObservedRevision;
	return Snapshot;
}

FString UCafeWorldSnapshotSubsystem::CaptureJson(bool bPretty, ACafeServiceManagerActor* ServiceManager)
{
	return FCafeWorldSnapshotJson::ToJson(CaptureSnapshot(ServiceManager), bPretty);
}

FString UCafeWorldSnapshotSubsystem::CaptureContext(ECafeSnapshotPurpose Purpose, const FString& FocusEntityId,
	int32 MaxEntities, ACafeServiceManagerActor* ServiceManager)
{
	return FCafeWorldSnapshotJson::BuildContext(CaptureSnapshot(ServiceManager), Purpose, FocusEntityId, MaxEntities);
}

FString UCafeWorldSnapshotSubsystem::CaptureDebugLog(ACafeServiceManagerActor* ServiceManager)
{
	const FString Summary = FCafeWorldSnapshotJson::DebugSummary(CaptureSnapshot(ServiceManager));
	UE_LOG(LogCafeWorldSnapshot, Log, TEXT("\n%s"), *Summary);
	return Summary;
}
