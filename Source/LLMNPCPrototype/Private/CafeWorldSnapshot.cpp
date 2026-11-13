// Copyright Epic Games, Inc. All Rights Reserved.

#include "CafeWorldSnapshot.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/SecureHash.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Pure projections of an already captured FCafeWorldSnapshot. Nothing in this file touches the world.

namespace
{
	using FObj = TSharedRef<FJsonObject>;
	using FValues = TArray<TSharedPtr<FJsonValue>>;

	double Round2(double Value)
	{
		return FMath::RoundToDouble(Value * 100.0) / 100.0;
	}

	FObj NewObj()
	{
		return MakeShared<FJsonObject>();
	}

	TSharedPtr<FJsonValue> ObjValue(const FObj& Obj)
	{
		return MakeShared<FJsonValueObject>(Obj);
	}

	FValues StringValues(const TArray<FString>& Items)
	{
		FValues Out;
		for (const FString& Item : Items)
		{
			Out.Add(MakeShared<FJsonValueString>(Item));
		}
		return Out;
	}

	FValues NameValues(const TArray<FName>& Items, int32 Limit = MAX_int32)
	{
		FValues Out;
		for (const FName& Item : Items)
		{
			if (Out.Num() >= Limit)
			{
				break;
			}
			Out.Add(MakeShared<FJsonValueString>(Item.ToString()));
		}
		return Out;
	}

	// Compact projections omit empty strings so unused fields cost no tokens.
	void SetNonEmpty(const FObj& Obj, const TCHAR* Key, const FString& Value)
	{
		if (!Value.IsEmpty())
		{
			Obj->SetStringField(Key, Value);
		}
	}

	FString PlanStatusName(ENPCAutonomyPlanStatus Status)
	{
		const UEnum* Enum = StaticEnum<ENPCAutonomyPlanStatus>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Status)) : FString(TEXT("Unknown"));
	}

	FString PurposeName(ECafeSnapshotPurpose Purpose)
	{
		switch (Purpose)
		{
		case ECafeSnapshotPurpose::Planning: return TEXT("planning");
		case ECafeSnapshotPurpose::PlayerDialogue: return TEXT("player_dialogue");
		case ECafeSnapshotPurpose::TaskInterruption: return TEXT("task_interruption");
		default: return TEXT("unknown");
		}
	}

	FString Serialize(const FObj& Obj, bool bPretty)
	{
		FString Out;
		if (bPretty)
		{
			TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
			FJsonSerializer::Serialize(Obj, Writer);
		}
		else
		{
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
			FJsonSerializer::Serialize(Obj, Writer);
		}
		return Out;
	}

	// ---------- Entity builders (full detail) ----------

	FObj ServiceJson(const FCafeServiceSnapshot& S)
	{
		FObj O = NewObj();
		O->SetStringField(TEXT("state"), S.PhaseName);
		O->SetStringField(TEXT("phase"), S.PhaseName);
		O->SetStringField(TEXT("raw_state"), S.StateName);
		O->SetBoolField(TEXT("started"), S.bServiceStarted);
		O->SetBoolField(TEXT("player_in_table_area"), S.bPlayerInTableArea);
		O->SetBoolField(TEXT("player_seated"), S.bPlayerSeated);
		O->SetBoolField(TEXT("seated_for_service"), S.bSeatedForService);
		O->SetBoolField(TEXT("guide_in_progress"), S.bGuideInProgress);
		O->SetBoolField(TEXT("seating_guidance_completed"), S.bSeatingGuidanceCompleted);
		O->SetStringField(TEXT("current_table_id"), S.CurrentTableID);
		O->SetStringField(TEXT("guide_target_id"), S.CurrentGuideTargetID);
		O->SetBoolField(TEXT("meal_completion_confirmation_pending"), S.bMealCompletionConfirmationPending);
		O->SetStringField(TEXT("meal_completion_table_id"), S.MealCompletionTableID);
		O->SetBoolField(TEXT("dining_service_completed"), S.bDiningServiceCompleted);
		O->SetStringField(TEXT("completed_service_table_id"), S.CompletedServiceTableID);
		return O;
	}

	FObj PlayerJson(const FCafePlayerSnapshot& P)
	{
		FObj O = NewObj();
		O->SetBoolField(TEXT("exists"), P.bExists);
		O->SetBoolField(TEXT("in_service_scene"), P.bInServiceScene);
		O->SetBoolField(TEXT("seat_state_known"), P.bSeatStateKnown);
		O->SetStringField(TEXT("actor_id"), P.ActorId);
		O->SetBoolField(TEXT("seated"), P.bSeated);
		O->SetBoolField(TEXT("in_table_area"), P.bInTableArea);
		O->SetStringField(TEXT("seat_id"), P.SeatId);
		O->SetStringField(TEXT("table_id"), P.TableId);
		O->SetStringField(TEXT("pose"), P.Pose);
		O->SetStringField(TEXT("zone"), P.Zone);
		O->SetStringField(TEXT("activity"), P.Activity);
		O->SetNumberField(TEXT("distance_to_npc"), Round2(P.DistanceToNPC));
		O->SetNumberField(TEXT("angle_from_npc_forward_deg"), Round2(P.AngleFromNPCForwardDeg));
		O->SetNumberField(TEXT("rear_factor"), Round2(P.RearFactor));
		return O;
	}

	FObj PlanJson(const FNPCAutonomyPlan& Plan, bool bAllSteps)
	{
		FObj O = NewObj();
		O->SetStringField(TEXT("status"), PlanStatusName(Plan.Status));
		O->SetStringField(TEXT("goal"), Plan.Goal);
		O->SetStringField(TEXT("commitment"), Plan.Commitment);
		O->SetStringField(TEXT("trigger"), Plan.Trigger);
		O->SetNumberField(TEXT("current_step_index"), Plan.CurrentStepIndex);
		O->SetNumberField(TEXT("step_count"), Plan.Steps.Num());
		if (bAllSteps)
		{
			O->SetArrayField(TEXT("steps"), StringValues(Plan.Steps));
		}
		return O;
	}

	FObj MotivesJson(const FCafeNPCSnapshot& N)
	{
		FObj O = NewObj();
		O->SetNumberField(TEXT("boredom"), Round2(N.Motives.Boredom));
		O->SetNumberField(TEXT("duty"), Round2(N.Motives.Duty));
		O->SetNumberField(TEXT("social_attention"), Round2(N.Motives.SocialAttention));
		O->SetNumberField(TEXT("energy"), Round2(N.Resources.Energy));
		return O;
	}

	FObj NpcJson(const FCafeNPCSnapshot& N)
	{
		FObj O = NewObj();
		O->SetBoolField(TEXT("exists"), N.bExists);
		O->SetStringField(TEXT("actor_id"), N.ActorId);
		O->SetBoolField(TEXT("autonomy_available"), N.bAutonomyAvailable);
		O->SetStringField(TEXT("activity_id"), N.ActivityId);
		O->SetStringField(TEXT("activity_target"), N.ActivityTarget);
		O->SetStringField(TEXT("selected_target_id"), N.SelectedTargetId);
		O->SetStringField(TEXT("selected_target_reason"), N.SelectedTargetReason);
		O->SetNumberField(TEXT("selected_target_age_seconds"), Round2(N.SelectedTargetAgeSeconds));
		O->SetBoolField(TEXT("activity_running"), N.bActivityRunning);
		O->SetBoolField(TEXT("busy"), N.bBusy);
		O->SetStringField(TEXT("runtime_phase"), N.RuntimePhase);
		O->SetStringField(TEXT("zone"), N.Zone);
		O->SetBoolField(TEXT("decision_pending"), N.bDecisionPending);
		O->SetBoolField(TEXT("appraisal_pending"), N.bAppraisalPending);
		O->SetBoolField(TEXT("awaiting_embodied_response"), N.bAwaitingEmbodiedResponse);
		O->SetBoolField(TEXT("resume_after_reply"), N.bResumeAfterReply);
		O->SetObjectField(TEXT("plan"), PlanJson(N.Plan, true));
		O->SetStringField(TEXT("current_plan_step"), N.CurrentPlanStep);
		O->SetObjectField(TEXT("motives"), MotivesJson(N));
		return O;
	}

	FObj ExecutionJson(const FCafeActionQueueSnapshot& E)
	{
		FObj O = NewObj();
		O->SetBoolField(TEXT("available"), E.bAvailable);
		O->SetBoolField(TEXT("running"), E.bRunning);
		O->SetStringField(TEXT("current_action"), E.CurrentAction);
		O->SetStringField(TEXT("target"), E.Target);
		O->SetNumberField(TEXT("pending_action_count"), E.PendingActionCount);
		O->SetBoolField(TEXT("waiting_for_move"), E.bWaitingForMove);
		O->SetBoolField(TEXT("animation_active"), E.bAnimationActive);
		O->SetBoolField(TEXT("animation_paused"), E.bAnimationPaused);
		O->SetStringField(TEXT("animation_asset"), E.AnimationAsset);
		O->SetBoolField(TEXT("seated"), E.bSeated);
		O->SetStringField(TEXT("reserved_seat_id"), E.ReservedSeatId);
		O->SetStringField(TEXT("held_prop_asset"), E.HeldPropAsset);
		O->SetStringField(TEXT("held_prop_socket"), E.HeldPropSocket);
		O->SetStringField(TEXT("service_gesture"), E.ServiceGesture);
		O->SetBoolField(TEXT("movement_locked"), E.bMovementLocked);
		O->SetBoolField(TEXT("paused_for_service_gesture"), E.bPausedForServiceGesture);
		O->SetBoolField(TEXT("deferred_action_pending"), E.bDeferredActionPending);
		O->SetStringField(TEXT("deferred_action"), E.DeferredAction);
		return O;
	}

	FObj HeldPropJson(const FCafeHeldPropSnapshot& P)
	{
		FObj O = NewObj();
		O->SetStringField(TEXT("source"), P.Source);
		O->SetStringField(TEXT("item"), P.Item);
		O->SetStringField(TEXT("id"), P.AssetOrActorId);
		O->SetStringField(TEXT("socket"), P.Socket);
		return O;
	}

	FObj BodyJson(const FCafeBodySnapshot& B)
	{
		FObj O = NewObj();
		O->SetBoolField(TEXT("execution_state_known"), B.bExecutionStateKnown);
		O->SetBoolField(TEXT("moving"), B.bMoving);
		O->SetBoolField(TEXT("facing_target"), B.bFacingTarget);
		O->SetStringField(TEXT("locomotion"), B.Locomotion);
		O->SetStringField(TEXT("upper_body"), B.UpperBody);
		O->SetStringField(TEXT("left_hand"), B.LeftHand);
		O->SetStringField(TEXT("right_hand"), B.RightHand);
		FValues Props;
		for (const FCafeHeldPropSnapshot& P : B.HeldProps)
		{
			Props.Add(ObjValue(HeldPropJson(P)));
		}
		O->SetArrayField(TEXT("held_props"), Props);
		return O;
	}

	FObj EmotionJson(const FCafeWorldSnapshot& S, bool bIncludeVolatile)
	{
		FObj O = NewObj();
		O->SetStringField(TEXT("label"), S.Emotion.EmotionLabel);
		O->SetNumberField(TEXT("valence"), Round2(S.Emotion.ShortTermValence));
		O->SetNumberField(TEXT("arousal"), Round2(S.Emotion.Arousal));
		O->SetNumberField(TEXT("mood_valence"), Round2(S.Emotion.MoodValence));
		O->SetStringField(TEXT("expression_intent"), S.Emotion.ExpressionIntent);
		O->SetStringField(TEXT("last_interpretation"), S.Emotion.LastInterpretation);
		if (bIncludeVolatile)
		{
			O->SetStringField(TEXT("updated_at_utc"), S.Emotion.UpdatedAtUtc.ToIso8601());
		}
		return O;
	}

	FObj MemoryJson(const FCafeMemorySnapshot& M)
	{
		FObj O = NewObj();
		O->SetBoolField(TEXT("available"), M.bAvailable);
		O->SetNumberField(TEXT("count"), M.Count);
		O->SetStringField(TEXT("last_recalled_id"), M.LastRecalledId);
		O->SetStringField(TEXT("last_recalled_summary"), M.LastRecalledSummary);
		O->SetStringField(TEXT("last_appraised_summary"), M.LastAppraisedSummary);
		return O;
	}

	FObj TableJson(const FCafeTableSnapshot& T)
	{
		FObj O = NewObj();
		O->SetStringField(TEXT("id"), T.Id);
		O->SetStringField(TEXT("actor_id"), T.ActorId);
		O->SetBoolField(TEXT("marker_occupied"), T.bMarkerOccupied);
		O->SetBoolField(TEXT("occupied"), T.bOccupied);
		O->SetNumberField(TEXT("capacity"), T.Capacity);
		O->SetNumberField(TEXT("occupied_or_reserved_seats"), T.OccupiedOrReservedSeats);
		O->SetBoolField(TEXT("cleanup_state_known"), T.bCleanupStateKnown);
		O->SetBoolField(TEXT("needs_cleanup"), T.bNeedsCleanup);
		O->SetStringField(TEXT("cleanup_evidence"), T.CleanupEvidence);
		return O;
	}

	FObj SeatJson(const FCafeSeatSnapshot& St)
	{
		FObj O = NewObj();
		O->SetStringField(TEXT("id"), St.Id);
		O->SetStringField(TEXT("actor_id"), St.ActorId);
		O->SetStringField(TEXT("table_id"), St.TableId);
		O->SetBoolField(TEXT("can_sit"), St.bCanSit);
		O->SetBoolField(TEXT("allow_npc_sit"), St.bAllowNPCSit);
		O->SetBoolField(TEXT("occupied"), St.bOccupied);
		O->SetBoolField(TEXT("reserved"), St.bReserved);
		O->SetBoolField(TEXT("available"), St.bAvailable);
		O->SetBoolField(TEXT("available_for_npc"), St.bAvailableForNPC);
		O->SetStringField(TEXT("occupant_id"), St.OccupantId);
		O->SetBoolField(TEXT("occupant_seated"), St.bOccupantSeated);
		return O;
	}

	FObj PlantJson(const FCafePlantSnapshot& P)
	{
		FObj O = NewObj();
		O->SetStringField(TEXT("id"), P.Id);
		O->SetStringField(TEXT("actor_id"), P.ActorId);
		O->SetNumberField(TEXT("water_need"), Round2(P.WaterNeed));
		O->SetNumberField(TEXT("needs_water_threshold"), Round2(P.NeedsWaterThreshold));
		O->SetNumberField(TEXT("moisture"), Round2(P.Moisture));
		O->SetStringField(TEXT("tier"), P.Tier);
		O->SetNumberField(TEXT("urgency"), Round2(P.Urgency));
		O->SetNumberField(TEXT("enter_moisture"), Round2(P.EnterMoisture));
		O->SetNumberField(TEXT("clear_moisture"), Round2(P.ClearMoisture));
		O->SetStringField(TEXT("reserved_by"), P.ReservedBy);
		O->SetBoolField(TEXT("enabled"), P.bEnabled);
		O->SetBoolField(TEXT("needs_water"), P.bNeedsWater);
		O->SetBoolField(TEXT("cached_needs_water"), P.bCachedNeedsWater);
		O->SetBoolField(TEXT("reserved"), P.bReserved);
		O->SetBoolField(TEXT("available"), P.bAvailable);
		O->SetNumberField(TEXT("priority"), P.Priority);
		O->SetBoolField(TEXT("sync_world_tag"), P.bSyncWorldTag);
		O->SetStringField(TEXT("world_tag"), P.WorldTag.ToString());
		return O;
	}

	FObj BuildFullJson(const FCafeWorldSnapshot& S, bool bIncludeVolatile)
	{
		FObj Root = NewObj();
		Root->SetNumberField(TEXT("schema_version"), S.SchemaVersion);
		if (bIncludeVolatile)
		{
			Root->SetNumberField(TEXT("revision"), static_cast<double>(S.Revision));
			Root->SetStringField(TEXT("captured_at_utc"), S.CapturedAtUtc);
			Root->SetNumberField(TEXT("world_time_seconds"), S.WorldTimeSeconds);
		}
		Root->SetStringField(TEXT("world_id"), S.WorldId);
		Root->SetStringField(TEXT("service_manager_id"), S.ServiceManagerId);
		Root->SetBoolField(TEXT("service_available"), S.bServiceAvailable);
		Root->SetObjectField(TEXT("service"), ServiceJson(S.Service));
		Root->SetObjectField(TEXT("player"), PlayerJson(S.Player));
		Root->SetObjectField(TEXT("npc"), NpcJson(S.NPC));
		Root->SetObjectField(TEXT("execution"), ExecutionJson(S.Execution));
		Root->SetObjectField(TEXT("body"), BodyJson(S.Body));
		Root->SetBoolField(TEXT("emotion_available"), S.bEmotionAvailable);
		if (S.bEmotionAvailable)
		{
			Root->SetObjectField(TEXT("emotion"), EmotionJson(S, bIncludeVolatile));
		}
		Root->SetObjectField(TEXT("memory"), MemoryJson(S.Memory));

		FValues Tables;
		for (const FCafeTableSnapshot& T : S.Tables) { Tables.Add(ObjValue(TableJson(T))); }
		Root->SetArrayField(TEXT("tables"), Tables);
		FValues Seats;
		for (const FCafeSeatSnapshot& St : S.Seats) { Seats.Add(ObjValue(SeatJson(St))); }
		Root->SetArrayField(TEXT("seats"), Seats);
		FValues Plants;
		for (const FCafePlantSnapshot& P : S.Plants) { Plants.Add(ObjValue(PlantJson(P))); }
		Root->SetArrayField(TEXT("plants"), Plants);

		FObj Tags = NewObj();
		Tags->SetArrayField(TEXT("reported"), NameValues(S.ReportedWorldStateTags));
		Tags->SetArrayField(TEXT("active"), NameValues(S.ActiveWorldStateTags));
		Root->SetObjectField(TEXT("world_state_tags"), Tags);
		Root->SetBoolField(TEXT("any_plant_needs_water"), S.bAnyPlantNeedsWater);
		Root->SetArrayField(TEXT("diagnostics"), StringValues(S.Diagnostics));
		return Root;
	}

	// ---------- Compact (purpose-specific) builders ----------

	FObj CompactService(const FCafeServiceSnapshot& S)
	{
		FObj O = NewObj();
		O->SetStringField(TEXT("state"), S.PhaseName);
		O->SetStringField(TEXT("phase"), S.PhaseName);
		O->SetStringField(TEXT("raw_state"), S.StateName);
		if (S.bGuideInProgress) { O->SetBoolField(TEXT("guide_in_progress"), true); }
		if (S.bMealCompletionConfirmationPending) { O->SetBoolField(TEXT("meal_completion_pending"), true); }
		if (S.bDiningServiceCompleted) { O->SetBoolField(TEXT("dining_completed"), true); }
		SetNonEmpty(O, TEXT("table"), S.CurrentTableID);
		SetNonEmpty(O, TEXT("guide_target"), S.CurrentGuideTargetID);
		return O;
	}

	FObj CompactPlayer(const FCafePlayerSnapshot& P)
	{
		FObj O = NewObj();
		O->SetBoolField(TEXT("present"), P.bInServiceScene);
		O->SetBoolField(TEXT("exists"), P.bExists);
		O->SetBoolField(TEXT("seated"), P.bSeated);
		O->SetBoolField(TEXT("in_table_area"), P.bInTableArea);
		SetNonEmpty(O, TEXT("seat"), P.SeatId);
		SetNonEmpty(O, TEXT("table"), P.TableId);
		O->SetStringField(TEXT("pose"), P.Pose);
		O->SetStringField(TEXT("zone"), P.Zone);
		O->SetStringField(TEXT("activity"), P.Activity);
		if (P.DistanceToNPC >= 0.0f)
		{
			// Only what a reply or an interruption decision can use: how far, and in front of or behind her.
			O->SetNumberField(TEXT("distance"), FMath::RoundToDouble(P.DistanceToNPC));
			O->SetStringField(TEXT("position"), P.RearFactor >= 0.7f ? TEXT("behind") : P.RearFactor >= 0.3f ? TEXT("side") : TEXT("front"));
		}
		return O;
	}

	FObj CompactEmotion(const FCafeWorldSnapshot& S)
	{
		FObj O = NewObj();
		O->SetStringField(TEXT("label"), S.Emotion.EmotionLabel);
		O->SetNumberField(TEXT("valence"), Round2(S.Emotion.ShortTermValence));
		O->SetNumberField(TEXT("arousal"), Round2(S.Emotion.Arousal));
		O->SetNumberField(TEXT("mood"), Round2(S.Emotion.MoodValence));
		return O;
	}

	// Only what is physically in use; "free" resources are implied by absence.
	FObj CompactBody(const FCafeBodySnapshot& B, const FCafeActionQueueSnapshot& E)
	{
		FObj O = NewObj();
		O->SetStringField(TEXT("locomotion"), B.Locomotion);
		O->SetStringField(TEXT("upper_body"), B.UpperBody);
		if (B.LeftHand != TEXT("free")) { O->SetStringField(TEXT("left_hand"), B.LeftHand); }
		if (B.RightHand != TEXT("free")) { O->SetStringField(TEXT("right_hand"), B.RightHand); }
		if (B.bFacingTarget) { O->SetBoolField(TEXT("facing_target"), true); }
		if (E.bMovementLocked) { O->SetBoolField(TEXT("movement_locked"), true); }
		return O;
	}

	FObj CompactPlanForDialogue(const FCafeNPCSnapshot& N)
	{
		FObj O = NewObj();
		O->SetStringField(TEXT("status"), PlanStatusName(N.Plan.Status));
		SetNonEmpty(O, TEXT("goal"), N.Plan.Goal);
		SetNonEmpty(O, TEXT("current_step"), N.CurrentPlanStep);
		return O;
	}

	FObj CompactPlanForInterruption(const FCafeNPCSnapshot& N)
	{
		FObj O = PlanJson(N.Plan, false);
		O->RemoveField(TEXT("step_count"));
		SetNonEmpty(O, TEXT("current_step"), N.CurrentPlanStep);
		// Remaining work decides resume vs. replan; completed steps do not.
		FValues Remaining;
		for (int32 i = N.Plan.CurrentStepIndex + 1; i < N.Plan.Steps.Num(); ++i)
		{
			Remaining.Add(MakeShared<FJsonValueString>(N.Plan.Steps[i]));
		}
		O->SetArrayField(TEXT("remaining_after_current"), Remaining);
		return O;
	}

	FObj CompactSeat(const FCafeSeatSnapshot& St)
	{
		FObj O = NewObj();
		O->SetStringField(TEXT("id"), St.Id);
		SetNonEmpty(O, TEXT("table"), St.TableId);
		SetNonEmpty(O, TEXT("occupant"), St.OccupantId);
		O->SetBoolField(TEXT("available"), St.bAvailable);
		O->SetBoolField(TEXT("npc_can_use"), St.bAvailableForNPC);
		return O;
	}

	FObj CompactPlant(const FCafePlantSnapshot& P)
	{
		FObj O = NewObj();
		O->SetStringField(TEXT("id"), P.Id);
		O->SetNumberField(TEXT("moisture"), Round2(P.Moisture));
		O->SetStringField(TEXT("tier"), P.Tier);
		O->SetBoolField(TEXT("needs_water"), P.bNeedsWater);
		O->SetNumberField(TEXT("urgency"), Round2(P.Urgency));
		O->SetNumberField(TEXT("priority"), P.Priority);
		if (P.bReserved) { O->SetBoolField(TEXT("reserved"), true); }
		return O;
	}

	FObj CompactTable(const FCafeTableSnapshot& T)
	{
		FObj O = NewObj();
		O->SetStringField(TEXT("id"), T.Id);
		O->SetBoolField(TEXT("occupied"), T.bOccupied);
		if (T.bCleanupStateKnown) { O->SetBoolField(TEXT("needs_cleanup"), T.bNeedsCleanup); }
		return O;
	}

	// Adds the single entity the decision is about (full detail), if the focus id matches one.
	void AddFocus(const FObj& Root, const FCafeWorldSnapshot& S, const FString& FocusId)
	{
		if (FocusId.IsEmpty())
		{
			return;
		}
		for (const FCafeSeatSnapshot& St : S.Seats)
		{
			if (St.Id.Equals(FocusId, ESearchCase::IgnoreCase))
			{
				FObj F = SeatJson(St); F->SetStringField(TEXT("type"), TEXT("seat"));
				Root->SetObjectField(TEXT("focus"), F);
				return;
			}
		}
		for (const FCafeTableSnapshot& T : S.Tables)
		{
			if (T.Id.Equals(FocusId, ESearchCase::IgnoreCase))
			{
				FObj F = TableJson(T); F->SetStringField(TEXT("type"), TEXT("table"));
				Root->SetObjectField(TEXT("focus"), F);
				return;
			}
		}
		for (const FCafePlantSnapshot& P : S.Plants)
		{
			if (P.Id.Equals(FocusId, ESearchCase::IgnoreCase))
			{
				FObj F = PlantJson(P); F->SetStringField(TEXT("type"), TEXT("plant"));
				Root->SetObjectField(TEXT("focus"), F);
				return;
			}
		}
	}

	FString YesNo(bool b) { return b ? TEXT("yes") : TEXT("no"); }
}

FString FCafeWorldSnapshotJson::ToJson(const FCafeWorldSnapshot& Snapshot, bool bPretty)
{
	return Serialize(BuildFullJson(Snapshot, true), bPretty);
}

FString FCafeWorldSnapshotJson::StateDigest(const FCafeWorldSnapshot& Snapshot)
{
	const FString Json = Serialize(BuildFullJson(Snapshot, false), false);
	return FMD5::HashAnsiString(*Json);
}

FString FCafeWorldSnapshotJson::BuildContext(const FCafeWorldSnapshot& S, ECafeSnapshotPurpose Purpose,
	const FString& FocusEntityId, int32 MaxEntities)
{
	const int32 Max = FMath::Clamp(MaxEntities, 0, 20);
	FObj Root = NewObj();
	Root->SetStringField(TEXT("purpose"), PurposeName(Purpose));
	Root->SetNumberField(TEXT("revision"), static_cast<double>(S.Revision));
	Root->SetObjectField(TEXT("service"), CompactService(S.Service));
	Root->SetObjectField(TEXT("player"), CompactPlayer(S.Player));

	const FCafeNPCSnapshot& N = S.NPC;
	FObj Npc = NewObj();
	Npc->SetStringField(TEXT("activity"), N.ActivityId.IsEmpty() ? FString(TEXT("none")) : N.ActivityId);
	Npc->SetBoolField(TEXT("busy"), N.bBusy);
	Npc->SetStringField(TEXT("zone"), N.Zone);

	switch (Purpose)
	{
	case ECafeSnapshotPurpose::Planning:
	{
		Npc->SetObjectField(TEXT("plan"), CompactPlanForDialogue(N));
		Npc->SetObjectField(TEXT("motives"), MotivesJson(N));
		Root->SetObjectField(TEXT("npc"), Npc);
		if (S.bEmotionAvailable) { Root->SetObjectField(TEXT("emotion"), CompactEmotion(S)); }
		Root->SetObjectField(TEXT("body"), CompactBody(S.Body, S.Execution));
		Root->SetArrayField(TEXT("active_tags"), NameValues(S.ActiveWorldStateTags, Max));

		// Only options the planner could actually pick: free NPC seats, waterable plants, live tables.
		FValues Seats;
		for (const FCafeSeatSnapshot& St : S.Seats)
		{
			if (St.bAvailableForNPC && Seats.Num() < Max) { Seats.Add(ObjValue(CompactSeat(St))); }
		}
		Root->SetArrayField(TEXT("free_seats"), Seats);

		TArray<const FCafePlantSnapshot*> Thirsty;
		for (const FCafePlantSnapshot& P : S.Plants)
		{
			if (P.bNeedsWater && P.bAvailable) { Thirsty.Add(&P); }
		}
		// TArray<T*>::Sort dereferences the pointers before calling the predicate.
		Thirsty.Sort([](const FCafePlantSnapshot& A, const FCafePlantSnapshot& B)
		{
			return A.Priority != B.Priority ? A.Priority > B.Priority : A.Moisture < B.Moisture;
		});
		FValues Plants;
		for (const FCafePlantSnapshot* P : Thirsty)
		{
			if (Plants.Num() < Max) { Plants.Add(ObjValue(CompactPlant(*P))); }
		}
		Root->SetArrayField(TEXT("plants_needing_water"), Plants);

		FValues Tables;
		for (const FCafeTableSnapshot& T : S.Tables)
		{
			if ((T.bOccupied || T.bNeedsCleanup) && Tables.Num() < Max) { Tables.Add(ObjValue(CompactTable(T))); }
		}
		Root->SetArrayField(TEXT("active_tables"), Tables);
		break;
	}
	case ECafeSnapshotPurpose::PlayerDialogue:
	{
		SetNonEmpty(Npc, TEXT("target"), N.ActivityTarget);
		SetNonEmpty(Npc, TEXT("selected_target"), N.SelectedTargetId);
		if (N.bAwaitingEmbodiedResponse) { Npc->SetBoolField(TEXT("responding_to_player"), true); }
		if (N.Plan.Status != ENPCAutonomyPlanStatus::None) { Npc->SetObjectField(TEXT("plan"), CompactPlanForDialogue(N)); }
		Root->SetObjectField(TEXT("npc"), Npc);
		if (S.bEmotionAvailable) { Root->SetObjectField(TEXT("emotion"), CompactEmotion(S)); }

		FObj Body = CompactBody(S.Body, S.Execution);
		SetNonEmpty(Body, TEXT("service_gesture"), S.Execution.ServiceGesture);
		Root->SetObjectField(TEXT("body"), Body);
		Root->SetArrayField(TEXT("active_tags"), NameValues(S.ActiveWorldStateTags, Max));
		break;
	}
	case ECafeSnapshotPurpose::TaskInterruption:
	{
		SetNonEmpty(Npc, TEXT("target"), N.ActivityTarget);
		SetNonEmpty(Npc, TEXT("selected_target"), N.SelectedTargetId);
		SetNonEmpty(Npc, TEXT("phase"), N.RuntimePhase);
		Npc->SetBoolField(TEXT("activity_running"), N.bActivityRunning);
		if (N.bDecisionPending) { Npc->SetBoolField(TEXT("decision_pending"), true); }
		if (N.bAwaitingEmbodiedResponse) { Npc->SetBoolField(TEXT("awaiting_embodied_response"), true); }
		if (N.bResumeAfterReply) { Npc->SetBoolField(TEXT("resume_after_reply"), true); }
		Npc->SetObjectField(TEXT("plan"), CompactPlanForInterruption(N));
		Npc->SetObjectField(TEXT("motives"), MotivesJson(N));
		Root->SetObjectField(TEXT("npc"), Npc);

		const FCafeActionQueueSnapshot& E = S.Execution;
		FObj Exec = NewObj();
		Exec->SetStringField(TEXT("current_action"), E.CurrentAction);
		SetNonEmpty(Exec, TEXT("target"), E.Target);
		Exec->SetNumberField(TEXT("pending_actions"), E.PendingActionCount);
		if (E.bWaitingForMove) { Exec->SetBoolField(TEXT("waiting_for_move"), true); }
		if (E.bAnimationActive) { Exec->SetBoolField(TEXT("animation_active"), true); }
		if (E.bPausedForServiceGesture) { Exec->SetBoolField(TEXT("paused_for_service_gesture"), true); }
		SetNonEmpty(Exec, TEXT("deferred_action"), E.DeferredAction);
		Root->SetObjectField(TEXT("execution"), Exec);

		FObj Body = CompactBody(S.Body, E);
		SetNonEmpty(Body, TEXT("service_gesture"), E.ServiceGesture);
		if (S.Body.HeldProps.Num() > 0)
		{
			FValues Props;
			for (const FCafeHeldPropSnapshot& P : S.Body.HeldProps) { Props.Add(ObjValue(HeldPropJson(P))); }
			Body->SetArrayField(TEXT("held_props"), Props);
		}
		Root->SetObjectField(TEXT("body"), Body);
		if (S.bEmotionAvailable) { Root->SetObjectField(TEXT("emotion"), CompactEmotion(S)); }
		Root->SetArrayField(TEXT("active_tags"), NameValues(S.ActiveWorldStateTags, Max));

		int32 ThirstyCount = 0;
		for (const FCafePlantSnapshot& P : S.Plants) { ThirstyCount += P.bNeedsWater ? 1 : 0; }
		Root->SetNumberField(TEXT("plants_needing_water_count"), ThirstyCount);
		break;
	}
	}

	AddFocus(Root, S, FocusEntityId);
	return Serialize(Root, false);
}

FString FCafeWorldSnapshotJson::DebugSummary(const FCafeWorldSnapshot& S)
{
	FString Out;
	Out += FString::Printf(TEXT("===== CafeWorldSnapshot rev=%lld t=%.2fs world=%s ====="), S.Revision, S.WorldTimeSeconds, *S.WorldId);
	Out += FString::Printf(TEXT("\nService : phase=%s raw=%s%s table=%s guide=%s guideTarget=%s mealPending=%s diningDone=%s"),
		*S.Service.PhaseName, *S.Service.StateName, S.bServiceAvailable ? TEXT("") : TEXT(" (manager missing)"),
		*S.Service.CurrentTableID, *YesNo(S.Service.bGuideInProgress), *S.Service.CurrentGuideTargetID,
		*YesNo(S.Service.bMealCompletionConfirmationPending), *YesNo(S.Service.bDiningServiceCompleted));
	Out += FString::Printf(TEXT("\nPlayer  : exists=%s present=%s pose=%s zone=%s seated=%s tableArea=%s seat=%s table=%s activity=%s dist=%.0f angle=%.0f rear=%.2f"),
		*YesNo(S.Player.bExists), *YesNo(S.Player.bInServiceScene), *S.Player.Pose, *S.Player.Zone,
		*YesNo(S.Player.bSeated), *YesNo(S.Player.bInTableArea), *S.Player.SeatId, *S.Player.TableId, *S.Player.Activity,
		S.Player.DistanceToNPC, S.Player.AngleFromNPCForwardDeg, S.Player.RearFactor);
	Out += FString::Printf(TEXT("\nNPC     : selectedTarget=%s(age %.1fs)"), *S.NPC.SelectedTargetId, S.NPC.SelectedTargetAgeSeconds);
	Out += FString::Printf(TEXT("\nNPC     : activity=%s target=%s running=%s busy=%s zone=%s phase=%s decisionPending=%s embodiedReply=%s"),
		*S.NPC.ActivityId, *S.NPC.ActivityTarget, *YesNo(S.NPC.bActivityRunning), *YesNo(S.NPC.bBusy),
		*S.NPC.Zone, *S.NPC.RuntimePhase, *YesNo(S.NPC.bDecisionPending), *YesNo(S.NPC.bAwaitingEmbodiedResponse));
	Out += FString::Printf(TEXT("\nPlan    : %s goal='%s' step=%d/%d current='%s' commitment=%s"),
		*PlanStatusName(S.NPC.Plan.Status), *S.NPC.Plan.Goal, S.NPC.Plan.CurrentStepIndex + 1,
		S.NPC.Plan.Steps.Num(), *S.NPC.CurrentPlanStep, *S.NPC.Plan.Commitment);
	Out += FString::Printf(TEXT("\nMotives : boredom=%.1f duty=%.1f social=%.1f energy=%.1f"),
		S.NPC.Motives.Boredom, S.NPC.Motives.Duty, S.NPC.Motives.SocialAttention, S.NPC.Resources.Energy);
	if (S.bEmotionAvailable)
	{
		Out += FString::Printf(TEXT("\nEmotion : %s valence=%.2f arousal=%.2f mood=%.2f"),
			*S.Emotion.EmotionLabel, S.Emotion.ShortTermValence, S.Emotion.Arousal, S.Emotion.MoodValence);
	}
	Out += FString::Printf(TEXT("\nExec    : action=%s target=%s pending=%d moveWait=%s anim=%s%s seated=%s gesture=%s locked=%s deferred=%s"),
		*S.Execution.CurrentAction, *S.Execution.Target, S.Execution.PendingActionCount,
		*YesNo(S.Execution.bWaitingForMove), *YesNo(S.Execution.bAnimationActive),
		S.Execution.bAnimationPaused ? TEXT("(paused)") : TEXT(""), *YesNo(S.Execution.bSeated),
		*S.Execution.ServiceGesture, *YesNo(S.Execution.bMovementLocked), *S.Execution.DeferredAction);
	Out += FString::Printf(TEXT("\nBody    : loco=%s upper=%s L=%s R=%s moving=%s facing=%s"),
		*S.Body.Locomotion, *S.Body.UpperBody, *S.Body.LeftHand, *S.Body.RightHand,
		*YesNo(S.Body.bMoving), *YesNo(S.Body.bFacingTarget));

	Out += FString::Printf(TEXT("\nTables  (%d):"), S.Tables.Num());
	for (const FCafeTableSnapshot& T : S.Tables)
	{
		Out += FString::Printf(TEXT("\n  - %s occupied=%s seats=%d/%d cleanup=%s (%s)"), *T.Id, *YesNo(T.bOccupied),
			T.OccupiedOrReservedSeats, T.Capacity,
			T.bCleanupStateKnown ? *YesNo(T.bNeedsCleanup) : TEXT("unknown"), *T.CleanupEvidence);
	}
	Out += FString::Printf(TEXT("\nSeats   (%d):"), S.Seats.Num());
	for (const FCafeSeatSnapshot& St : S.Seats)
	{
		Out += FString::Printf(TEXT("\n  - %s table=%s available=%s npcOk=%s occupied=%s reserved=%s by=%s"), *St.Id, *St.TableId,
			*YesNo(St.bAvailable), *YesNo(St.bAvailableForNPC), *YesNo(St.bOccupied), *YesNo(St.bReserved), *St.OccupantId);
	}
	Out += FString::Printf(TEXT("\nPlants  (%d) anyNeedsWater=%s:"), S.Plants.Num(), *YesNo(S.bAnyPlantNeedsWater));
	for (const FCafePlantSnapshot& P : S.Plants)
	{
		Out += FString::Printf(TEXT("\n  - %s moisture=%.1f tier=%s needsWater=%s urgency=%.2f reserved=%s prio=%d"), *P.Id, P.Moisture,
			*P.Tier, *YesNo(P.bNeedsWater), P.Urgency, P.bReserved ? *P.ReservedBy : TEXT("no"), P.Priority);
	}

	TArray<FString> Tags;
	for (const FName& Tag : S.ActiveWorldStateTags) { Tags.Add(Tag.ToString()); }
	Out += FString::Printf(TEXT("\nTags    : [%s]"), *FString::Join(Tags, TEXT(", ")));
	if (S.Diagnostics.Num() > 0)
	{
		Out += FString::Printf(TEXT("\nDiag    : %s"), *FString::Join(S.Diagnostics, TEXT("; ")));
	}
	return Out;
}
