// Copyright Epic Games, Inc. All Rights Reserved.

#include "NPCAutonomyComponent.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/MorphTarget.h"
#include "CafeTargetPointActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FurnitureMarkerActor.h"
#include "EmotionClient.h"
#include "HAL/IConsoleManager.h"
#include "LLMClient.h"
#include "NPCActionQueueComponent.h"
#include "NPCFaceTargetComponent.h"
#include "NPCFoodCarryComponent.h"
#include "NPCAutonomyActionSet.h"
#include "NPCEmotionComponent.h"
#include "NPCMemoryComponent.h"
#include "NPCPersonaProfile.h"
#include "NPCResearchLoggerComponent.h"
#include "PlayerSeatComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TableMarkerActor.h"
#include "WaterablePlantActor.h"

namespace
{
	constexpr uint64 FastValidationDebugMessageKey = 170041;
	TWeakObjectPtr<UNPCAutonomyComponent> GPrimaryAutonomyComponent;

	bool ContainsTagIgnoreCase(const TArray<FString>& Tags, const FString& TargetTag)
	{
		return Tags.ContainsByPredicate([&TargetTag](const FString& Tag)
		{
			return Tag.TrimStartAndEnd().Equals(TargetTag, ESearchCase::IgnoreCase);
		});
	}

	FString SerializeResearchPayload(const TSharedRef<FJsonObject>& Object)
	{
		FString Json;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		FJsonSerializer::Serialize(Object, Writer);
		return Json;
	}

	void RunNPCSitTestCommand(const TArray<FString>& Args, UWorld*)
	{
		UNPCAutonomyComponent* Autonomy = UNPCAutonomyComponent::GetPrimaryAutonomyComponent();
		if (!Autonomy)
		{
			UE_LOG(LogTemp, Warning, TEXT("[NPCSeatTest] autonomy component not found."));
			return;
		}

		const FString SeatID = Args.IsEmpty() ? TEXT("sofa01") : Args[0];
		Autonomy->ForceSeatActivityForValidation(SeatID);
	}

	void RunNPCSitWithPlayerTestCommand(const TArray<FString>&, UWorld*)
	{
		if (UNPCAutonomyComponent* Autonomy = UNPCAutonomyComponent::GetPrimaryAutonomyComponent())
		{
			Autonomy->ForceSitOppositePlayerForValidation();
		}
	}

	void RunNPCOfferSitWithPlayerTestCommand(const TArray<FString>&, UWorld*)
	{
		if (UNPCAutonomyComponent* Autonomy = UNPCAutonomyComponent::GetPrimaryAutonomyComponent())
		{
			Autonomy->ForceSeatInvitationForValidation();
		}
	}

	void RunNPCAcceptSeatInvitationTestCommand(const TArray<FString>&, UWorld*)
	{
		if (UNPCAutonomyComponent* Autonomy = UNPCAutonomyComponent::GetPrimaryAutonomyComponent())
		{
			Autonomy->ResolveSeatInvitation(true);
		}
	}

	void RunNPCDeclineSeatInvitationTestCommand(const TArray<FString>&, UWorld*)
	{
		if (UNPCAutonomyComponent* Autonomy = UNPCAutonomyComponent::GetPrimaryAutonomyComponent())
		{
			Autonomy->ResolveSeatInvitation(false);
		}
	}

	void RunNPCStandTestCommand(const TArray<FString>&, UWorld*)
	{
		if (UNPCAutonomyComponent* Autonomy = UNPCAutonomyComponent::GetPrimaryAutonomyComponent())
		{
			Autonomy->InterruptAutonomy(TEXT("manual_seat_exit"));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GForceNPCSitCommand(
		TEXT("npc.Sit"),
		TEXT("Force the NPC to sit at a furniture marker. Usage: npc.Sit sofa01"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunNPCSitTestCommand));
	FAutoConsoleCommandWithWorldAndArgs GForceNPCSitWithPlayerCommand(
		TEXT("npc.SitWithPlayer"),
		TEXT("Force the NPC to sit in an available seat across from the seated player."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunNPCSitWithPlayerTestCommand));
	FAutoConsoleCommandWithWorldAndArgs GForceNPCOfferSitWithPlayerCommand(
		TEXT("npc.OfferSitWithPlayer"),
		TEXT("Force the NPC to approach the player's table and ask permission before sitting."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunNPCOfferSitWithPlayerTestCommand));
	FAutoConsoleCommandWithWorldAndArgs GForceNPCAcceptSeatInvitationCommand(
		TEXT("npc.AcceptSeatInvitation"),
		TEXT("Accept a pending NPC seat invitation for validation."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunNPCAcceptSeatInvitationTestCommand));
	FAutoConsoleCommandWithWorldAndArgs GForceNPCDeclineSeatInvitationCommand(
		TEXT("npc.DeclineSeatInvitation"),
		TEXT("Decline a pending NPC seat invitation for validation."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunNPCDeclineSeatInvitationTestCommand));
	FAutoConsoleCommandWithWorldAndArgs GForceNPCStandCommand(
		TEXT("npc.Stand"),
		TEXT("Interrupt the current seat activity and make the NPC leave the seat."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunNPCStandTestCommand));
}

UNPCAutonomyComponent::UNPCAutonomyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	bAutonomyEnabled = true;
	DecisionMode = ENPCAutonomyDecisionMode::EventTriggeredShortPlan;
	bFastValidationMode = false;
	FastValidationMotiveTimeScale = 10.0f;
	bShowFastValidationDebug = true;
	bForceFirstPhoneActivityForValidation = true;
	bEnableMultiStepPlanValidationScenario = false;
	MultiStepPlanValidationWorldTag = FName(TEXT("dining_area_needs_tidy"));
	bAutoInterruptMultiStepValidationPlan = true;
	MultiStepValidationInterruptDelaySeconds = 1.5f;
	EvaluationIntervalSeconds = 2.0f;
	InitialIdleDelaySeconds = 8.0f;
	DelayAfterActivitySeconds = 5.0f;
	PostPlanFreeActivitySeconds = 25.0f;
	DelayAfterPlayerInteractionSeconds = 12.0f;
	DecisionErrorRetrySeconds = 15.0f;
	MinimumSecondsBetweenLLMDecisions = 20.0f;
	DelayBetweenPlanStepsSeconds = 1.0f;
	WaitDecisionCooldownSeconds = 45.0f;
	BoredomIncreasePerIdleSecond = 0.24f;
	BoredomDecisionThresholdMin = 42.0f;
	BoredomDecisionThresholdMax = 58.0f;
	DutyDecisionThreshold = 50.0f;
	DutyIncreasePerSecondWithTask = 2.0f;
	DutyDecayPerSecond = 4.0f;
	ServiceTransitionEnergyCost = 1.5f;
	IdleEnergyRecoveryPerSecond = 0.01f;
	MaxPlanSteps = 5;
	bResumeTaskPlansAfterService = true;
	bEnableAmbientBehaviorLayer = true;
	AmbientOpportunitiesPerMinute = 3.0f;
	MinimumSecondsBetweenAmbientActivities = 10.0f;
	MaximumQuietIdleSecondsBeforeAmbient = 40.0f;
	SofaRestEnergyThreshold = 35.0f;
	SeatedIdleLoopCount = 3;
	SocialSeatedIdleLoopCount = 8;
	bRequireServedStateForNaturalSocialSit = true;
	SeatInvitationTimeoutSeconds = 25.0f;
	SeatInvitationDeclineCooldownSeconds = 120.0f;
	SeatInvitationText = TEXT("少しゆっくりお話ししたいのですが、向かいの席に座ってもいいですか？");
	SitDownAnimation = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Characters/OriginalCafeMaid/Animations/Anim_KA_Sit_Start_OCM.Anim_KA_Sit_Start_OCM")));
	SeatedIdleAnimation = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Characters/OriginalCafeMaid/Animations/Anim_KA_Sit_LookingAround_OCM.Anim_KA_Sit_LookingAround_OCM")));
	SeatedConversationAnimation = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Characters/OriginalCafeMaid/Animations/Anim_Female_Sit_Talk_OCM.Anim_Female_Sit_Talk_OCM")));
	StandUpAnimation = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Characters/OriginalCafeMaid/Animations/Anim_KA_Sit_End_OCM.Anim_KA_Sit_End_OCM")));
	SeatedConversationOpeningDelaySeconds = 2.5f;
	AutonomyModelName = TEXT("gpt-4o-mini");
	bEnableMemoryEmotionCycle = true;
	bEnableSpontaneousRecall = true;
	BaseSpontaneousRecallOpportunitiesPerMinute = 8.0f;
	MinimumSecondsBetweenRecallAppraisals = 30.0f;
	PostPlanRecallBoostDurationSeconds = 45.0f;
	PostPlanRecallOpportunityMultiplier = 2.5f;
	bSeedPositiveMemoryForValidation = true;
	bForceFirstValidationRecall = true;
	bWaitForPlayerEncounterBeforeValidation = true;
	bPlayVisibleMemoryReaction = true;
	bEnableConcurrentExpressionOverlay = true;
	bAllowBlockingStrongMemoryReactions = true;
	PositiveMemoryReactionAnimation = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Characters/OriginalCafeMaid/Animations/Anim_KA_Idle08_ComeUpWithAnIdea_OCM.Anim_KA_Idle08_ComeUpWithAnIdea_OCM")));
	NegativeMemoryReactionAnimation = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Characters/OriginalCafeMaid/Animations/Anim_Angry_OCM.Anim_Angry_OCM")));
	HighArousalNegativeMemoryReactionAnimation = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Characters/OriginalCafeMaid/Animations/Anim_Standing_Arguing_OCM.Anim_Standing_Arguing_OCM")));
	HighArousalNegativeReactionThreshold = 0.6f;
	NeutralMemoryReactionAnimation = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Characters/OriginalCafeMaid/Animations/Anim_Searching_Files_High_OCM.Anim_Searching_Files_High_OCM")));
	ExpressionMorphStrength = 1.0f;
	ExpressionMorphDurationSeconds = 6.0f;
	ConcurrentExpressionLeadDurationSeconds = 30.0f;
	ExpressionFadeInDurationSeconds = 0.3f;
	ExpressionFadeOutDurationSeconds = 0.5f;
	bShowPlanSurfaceThoughts = true;
	PlanThoughtDelayAfterMemorySeconds = 4.5f;
	bEnableAutomaticBlinking = true;
	BlinkIntervalMinSeconds = 2.5f;
	BlinkIntervalMaxSeconds = 5.5f;
	BlinkClosedDurationSeconds = 0.14f;
	BlinkStrength = 1.0f;
	DoubleBlinkChance = 0.15f;
	bEnableEmbodiedResponseCoordination = true;
	bPreservePhoneActivityUntilLLMResponse = true;
	bPreserveAutonomyActivityUntilLLMResponse = true;
	EmbodiedResponseResumeFallbackSeconds = 10.0f;
	InteractiveFacingFallbackSeconds = 10.0f;
	bEnableInternalEmbodiedGestureAnimations = true;
	StartledReactionAnimation = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Characters/OriginalCafeMaid/Animations/Anim_Surprised_OCM.Anim_Surprised_OCM")));
	ShushGestureAnimation = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Characters/OriginalCafeMaid/Animations/Anim_Telling_A_Secret_OCM.Anim_Telling_A_Secret_OCM")));
	bEnablePlayerSpeechStartle = true;
	PlayerSpeechStartleBaseChance = 0.05f;
	PlayerSpeechStartleFocusWeight = 0.5f;
	PlayerSpeechStartleUnexpectednessWeight = 0.2f;
	PlayerSpeechStartleRearWeight = 0.25f;
	PlayerSpeechStartleExpectedConversationPenalty = 0.4f;
	PlayerSpeechStartleMaximumChance = 0.9f;
	PlayerSpeechStartleCooldownSeconds = 12.0f;
	PlayerSpeechStartleMaximumDistance = 650.0f;
	PlayerSpeechStartleFullUnexpectednessSeconds = 15.0f;
	PlayerSpeechStartleExpressionSeconds = 2.5f;
	bForceNextPlayerSpeechStartle = false;
	CharacterDescription = LLMNPCPersona::CompactDescriptionEnglish();
	ActionSet = TSoftObjectPtr<UNPCAutonomyActionSet>(
		FSoftObjectPath(TEXT("/Game/Blueprints/Cafe/DA_NPCAutonomyActionSet.DA_NPCAutonomyActionSet")));
	CurrentActivityID = TEXT("");
	LastDecisionReason = TEXT("");
	LastDecisionContextJson = TEXT("");
	LastDecisionTrigger = TEXT("");
	CurrentBoredomDecisionThreshold = 65.0f;
	TotalDecisionRequests = 0;
	TotalContextCharactersSent = 0;
	TotalPlansCreated = 0;
	TotalPlanStepsStarted = 0;
	TotalPlanInterruptions = 0;
	TotalPlansResumed = 0;
	TotalAmbientActivitiesStarted = 0;
	bExpressionOverlayActive = false;
	bExpressionOverlayHeldByPlan = false;
	bAwaitingEmbodiedResponse = false;
	bResumePausedActivityAfterReply = false;
	bInteractiveFacingPlayer = false;
	bStartlePreparedForCurrentInteraction = false;
	bPendingPlayerSpeechStartle = false;
	bSeatInvitationApproachInProgress = false;
	bSeatInvitationPending = false;
	bSeatInvitationAcceptedForStart = false;
	SeatedConversationOpeningAttempts = 0;
	PendingInteractiveActivityID = TEXT("");
	PendingSeatInvitationSeatID = TEXT("");
	bBlinkFollowUpPending = false;
	TotalAutonomyPromptTokens = 0;
	TotalAutonomyCompletionTokens = 0;
	TotalAutonomyCachedPromptTokens = 0;
	TotalAppraisalRequests = 0;
	TotalAppraisalPromptTokens = 0;
	TotalAppraisalCompletionTokens = 0;
	ServiceManager = nullptr;
	MaidNPCActor = nullptr;
	ActionQueueComponent = nullptr;
	AutonomyLLMClient = nullptr;
	ActiveActivityAnimation = nullptr;
	LoadedActionSet = nullptr;
	MemoryComponent = nullptr;
	EmotionComponent = nullptr;
	ResearchLoggerComponent = nullptr;
	bDecisionPending = false;
	bAppraisalPending = false;
	bActivityRunning = false;
	bInterruptingActivity = false;
	bMeaningfulContextChangePending = false;
	bMultiStepValidationInterruptConsumed = false;
	bValidationRecallConsumed = false;
	bPhoneValidationActivityConsumed = false;
	bHasEncounteredPlayer = false;
	bCurrentActivityIsAmbient = false;
	PendingDecisionRequestId = 0;
	PendingAppraisalRequestId = 0;
	NextDecisionEarliestWorldTime = 0.0;
	LastStateUpdateWorldTime = 0.0;
	LastDecisionRequestWorldTime = -1000000.0;
	LastRecallAppraisalWorldTime = -1000000.0;
	PostPlanRecallBoostUntilWorldTime = 0.0;
	NextIdleStatusLogWorldTime = 0.0;
	LastActivityEndWorldTime = 0.0;
	PlanningSuppressedUntilWorldTime = 0.0;
	LastPlayerSpeechWorldTime = -1000000.0;
	LastPlayerSpeechStartleWorldTime = -1000000.0;
	ExpressionBlendStartWorldTime = 0.0;
	ExpressionBlendDurationSeconds = 0.0f;
	PendingStartleFocus = 0.0f;
	PendingStartleUnexpectedness = 0.0f;
	PendingStartleRearFactor = 0.0f;
	PendingStartleExpectedConversation = 0.0f;
	PendingStartleProbability = 0.0f;
	PendingStartleRoll = 1.0f;
	PendingStartleIntensity = 0.0f;
	PendingStartlePlayerDistance = 0.0f;
	LastKnownNPCZone = TEXT("counter");
	PendingPlanSurfaceThought = TEXT("");
	PendingDecisionTrigger = TEXT("");
	PendingPlanningTriggerAfterAppraisal = TEXT("");
	LastAppraisedMemorySummary = TEXT("");
	LastAppraisalInterpretation = TEXT("");
	LastSpontaneousRecallOpportunityProbability = 0.0f;
	CurrentEmotionLabel = TEXT("neutral");
	CurrentExpressionIntent = TEXT("neutral");
	RuntimePhase = ENPCAutonomyRuntimePhase::None;

	WorldStateTags.Add(FName(TEXT("flower_needs_water")), false);
	WorldStateTags.Add(MultiStepPlanValidationWorldTag, false);
}

void UNPCAutonomyComponent::BeginPlay()
{
	Super::BeginPlay();
	GPrimaryAutonomyComponent = this;

	ServiceManager = Cast<ACafeServiceManagerActor>(GetOwner());
	ResolveReferences();
	InitializeAutomaticBlinking();
	SeedPositiveValidationMemory();
	LoadedActionSet = ActionSet.LoadSynchronous();
	if (!LoadedActionSet)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[Autonomy] action set could not be loaded. Falling back to component activities. Asset=%s"),
			*ActionSet.ToSoftObjectPath().ToString());
	}
	BuildRuntimeActivityLibrary();
	if (bEnableMultiStepPlanValidationScenario && !MultiStepPlanValidationWorldTag.IsNone())
	{
		WorldStateTags.Add(MultiStepPlanValidationWorldTag, true);
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[AutonomyPlanTest] scenario activated. WorldTag=%s"),
			*MultiStepPlanValidationWorldTag.ToString());
	}

	AutonomyLLMClient = NewObject<ULLMClient>(this);
	if (AutonomyLLMClient)
	{
		AutonomyLLMClient->OpenAIModelName = AutonomyModelName;
		AutonomyLLMClient->OnAutonomyDecisionReceived.AddDynamic(
			this,
			&UNPCAutonomyComponent::HandleAutonomyDecisionReceived);
		AutonomyLLMClient->OnAutonomyDecisionError.AddDynamic(
			this,
			&UNPCAutonomyComponent::HandleAutonomyDecisionError);
		AutonomyLLMClient->OnMemoryAppraisalReceived.AddDynamic(
			this,
			&UNPCAutonomyComponent::HandleMemoryAppraisalReceived);
		AutonomyLLMClient->OnMemoryAppraisalError.AddDynamic(
			this,
			&UNPCAutonomyComponent::HandleMemoryAppraisalError);
	}

	if (ServiceManager)
	{
		ServiceManager->OnCafeServiceStateChanged.RemoveDynamic(
			this,
			&UNPCAutonomyComponent::HandleServiceStateChanged);
		ServiceManager->OnCafeServiceStateChanged.AddDynamic(
			this,
			&UNPCAutonomyComponent::HandleServiceStateChanged);
	}

	ULLMClient::OnAnyInteractiveRequestStarted.AddUObject(
		this,
		&UNPCAutonomyComponent::HandleInteractiveLLMRequestStarted);
	ULLMClient::OnInteractionMemoryExtracted.AddUObject(
		this,
		&UNPCAutonomyComponent::HandleInteractionMemoryExtracted);
	ULLMClient::OnNPCSocialExpressionRequested.AddUObject(
		this,
		&UNPCAutonomyComponent::HandleSocialExpressionRequested);
	ULLMClient::OnNPCEmbodiedResponseRequested.AddUObject(
		this,
		&UNPCAutonomyComponent::HandleEmbodiedResponseRequested);
	ULLMClient::OnInteractiveReplyFinished.AddUObject(
		this,
		&UNPCAutonomyComponent::HandleInteractiveReplyFinished);

	RollNextBoredomThreshold();
	if (const UWorld* World = GetWorld())
	{
		LastStateUpdateWorldTime = World->GetTimeSeconds();
		LastActivityEndWorldTime = LastStateUpdateWorldTime;
	}
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[Autonomy] initialized. Mode=%s Boredom=%.1f Threshold=%.1f Energy=%.1f FastValidation=%s MotiveTimeScale=%.1fx"),
		DecisionMode == ENPCAutonomyDecisionMode::EventTriggeredShortPlan
			? TEXT("event_triggered_agenda")
			: TEXT("fixed_interval_single_action"),
		MotiveState.Boredom,
		CurrentBoredomDecisionThreshold,
		ResourceState.Energy,
		bFastValidationMode ? TEXT("true") : TEXT("false"),
		bFastValidationMode ? FMath::Max(FastValidationMotiveTimeScale, 1.0f) : 1.0f);
	ScheduleNextDecision(InitialIdleDelaySeconds);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			EvaluationTimerHandle,
			this,
			&UNPCAutonomyComponent::EvaluateAutonomyNow,
			FMath::Max(EvaluationIntervalSeconds, 0.1f),
			true);
	}
}

void UNPCAutonomyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bAutonomyEnabled = false;
	InterruptAutonomy(TEXT("end_play"));
	if (GPrimaryAutonomyComponent.Get() == this)
	{
		GPrimaryAutonomyComponent.Reset();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EvaluationTimerHandle);
		World->GetTimerManager().ClearTimer(BlueprintActivityTimeoutTimerHandle);
		World->GetTimerManager().ClearTimer(MultiStepValidationInterruptTimerHandle);
		World->GetTimerManager().ClearTimer(ExpressionMorphResetTimerHandle);
		World->GetTimerManager().ClearTimer(ExpressionMorphBlendTimerHandle);
		World->GetTimerManager().ClearTimer(PlanThoughtDisplayTimerHandle);
		World->GetTimerManager().ClearTimer(BlinkStartTimerHandle);
		World->GetTimerManager().ClearTimer(BlinkFinishTimerHandle);
		World->GetTimerManager().ClearTimer(EmbodiedResponseResumeTimerHandle);
		World->GetTimerManager().ClearTimer(InteractiveFacingFallbackTimerHandle);
		World->GetTimerManager().ClearTimer(SeatInvitationTimeoutTimerHandle);
		World->GetTimerManager().ClearTimer(SeatedConversationOpeningTimerHandle);
	}
	StopInteractiveFacingPlayer();

	ULLMClient::OnAnyInteractiveRequestStarted.RemoveAll(this);
	ULLMClient::OnInteractionMemoryExtracted.RemoveAll(this);
	ULLMClient::OnNPCSocialExpressionRequested.RemoveAll(this);
	ULLMClient::OnNPCEmbodiedResponseRequested.RemoveAll(this);
	ULLMClient::OnInteractiveReplyFinished.RemoveAll(this);
	if (ServiceManager)
	{
		ServiceManager->OnCafeServiceStateChanged.RemoveDynamic(
			this,
			&UNPCAutonomyComponent::HandleServiceStateChanged);
	}
	BindActionQueue(nullptr);
	ClearExpressionMorphsImmediately();
	SetBlinkMorphWeight(0.0f);

	if (AutonomyLLMClient)
	{
		AutonomyLLMClient->OnAutonomyDecisionReceived.RemoveDynamic(
			this,
			&UNPCAutonomyComponent::HandleAutonomyDecisionReceived);
		AutonomyLLMClient->OnAutonomyDecisionError.RemoveDynamic(
			this,
			&UNPCAutonomyComponent::HandleAutonomyDecisionError);
		AutonomyLLMClient->OnMemoryAppraisalReceived.RemoveDynamic(
			this,
			&UNPCAutonomyComponent::HandleMemoryAppraisalReceived);
		AutonomyLLMClient->OnMemoryAppraisalError.RemoveDynamic(
			this,
			&UNPCAutonomyComponent::HandleMemoryAppraisalError);
	}
	if (GEngine)
	{
		GEngine->RemoveOnScreenDebugMessage(FastValidationDebugMessageKey);
	}

	Super::EndPlay(EndPlayReason);
}

void UNPCAutonomyComponent::SetAutonomyEnabled(bool bEnabled)
{
	bAutonomyEnabled = bEnabled;
	if (!bAutonomyEnabled)
	{
		InterruptAutonomy(TEXT("autonomy_disabled"));
		return;
	}

	ScheduleNextDecision(InitialIdleDelaySeconds);
}

void UNPCAutonomyComponent::EvaluateAutonomyNow()
{
	ResolveReferences();
	if (!bHasEncounteredPlayer && ServiceManager && ServiceManager->CurrentPlayerActor)
	{
		bHasEncounteredPlayer = true;
		UE_LOG(LogTemp, Log, TEXT("[AutonomyPlanTest] player encounter observed; visible validation is now enabled."));
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();
	const float DeltaSeconds = LastStateUpdateWorldTime > 0.0
		? static_cast<float>(FMath::Clamp(CurrentTime - LastStateUpdateWorldTime, 0.0, 5.0))
		: 0.0f;
	LastStateUpdateWorldTime = CurrentTime;
	UpdateInternalState(DeltaSeconds);
	if (EmotionComponent)
	{
		EmotionComponent->RefreshStateFromRealTime();
		const FNPCEmotionState EmotionState = EmotionComponent->GetCurrentState();
		CurrentEmotionLabel = EmotionState.EmotionLabel;
		CurrentExpressionIntent = EmotionState.ExpressionIntent;
	}
	UpdateFastValidationDebugMessage();

	const bool bSocialSeatFlowActive = bSeatInvitationApproachInProgress
		|| bSeatInvitationPending
		|| (bActivityRunning
			&& CurrentActivityID.Equals(TEXT("sit_with_player"), ESearchCase::IgnoreCase));
	if (bSocialSeatFlowActive && FindAvailableSeatOppositePlayerID().IsEmpty())
	{
		if (bActivityRunning)
		{
			InterruptAutonomy(TEXT("player_left_social_seat"));
		}
		else
		{
			CancelSeatInvitation(TEXT("player_left_before_consent"), true, true);
		}
		return;
	}

	if (CurrentTime < NextDecisionEarliestWorldTime || !CanRunAutonomy())
	{
		return;
	}

	if (ActivePlan.Status == ENPCAutonomyPlanStatus::Active
		|| ActivePlan.Status == ENPCAutonomyPlanStatus::Suspended)
	{
		const bool bWasSuspended = ActivePlan.Status == ENPCAutonomyPlanStatus::Suspended;
		if (bWasSuspended
			&& ServiceManager
			&& ServiceManager->CurrentState == ECafeServiceState::WaitingForOrder)
		{
			return;
		}
		if (TryStartCurrentPlanStep(bWasSuspended))
		{
			return;
		}

		ClearActivePlan(TEXT("plan_step_no_longer_available"));
		MarkMeaningfulContextChange(TEXT("plan_invalid"));
	}

	FString Trigger;
	if (!ShouldRequestNewDecision(Trigger))
	{
		if (TryBeginSpontaneousRecall(DeltaSeconds))
		{
			return;
		}
		if (TryStartAmbientBehavior(DeltaSeconds))
		{
			return;
		}

		if (CurrentTime >= NextIdleStatusLogWorldTime)
		{
			const float MotiveTimeScale = bFastValidationMode
				? FMath::Max(FastValidationMotiveTimeScale, 1.0f)
				: 1.0f;
			const float BoredomRate = BoredomIncreasePerIdleSecond * MotiveTimeScale;
			const float EstimatedSecondsToDecision = BoredomRate > KINDA_SMALL_NUMBER
				? FMath::Max((CurrentBoredomDecisionThreshold - MotiveState.Boredom) / BoredomRate, 0.0f)
				: -1.0f;
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[AutonomyIdle] monitoring. Boredom=%.1f/%.1f EstimatedPlanGate=%.0fs RecallStepProbability=%.3f PostPlanBoost=%s"),
				MotiveState.Boredom,
				CurrentBoredomDecisionThreshold,
				EstimatedSecondsToDecision,
				LastSpontaneousRecallOpportunityProbability,
				CurrentTime < PostPlanRecallBoostUntilWorldTime ? TEXT("true") : TEXT("false"));
			NextIdleStatusLogWorldTime = CurrentTime + 30.0;
		}
		return;
	}
	if (!Trigger.StartsWith(TEXT("memory_appraised"), ESearchCase::IgnoreCase)
		&& !Trigger.StartsWith(TEXT("appraisal_failed"), ESearchCase::IgnoreCase)
		&& TryBeginMemoryAppraisal(Trigger))
	{
		return;
	}

	RequestNewDecision(Trigger);
}

void UNPCAutonomyComponent::ForceAutonomyDecisionNow(const FString& Trigger)
{
	MarkMeaningfulContextChange(Trigger.TrimStartAndEnd().IsEmpty() ? TEXT("manual_debug") : Trigger);
	ScheduleNextDecision(0.0f);
	EvaluateAutonomyNow();
}

void UNPCAutonomyComponent::ActivateMultiStepPlanValidationScenario()
{
	if (MultiStepPlanValidationWorldTag.IsNone())
	{
		return;
	}

	WorldStateTags.Add(MultiStepPlanValidationWorldTag, true);
	bMultiStepValidationInterruptConsumed = false;
	MotiveState.Duty = FMath::Max(MotiveState.Duty, DutyDecisionThreshold);
	MarkMeaningfulContextChange(TEXT("multi_step_validation_task_activated"));
	ScheduleNextDecision(0.0f);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[AutonomyPlanTest] scenario reactivated. WorldTag=%s"),
		*MultiStepPlanValidationWorldTag.ToString());
}

bool UNPCAutonomyComponent::ForceSeatActivityForValidation(const FString& FurnitureID)
{
	ResolveReferences();
	const FString RequestedSeatID = FurnitureID.TrimStartAndEnd().IsEmpty()
		? TEXT("sofa01")
		: FurnitureID.TrimStartAndEnd();
	const FNPCAutonomyActivityDefinition* Activity = FindActivityDefinition(TEXT("rest_on_sofa"));
	if (!Activity || !ServiceManager || !IsServiceStateAutonomyAllowed(ServiceManager->CurrentState))
	{
		UE_LOG(LogTemp, Warning, TEXT("[NPCSeatTest] unavailable in current service state."));
		return false;
	}

	if (bActivityRunning)
	{
		InterruptAutonomy(TEXT("seat_validation_override"));
	}
	if (!CanRunAutonomy())
	{
		UE_LOG(LogTemp, Warning, TEXT("[NPCSeatTest] NPC is busy; seat test was not started."));
		return false;
	}

	ForcedSeatID = RequestedSeatID;
	const bool bStarted = StartActivity(*Activity, TEXT("manual_seat_validation"), true);
	ForcedSeatID.Reset();
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[NPCSeatTest] request. Seat=%s Started=%s"),
		*RequestedSeatID,
		bStarted ? TEXT("true") : TEXT("false"));
	return bStarted;
}

bool UNPCAutonomyComponent::ForceSitOppositePlayerForValidation()
{
	return RequestSitOppositePlayer();
}

bool UNPCAutonomyComponent::RequestSitOppositePlayer()
{
	ResolveReferences();
	if (bSeatInvitationPending)
	{
		return ResolveSeatInvitation(true);
	}
	if (bSeatInvitationApproachInProgress)
	{
		CancelSeatInvitation(TEXT("player_direct_sit_request"), false, false);
	}
	if (bActivityRunning
		&& CurrentActivityID.Equals(TEXT("sit_with_player"), ESearchCase::IgnoreCase))
	{
		return true;
	}

	const FNPCAutonomyActivityDefinition* Activity = FindActivityDefinition(TEXT("sit_with_player"));
	if (!Activity || !ServiceManager || !IsServiceStateAutonomyAllowed(ServiceManager->CurrentState))
	{
		UE_LOG(LogTemp, Warning, TEXT("[NPCSeat] explicit sit request unavailable in the current service state."));
		return false;
	}
	if (bActivityRunning)
	{
		InterruptAutonomy(TEXT("player_requested_social_seat"));
	}
	const FString SeatID = FindAvailableSeatOppositePlayerID();
	if (SeatID.IsEmpty() || !CanRunAutonomy())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[NPCSeat] explicit sit request failed. Seat=%s CanRun=%s"),
			SeatID.IsEmpty() ? TEXT("none") : *SeatID,
			CanRunAutonomy() ? TEXT("true") : TEXT("false"));
		return false;
	}

	ForcedSeatID = SeatID;
	bSeatInvitationAcceptedForStart = true;
	const bool bStarted = StartActivity(*Activity, TEXT("player_requested_social_seat"), false);
	bSeatInvitationAcceptedForStart = false;
	ForcedSeatID.Reset();
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[NPCSeat] explicit sit-opposite request. Seat=%s Started=%s"),
		*SeatID,
		bStarted ? TEXT("true") : TEXT("false"));
	return bStarted;
}

bool UNPCAutonomyComponent::ForceSeatInvitationForValidation()
{
	ResolveReferences();
	const FNPCAutonomyActivityDefinition* Activity = FindActivityDefinition(TEXT("sit_with_player"));
	const FString SeatID = FindAvailableSeatOppositePlayerID();
	if (!Activity
		|| SeatID.IsEmpty()
		|| !ServiceManager
		|| !IsServiceStateAutonomyAllowed(ServiceManager->CurrentState))
	{
		UE_LOG(LogTemp, Warning, TEXT("[NPCSeatInvitation] validation request unavailable."));
		return false;
	}

	if (bActivityRunning || bSeatInvitationApproachInProgress || bSeatInvitationPending)
	{
		InterruptAutonomy(TEXT("seat_invitation_validation_override"));
	}
	if (!CanRunAutonomy())
	{
		return false;
	}

	ForcedSeatID = SeatID;
	const bool bStarted = StartActivity(*Activity, TEXT("manual_seat_invitation_validation"), false);
	ForcedSeatID.Reset();
	return bStarted;
}

bool UNPCAutonomyComponent::ResolveSeatInvitation(bool bAccepted)
{
	if (!bSeatInvitationPending)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NPCSeatInvitation] no pending invitation to resolve."));
		return false;
	}

	const FString SeatID = PendingSeatInvitationSeatID;
	if (!bAccepted)
	{
		CancelSeatInvitation(TEXT("player_declined"), true, true);
		return true;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SeatInvitationTimeoutTimerHandle);
	}
	bSeatInvitationPending = false;

	const FNPCAutonomyActivityDefinition* Activity = FindActivityDefinition(TEXT("sit_with_player"));
	if (!Activity || SeatID.IsEmpty() || FindAvailableSeatOppositePlayerID().IsEmpty())
	{
		CancelSeatInvitation(TEXT("accepted_but_seat_unavailable"), true, true);
		return false;
	}

	ForcedSeatID = SeatID;
	bSeatInvitationAcceptedForStart = true;
	const bool bStarted = StartActivity(*Activity, TEXT("player_accepted_seat_invitation"), false);
	bSeatInvitationAcceptedForStart = false;
	ForcedSeatID.Reset();
	if (!bStarted)
	{
		CancelSeatInvitation(TEXT("accepted_but_start_failed"), true, true);
		return false;
	}
	PendingSeatInvitationSeatID.Reset();

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("seat_id"), SeatID);
	RecordResearchEvent(TEXT("seat_invitation_accepted"), Payload);
	UE_LOG(LogTemp, Log, TEXT("[NPCSeatInvitation] accepted. Seat=%s"), *SeatID);
	return true;
}

bool UNPCAutonomyComponent::IsSeatInvitationPending() const
{
	return bSeatInvitationPending;
}

void UNPCAutonomyComponent::NotifyNPCEnteredSeat(const FString& SeatID)
{
	if (!bActivityRunning
		|| !CurrentActivityID.Equals(TEXT("sit_with_player"), ESearchCase::IgnoreCase))
	{
		return;
	}

	SeatedConversationOpeningAttempts = 0;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SeatedConversationOpeningTimerHandle);
		World->GetTimerManager().SetTimer(
			SeatedConversationOpeningTimerHandle,
			this,
			&UNPCAutonomyComponent::BeginSeatedConversationOpening,
			FMath::Max(SeatedConversationOpeningDelaySeconds, 0.1f),
			false);
	}

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("seat_id"), SeatID);
	Payload->SetNumberField(TEXT("opening_delay_seconds"), SeatedConversationOpeningDelaySeconds);
	RecordResearchEvent(TEXT("social_seat_entered"), Payload);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[NPCSeat] seated conversation opening scheduled. Seat=%s Delay=%.1fs"),
		*SeatID,
		SeatedConversationOpeningDelaySeconds);
}

void UNPCAutonomyComponent::InterruptAutonomy(const FString& Reason)
{
	if (bSeatInvitationApproachInProgress || bSeatInvitationPending)
	{
		CancelSeatInvitation(Reason, false, false);
	}
	StopInteractiveFacingPlayer();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MultiStepValidationInterruptTimerHandle);
		World->GetTimerManager().ClearTimer(PlanThoughtDisplayTimerHandle);
		World->GetTimerManager().ClearTimer(EmbodiedResponseResumeTimerHandle);
		World->GetTimerManager().ClearTimer(SeatInvitationTimeoutTimerHandle);
		World->GetTimerManager().ClearTimer(SeatedConversationOpeningTimerHandle);
	}
	bAwaitingEmbodiedResponse = false;
	bResumePausedActivityAfterReply = false;
	PendingInteractiveActivityID = TEXT("");
	PendingPlanSurfaceThought = TEXT("");
	bDecisionPending = false;
	PendingDecisionRequestId = 0;
	bAppraisalPending = false;
	PendingAppraisalRequestId = 0;
	PendingPlanningTriggerAfterAppraisal = TEXT("");
	ResetExpressionMorphs();
	LastRequestedAvailableActions.Reset();
	SuspendOrDiscardPlan(Reason);

	if (bActivityRunning)
	{
		const FString InterruptedActivityID = CurrentActivityID;
		const bool bInterruptedAmbientActivity = bCurrentActivityIsAmbient;
		const FNPCAutonomyActivityDefinition* InterruptedActivity = FindActivityDefinition(InterruptedActivityID);
		ActivityCooldownUntil.Remove(InterruptedActivityID);
		bInterruptingActivity = true;
		bActivityRunning = false;
		bCurrentActivityIsAmbient = false;
		CurrentActivityID = TEXT("");
		ActiveActivityAnimation = nullptr;
		RuntimePhase = ENPCAutonomyRuntimePhase::None;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(BlueprintActivityTimeoutTimerHandle);
		}
		if (InterruptedActivity
			&& InterruptedActivity->ExecutionMode == ENPCAutonomyExecutionMode::BlueprintEvent)
		{
			OnBlueprintActivityAborted.Broadcast(InterruptedActivityID, Reason);
		}
		if (ActionQueueComponent)
		{
			ActionQueueComponent->StopActionQueue();
		}
		bInterruptingActivity = false;
		if (const UWorld* World = GetWorld())
		{
			LastActivityEndWorldTime = World->GetTimeSeconds();
		}
		OnAutonomyActivityFinished.Broadcast(InterruptedActivityID, true);
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[Autonomy] activity interrupted. Activity=%s Reason=%s"),
			*InterruptedActivityID,
			*Reason);
		if (bInterruptedAmbientActivity)
		{
			UE_LOG(LogTemp, Log, TEXT("[Ambient] interrupted. Activity=%s Reason=%s"), *InterruptedActivityID, *Reason);
		}
	}

	ScheduleNextDecision(DelayAfterPlayerInteractionSeconds);
}

void UNPCAutonomyComponent::SetPlayerContext(const FNPCAutonomyPlayerContext& NewContext, bool bMeaningfulChange)
{
	PlayerContext = NewContext;
	if (bMeaningfulChange)
	{
		if (bActivityRunning
			|| bDecisionPending
			|| ActivePlan.Status == ENPCAutonomyPlanStatus::Active)
		{
			InterruptAutonomy(TEXT("player_context_changed"));
		}
		else if (DecisionMode == ENPCAutonomyDecisionMode::FixedIntervalSingleAction)
		{
			ScheduleNextDecision(0.0f);
		}
	}
}

void UNPCAutonomyComponent::SetWorldStateTag(FName Tag, bool bValue, bool bMeaningfulChange)
{
	if (Tag.IsNone())
	{
		return;
	}

	const bool bPreviousValue = GetWorldStateTag(Tag);
	WorldStateTags.Add(Tag, bValue);
	if (bMeaningfulChange && bPreviousValue != bValue)
	{
		const FString Trigger = FString::Printf(TEXT("world_state_changed:%s"), *Tag.ToString());
		MarkMeaningfulContextChange(Trigger);
		if (bActivityRunning || bDecisionPending || ActivePlan.Status == ENPCAutonomyPlanStatus::Active)
		{
			InterruptAutonomy(TEXT("world_state_changed"));
			ScheduleNextDecision(0.0f);
		}
		else
		{
			ScheduleNextDecision(0.0f);
		}
	}
}

bool UNPCAutonomyComponent::RequestDiningAreaCleanup(const FString& TableID)
{
	ResolveReferences();
	if (!bAutonomyEnabled || !ServiceManager || !MaidNPCActor || !ActionQueueComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AutonomyPlan] explicit cleanup request rejected because autonomy references are unavailable."));
		return false;
	}

	if (ServiceManager->CurrentState != ECafeServiceState::Served)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[AutonomyPlan] explicit cleanup request rejected before an order was served. State=%s Table=%s"),
			*GetServiceStateName(),
			*TableID);
		return false;
	}

	static const TArray<FString> CleanupSteps = {
		TEXT("inspect_table_area"),
		TEXT("tidy_table_area"),
		TEXT("collect_tableware")
	};
	for (const FString& StepID : CleanupSteps)
	{
		if (!FindActivityDefinition(StepID))
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[AutonomyPlan] explicit cleanup request failed because activity %s is missing."),
				*StepID);
			return false;
		}
	}

	// An explicit service request supersedes any suspended daily agenda.
	InterruptAutonomy(TEXT("world_state_changed"));
	ClearActivePlan(TEXT("explicit_cleanup_request"));

	const FName CleanupTag(TEXT("dining_area_needs_tidy"));
	WorldStateTags.Add(CleanupTag, true);
	MotiveState.Duty = FMath::Max(MotiveState.Duty, DutyDecisionThreshold);
	for (const FString& StepID : CleanupSteps)
	{
		ActivityCooldownUntil.Remove(StepID);
	}

	ActivePlan.Goal = TEXT("Clear the customer's used dining table");
	ActivePlan.Steps = CleanupSteps;
	ActivePlan.CurrentStepIndex = 0;
	ActivePlan.Commitment = TEXT("medium");
	ActivePlan.Trigger = TEXT("explicit_player_cleanup_request");
	ActivePlan.Status = ENPCAutonomyPlanStatus::Active;
	LastDecisionReason = ActivePlan.Trigger;
	MarkMeaningfulContextChange(ActivePlan.Trigger);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[AutonomyPlan] explicit cleanup plan created. Table=%s Steps=%d"),
		*TableID,
		ActivePlan.Steps.Num());

	if (!TryStartCurrentPlanStep())
	{
		ScheduleNextDecision(0.0f);
		UE_LOG(LogTemp, Log, TEXT("[AutonomyPlan] explicit cleanup plan queued until the NPC is ready."));
	}
	return true;
}

bool UNPCAutonomyComponent::GetWorldStateTag(FName Tag) const
{
	if (const bool* Value = WorldStateTags.Find(Tag))
	{
		return *Value;
	}
	return false;
}

void UNPCAutonomyComponent::NotifyPlayerInteraction()
{
	InterruptAutonomy(TEXT("player_interaction"));
}

bool UNPCAutonomyComponent::IsAutonomyActivityRunning() const
{
	return bActivityRunning;
}

bool UNPCAutonomyComponent::HasProtectedTaskInProgress() const
{
	auto IsProtectedTask = [this](const FString& ActivityID)
	{
		const FNPCAutonomyActivityDefinition* Activity = FindActivityDefinition(ActivityID);
		return Activity
			&& (Activity->Category == ENPCAutonomyActivityCategory::Task
				|| Activity->bRequiredTaskStep);
	};

	if (bActivityRunning && IsProtectedTask(CurrentActivityID))
	{
		return true;
	}
	if (ActivePlan.Status == ENPCAutonomyPlanStatus::None)
	{
		return false;
	}
	for (int32 Index = FMath::Max(ActivePlan.CurrentStepIndex, 0); Index < ActivePlan.Steps.Num(); ++Index)
	{
		if (IsProtectedTask(ActivePlan.Steps[Index]))
		{
			return true;
		}
	}
	return false;
}

UNPCAutonomyComponent* UNPCAutonomyComponent::GetPrimaryAutonomyComponent()
{
	return GPrimaryAutonomyComponent.Get();
}

FString UNPCAutonomyComponent::BuildInteractiveDialogueContext()
{
	PreparePlayerSpeechStartle();

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("activity_running_when_player_spoke"), bActivityRunning);
	Root->SetStringField(TEXT("npc_location"), LastKnownNPCZone);
	if (bSeatInvitationPending)
	{
		TSharedRef<FJsonObject> InvitationObject = MakeShared<FJsonObject>();
		InvitationObject->SetBoolField(TEXT("pending"), true);
		InvitationObject->SetStringField(TEXT("offered_seat_id"), PendingSeatInvitationSeatID);
		InvitationObject->SetBoolField(TEXT("explicit_consent_required"), true);
		InvitationObject->SetStringField(
			TEXT("situation_ja"),
			TEXT("澄羽はお客様に、向かいの席に座ってもいいか尋ね、返事を待っている。"));
		InvitationObject->SetStringField(
			TEXT("resolution_rule_ja"),
			TEXT("明確な同意のときだけ着席し、拒否または別の用件なら着席しない。曖昧な返事なら一度だけ確認する。"));
		Root->SetObjectField(TEXT("seat_invitation"), InvitationObject);
	}
	TSharedRef<FJsonObject> EmbodiedCapabilities = MakeShared<FJsonObject>();
	EmbodiedCapabilities->SetBoolField(TEXT("facial_expression"), true);
	EmbodiedCapabilities->SetBoolField(TEXT("face_player"), true);
	EmbodiedCapabilities->SetBoolField(
		TEXT("hand_gesture_executor_available"),
		HasInternalEmbodiedGestureExecutor() || OnEmbodiedGestureRequested.IsBound());
	EmbodiedCapabilities->SetBoolField(TEXT("voice_style_audio_available"), false);
	Root->SetObjectField(TEXT("embodied_capabilities"), EmbodiedCapabilities);

	TSharedRef<FJsonObject> StartleContext = MakeShared<FJsonObject>();
	const FString PlayerDirection = PendingStartleRearFactor >= 0.7f ? TEXT("behind")
		: PendingStartleRearFactor >= 0.3f ? TEXT("side")
		: TEXT("front");
	StartleContext->SetBoolField(TEXT("triggered"), bPendingPlayerSpeechStartle);
	StartleContext->SetNumberField(TEXT("focus"), PendingStartleFocus);
	StartleContext->SetNumberField(TEXT("unexpectedness"), PendingStartleUnexpectedness);
	StartleContext->SetNumberField(TEXT("rear_factor"), PendingStartleRearFactor);
	StartleContext->SetStringField(TEXT("player_direction"), PlayerDirection);
	StartleContext->SetNumberField(TEXT("expected_conversation"), PendingStartleExpectedConversation);
	StartleContext->SetNumberField(TEXT("probability"), PendingStartleProbability);
	StartleContext->SetNumberField(TEXT("intensity"), PendingStartleIntensity);
	StartleContext->SetNumberField(TEXT("player_distance"), PendingStartlePlayerDistance);
	if (bPendingPlayerSpeechStartle)
	{
		const FString ImmediateScene = PlayerDirection == TEXT("behind")
			? TEXT("お客様が澄羽の背後から突然声をかけたため、澄羽は実際にびっくりした。")
			: PlayerDirection == TEXT("side")
			? TEXT("お客様が澄羽の横から突然声をかけたため、澄羽は実際にびっくりした。")
			: TEXT("集中していた澄羽にお客様が突然声をかけたため、澄羽は実際にびっくりした。");
		StartleContext->SetStringField(TEXT("immediate_scene_ja"), ImmediateScene);
		StartleContext->SetStringField(
			TEXT("reply_requirement_ja"),
			TEXT("通常の挨拶として流さず、最初に自然な驚きの声を出し、その後も驚いた理由や落ち着こうとする様子を台詞に反映する。"));
	}
	Root->SetObjectField(TEXT("player_speech_startle"), StartleContext);

	auto MakeActivityObject = [this](const FString& ActivityID)
	{
		TSharedRef<FJsonObject> ActivityObject = MakeShared<FJsonObject>();
		ActivityObject->SetStringField(TEXT("id"), ActivityID);
		FString DialogueSummaryJapanese;
		if (ActivityID.Equals(TEXT("phone_at_counter"), ESearchCase::IgnoreCase))
		{
			DialogueSummaryJapanese = TEXT("カウンターで少しスマートフォンを見ている");
		}
		else if (ActivityID.Equals(TEXT("water_flower"), ESearchCase::IgnoreCase))
		{
			DialogueSummaryJapanese = TEXT("花に水をあげている");
		}
		else if (ActivityID.Equals(TEXT("collect_tableware"), ESearchCase::IgnoreCase))
		{
			DialogueSummaryJapanese = TEXT("使い終わった食器を片付けている");
		}
		else if (ActivityID.Equals(TEXT("tidy_table_area"), ESearchCase::IgnoreCase))
		{
			DialogueSummaryJapanese = TEXT("テーブルをきれいに整えている");
		}
		else if (ActivityID.Equals(TEXT("inspect_table_area"), ESearchCase::IgnoreCase))
		{
			DialogueSummaryJapanese = TEXT("片付けるテーブルの状態を確認している");
		}
		else if (ActivityID.Equals(TEXT("review_shift_notes"), ESearchCase::IgnoreCase))
		{
			DialogueSummaryJapanese = TEXT("カウンターで端末を使って業務メモを確認している");
		}
		else if (ActivityID.Equals(TEXT("rest_on_sofa"), ESearchCase::IgnoreCase))
		{
			DialogueSummaryJapanese = TEXT("疲れたため、ソファに座って少し休んでいる");
		}
		else if (ActivityID.Equals(TEXT("sit_with_player"), ESearchCase::IgnoreCase))
		{
			DialogueSummaryJapanese = TEXT("お客様の向かいの席に座って、落ち着いて話そうとしている");
		}
		if (!DialogueSummaryJapanese.IsEmpty())
		{
			ActivityObject->SetStringField(TEXT("dialogue_summary_ja"), DialogueSummaryJapanese);
		}
		if (const FNPCAutonomyActivityDefinition* Activity = FindActivityDefinition(ActivityID))
		{
			ActivityObject->SetStringField(TEXT("display_name"), Activity->DisplayName.ToString());
			ActivityObject->SetStringField(TEXT("description"), Activity->LLMDescription.TrimStartAndEnd());
			ActivityObject->SetStringField(TEXT("category"), GetCategoryName(Activity->Category));
			ActivityObject->SetStringField(TEXT("target"), Activity->TargetSceneTag.TrimStartAndEnd());
		}
		return ActivityObject;
	};

	if (bActivityRunning && !CurrentActivityID.TrimStartAndEnd().IsEmpty())
	{
		TSharedRef<FJsonObject> CurrentActivity = MakeActivityObject(CurrentActivityID);
		CurrentActivity->SetStringField(
			TEXT("layer"),
			bCurrentActivityIsAmbient ? TEXT("ambient")
			: RuntimePhase == ENPCAutonomyRuntimePhase::EmotionReaction ? TEXT("emotion_reaction")
			: TEXT("plan"));
		const bool bCanContinueDuringConversation = CanPreserveActivityForEmbodiedResponse();
		CurrentActivity->SetBoolField(TEXT("can_continue_during_conversation"), bCanContinueDuringConversation);
		CurrentActivity->SetBoolField(TEXT("will_pause_for_this_conversation"), !bCanContinueDuringConversation);
		Root->SetObjectField(TEXT("activity_before_player_interruption"), CurrentActivity);

		TSharedRef<FJsonObject> BodyResources = MakeShared<FJsonObject>();
		BodyResources->SetStringField(TEXT("locomotion"), TEXT("available"));
		BodyResources->SetStringField(TEXT("upper_body"), TEXT("available"));
		BodyResources->SetStringField(TEXT("left_hand"), TEXT("available"));
		BodyResources->SetStringField(TEXT("right_hand"), TEXT("available"));
		BodyResources->SetStringField(TEXT("prop"), TEXT("none"));
		if (CurrentActivityID.Equals(TEXT("phone_at_counter"), ESearchCase::IgnoreCase))
		{
			BodyResources->SetStringField(TEXT("upper_body"), TEXT("occupied_by_phone_animation"));
			BodyResources->SetStringField(TEXT("left_hand"), TEXT("partially_available"));
			BodyResources->SetStringField(TEXT("right_hand"), TEXT("occupied_by_phone"));
			BodyResources->SetStringField(TEXT("prop"), TEXT("phone"));
		}
		else if (CurrentActivityID.Equals(TEXT("water_flower"), ESearchCase::IgnoreCase))
		{
			BodyResources->SetStringField(TEXT("upper_body"), TEXT("occupied_by_watering_animation"));
			BodyResources->SetStringField(TEXT("left_hand"), TEXT("occupied"));
			BodyResources->SetStringField(TEXT("right_hand"), TEXT("occupied_by_watering_can"));
			BodyResources->SetStringField(TEXT("prop"), TEXT("watering_can"));
		}
		else if (CurrentActivityID.Equals(TEXT("rest_on_sofa"), ESearchCase::IgnoreCase)
			|| CurrentActivityID.Equals(TEXT("sit_with_player"), ESearchCase::IgnoreCase))
		{
			BodyResources->SetStringField(TEXT("locomotion"), TEXT("seated"));
			BodyResources->SetStringField(TEXT("upper_body"), TEXT("available_for_seated_expression"));
		}
		Root->SetObjectField(TEXT("body_resource_state"), BodyResources);
	}

	TArray<TSharedPtr<FJsonValue>> RecentActivityValues;
	const double CurrentWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (int32 Index = RecentActions.Num() - 1; Index >= 0; --Index)
	{
		TSharedRef<FJsonObject> RecentActivity = MakeActivityObject(RecentActions[Index]);
		if (RecentActionCompletedWorldTimes.IsValidIndex(Index))
		{
			RecentActivity->SetNumberField(
				TEXT("seconds_ago"),
				FMath::Max(CurrentWorldTime - RecentActionCompletedWorldTimes[Index], 0.0));
		}
		RecentActivityValues.Add(MakeShared<FJsonValueObject>(RecentActivity));
	}
	Root->SetArrayField(TEXT("recent_completed_activities_newest_first"), RecentActivityValues);

	const FNPCEmotionState EmotionState = EmotionComponent
		? EmotionComponent->GetCurrentState()
		: FNPCEmotionState();
	TSharedRef<FJsonObject> EmotionObject = MakeShared<FJsonObject>();
	EmotionObject->SetStringField(TEXT("label"), EmotionState.EmotionLabel);
	EmotionObject->SetStringField(TEXT("performance_tone"), ResolveCurrentPerformanceTone());
	EmotionObject->SetStringField(TEXT("expression_intent"), EmotionState.ExpressionIntent);
	EmotionObject->SetNumberField(TEXT("short_term_valence"), EmotionState.ShortTermValence);
	EmotionObject->SetNumberField(TEXT("arousal"), EmotionState.Arousal);
	EmotionObject->SetNumberField(TEXT("mood_valence"), EmotionState.MoodValence);
	Root->SetObjectField(TEXT("current_emotional_state"), EmotionObject);

	if (ActivePlan.Status != ENPCAutonomyPlanStatus::None)
	{
		TSharedRef<FJsonObject> PlanObject = MakeShared<FJsonObject>();
		PlanObject->SetStringField(TEXT("goal"), ActivePlan.Goal);
		PlanObject->SetStringField(
			TEXT("status"),
			ActivePlan.Status == ENPCAutonomyPlanStatus::Suspended ? TEXT("suspended") : TEXT("active"));
		PlanObject->SetNumberField(TEXT("current_step_index"), ActivePlan.CurrentStepIndex);
		Root->SetObjectField(TEXT("active_plan"), PlanObject);
	}

	if (!LastAppraisedMemorySummary.TrimStartAndEnd().IsEmpty())
	{
		TSharedRef<FJsonObject> MemoryObject = MakeShared<FJsonObject>();
		MemoryObject->SetStringField(TEXT("summary"), LastAppraisedMemorySummary.TrimStartAndEnd());
		MemoryObject->SetStringField(TEXT("interpretation"), LastAppraisalInterpretation.TrimStartAndEnd());
		Root->SetObjectField(TEXT("recently_appraised_memory"), MemoryObject);
	}

	return SerializeResearchPayload(Root);
}

void UNPCAutonomyComponent::CompleteBlueprintActivity(const FString& ActivityID, bool bSuccess)
{
	if (!bActivityRunning
		|| RuntimePhase != ENPCAutonomyRuntimePhase::BlueprintExecuting
		|| !CurrentActivityID.Equals(ActivityID.TrimStartAndEnd(), ESearchCase::IgnoreCase))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[Autonomy] ignored blueprint completion for inactive activity. Activity=%s Current=%s"),
			*ActivityID,
			*CurrentActivityID);
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BlueprintActivityTimeoutTimerHandle);
	}

	const FNPCAutonomyActivityDefinition* Activity = FindActivityDefinition(CurrentActivityID);
	if (!bSuccess || !Activity)
	{
		OnBlueprintActivityAborted.Broadcast(CurrentActivityID, TEXT("blueprint_reported_failure"));
		FinishActivity(true);
		return;
	}

	ApplyActivitySuccess(*Activity);
	if (Activity->bReturnToCounterAfterCompletion
		&& !Activity->TargetSceneTag.Equals(TEXT("counter"), ESearchCase::IgnoreCase)
		&& !IsMaidNearSceneTargetTag(TEXT("counter")))
	{
		if (!ActionQueueComponent)
		{
			FinishActivity(true);
			return;
		}
		ActionQueueComponent->ClearActionQueue();
		ActionQueueComponent->EnqueueMoveToSceneTag(TEXT("counter"));
		RuntimePhase = ENPCAutonomyRuntimePhase::BlueprintReturnMove;
		ActionQueueComponent->StartActionQueue();
		if (!ActionQueueComponent->bIsRunningQueue)
		{
			FinishActivity(true);
		}
		return;
	}

	if (Activity->bReturnToCounterAfterCompletion)
	{
		LastKnownNPCZone = TEXT("counter");
	}
	FinishActivity(false);
}

void UNPCAutonomyComponent::ResolveReferences()
{
	if (!ServiceManager)
	{
		ServiceManager = Cast<ACafeServiceManagerActor>(GetOwner());
	}
	if (ServiceManager)
	{
		MemoryComponent = ServiceManager->MemoryComponent;
		EmotionComponent = ServiceManager->EmotionComponent;
		ResearchLoggerComponent = ServiceManager->ResearchLoggerComponent;
	}

	AActor* ResolvedMaidActor = ServiceManager ? ServiceManager->MaidNPCActor : GetOwner();
	if (ResolvedMaidActor != MaidNPCActor)
	{
		MaidNPCActor = ResolvedMaidActor;
	}

	UNPCActionQueueComponent* ResolvedActionQueue = MaidNPCActor
		? MaidNPCActor->FindComponentByClass<UNPCActionQueueComponent>()
		: nullptr;
	BindActionQueue(ResolvedActionQueue);
}

void UNPCAutonomyComponent::BindActionQueue(UNPCActionQueueComponent* NewActionQueue)
{
	if (ActionQueueComponent == NewActionQueue)
	{
		return;
	}

	if (ActionQueueComponent)
	{
		ActionQueueComponent->OnActionQueueFinished.RemoveDynamic(
			this,
			&UNPCAutonomyComponent::HandleActionQueueFinished);
	}

	ActionQueueComponent = NewActionQueue;
	if (ActionQueueComponent)
	{
		ActionQueueComponent->OnActionQueueFinished.AddDynamic(
			this,
			&UNPCAutonomyComponent::HandleActionQueueFinished);
	}
}

void UNPCAutonomyComponent::BuildRuntimeActivityLibrary()
{
	RuntimeActivities = LoadedActionSet && !LoadedActionSet->Activities.IsEmpty()
		? LoadedActionSet->Activities
		: Activities;

	auto FindRuntimeActivity = [this](const FString& ActivityID) -> FNPCAutonomyActivityDefinition*
	{
		return RuntimeActivities.FindByPredicate([&ActivityID](const FNPCAutonomyActivityDefinition& Activity)
		{
			return Activity.ActivityID.Equals(ActivityID, ESearchCase::IgnoreCase);
		});
	};

	if (FNPCAutonomyActivityDefinition* Activity = FindRuntimeActivity(TEXT("phone_at_counter")))
	{
		Activity->AmbientDrive = ENPCAutonomyAmbientDrive::BoredomRelief;
		Activity->AmbientBaseWeight = 9.0f;
		Activity->StimulationGain = 12.0f;
		Activity->EnergyDelta = -3.0f;
		Activity->CooldownSeconds = 70.0f;
		if (Activity->HeldPropMesh.IsNull())
		{
			Activity->HeldPropMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
				TEXT("/Game/Fab/Cartoon_Phone_3D/cartoonphone.cartoonphone")));
		}
		Activity->HeldPropSocketName = FName(TEXT("hand_L"));
		if (Activity->HeldPropRelativeTransform.GetScale3D().Equals(FVector::OneVector))
		{
			Activity->HeldPropRelativeTransform.SetScale3D(FVector(0.0145f));
		}
	}
	if (FNPCAutonomyActivityDefinition* Activity = FindRuntimeActivity(TEXT("water_flower")))
	{
		Activity->CooldownSeconds = 5.0f;
		if (Activity->HeldPropMesh.IsNull())
		{
			Activity->HeldPropMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
				TEXT("/Game/Fab/CC0_-_Watering_Can_6/cc0_watering_can_6/StaticMeshes/cc0_watering_can_6.cc0_watering_can_6")));
		}
		Activity->HeldPropSocketName = FName(TEXT("hand_R"));
		if (Activity->HeldPropRelativeTransform.GetScale3D().Equals(FVector::OneVector))
		{
			Activity->HeldPropRelativeTransform.SetScale3D(FVector(3.5f));
		}
	}
	if (FNPCAutonomyActivityDefinition* Activity = FindRuntimeActivity(TEXT("daydream_at_counter")))
	{
		Activity->AmbientDrive = ENPCAutonomyAmbientDrive::Neutral;
		Activity->AmbientBaseWeight = 5.0f;
		Activity->StimulationGain = 3.0f;
		Activity->EnergyDelta = 2.0f;
		Activity->CooldownSeconds = 50.0f;
	}
	if (FNPCAutonomyActivityDefinition* Activity = FindRuntimeActivity(TEXT("look_around")))
	{
		Activity->AmbientDrive = ENPCAutonomyAmbientDrive::Curiosity;
		Activity->AmbientBaseWeight = 6.0f;
		Activity->StimulationGain = 5.0f;
		Activity->EnergyDelta = -1.0f;
		Activity->CooldownSeconds = 45.0f;
	}
	if (FNPCAutonomyActivityDefinition* Activity = FindRuntimeActivity(TEXT("doze_at_counter")))
	{
		Activity->AmbientDrive = ENPCAutonomyAmbientDrive::EnergyRecovery;
		Activity->AmbientBaseWeight = 5.0f;
		Activity->StimulationGain = 1.0f;
		Activity->EnergyDelta = 5.0f;
		Activity->CooldownSeconds = 90.0f;
	}
	if (!FindRuntimeActivity(TEXT("rest_on_sofa")))
	{
		FNPCAutonomyActivityDefinition Activity;
		Activity.ActivityID = TEXT("rest_on_sofa");
		Activity.DisplayName = FText::FromString(TEXT("Rest on Sofa"));
		Activity.LLMDescription = TEXT(
			"Sit on an available sofa for a short recovery only when energy is especially low and no service task is urgent.");
		Activity.Category = ENPCAutonomyActivityCategory::Leisure;
		Activity.AmbientDrive = ENPCAutonomyAmbientDrive::EnergyRecovery;
		Activity.AmbientBaseWeight = 12.0f;
		Activity.AnimationFallbackSeconds = 15.0f;
		Activity.CooldownSeconds = 90.0f;
		Activity.StimulationGain = 2.0f;
		Activity.EnergyDelta = 15.0f;
		Activity.bEnabled = true;
		RuntimeActivities.Add(MoveTemp(Activity));
	}
	if (!FindRuntimeActivity(TEXT("sit_with_player")))
	{
		FNPCAutonomyActivityDefinition Activity;
		Activity.ActivityID = TEXT("sit_with_player");
		Activity.DisplayName = FText::FromString(TEXT("Sit with Player"));
		Activity.LLMDescription = TEXT(
			"Propose sitting in the available seat across from the seated player when a calm, longer conversation is socially appropriate. The local consent gate asks permission before Sumiha sits.");
		Activity.Category = ENPCAutonomyActivityCategory::Preference;
		Activity.AmbientDrive = ENPCAutonomyAmbientDrive::Neutral;
		Activity.AnimationFallbackSeconds = 15.0f;
		Activity.CooldownSeconds = 120.0f;
		Activity.StimulationGain = 6.0f;
		Activity.EnergyDelta = 1.0f;
		Activity.bEnabled = true;
		RuntimeActivities.Add(MoveTemp(Activity));
	}

	auto AddPlaceholder = [this, &FindRuntimeActivity](
		const FString& ActivityID,
		const FString& DisplayName,
		const FString& Description,
		ENPCAutonomyActivityCategory Category,
		const TCHAR* AnimationPath,
		float StimulationGain,
		float EnergyDelta,
		float CooldownSeconds)
	{
		if (FindRuntimeActivity(ActivityID))
		{
			return;
		}

		FNPCAutonomyActivityDefinition Placeholder;
		Placeholder.ActivityID = ActivityID;
		Placeholder.DisplayName = FText::FromString(DisplayName);
		Placeholder.LLMDescription = Description;
		Placeholder.Category = Category;
		Placeholder.TargetSceneTag = TEXT("counter");
		Placeholder.Animation = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(AnimationPath));
		Placeholder.AnimationSlotName = FName(TEXT("DefaultSlot"));
		Placeholder.AnimationPlayRate = 1.0f;
		Placeholder.AnimationFallbackSeconds = 5.0f;
		Placeholder.CooldownSeconds = CooldownSeconds;
		Placeholder.StimulationGain = StimulationGain;
		Placeholder.EnergyDelta = EnergyDelta;
		Placeholder.bEnabled = true;
		RuntimeActivities.Add(MoveTemp(Placeholder));
	};

	AddPlaceholder(
		TEXT("hum_at_counter_placeholder"),
		TEXT("Hum at Counter (Placeholder)"),
		TEXT("Hum softly at the counter while remaining available for service."),
		ENPCAutonomyActivityCategory::Leisure,
		TEXT("/Game/Characters/OriginalCafeMaid/Animations/Anim_KA_Idle08_ComeUpWithAnIdea_OCM.Anim_KA_Idle08_ComeUpWithAnIdea_OCM"),
		3.0f,
		-1.0f,
		50.0f);
	AddPlaceholder(
		TEXT("straighten_uniform_placeholder"),
		TEXT("Straighten Uniform (Placeholder)"),
		TEXT("Briefly straighten her uniform during a quiet moment."),
		ENPCAutonomyActivityCategory::Leisure,
		TEXT("/Game/Characters/OriginalCafeMaid/Animations/Anim_KA_Idle03_LookAtHands_OCM.Anim_KA_Idle03_LookAtHands_OCM"),
		2.0f,
		-0.5f,
		45.0f);
	AddPlaceholder(
		TEXT("compose_self_at_counter_placeholder"),
		TEXT("Compose Self at Counter (Placeholder)"),
		TEXT("Pause at the counter, look down briefly, and take a breath to settle a negative feeling."),
		ENPCAutonomyActivityCategory::Expression,
		TEXT("/Game/Characters/OriginalCafeMaid/Animations/Anim_KA_Idle04_LookAtFeet_OCM.Anim_KA_Idle04_LookAtFeet_OCM"),
		1.0f,
		1.0f,
		55.0f);
	AddPlaceholder(
		TEXT("inspect_cafe_placeholder"),
		TEXT("Inspect Cafe (Placeholder)"),
		TEXT("Deliberately inspect the cafe and check whether the room looks orderly."),
		ENPCAutonomyActivityCategory::Preference,
		TEXT("/Game/Characters/OriginalCafeMaid/Animations/Anim_KA_Idle02_LookLeftAndRight_OCM.Anim_KA_Idle02_LookLeftAndRight_OCM"),
		2.0f,
		-1.0f,
		60.0f);
	AddPlaceholder(
		TEXT("organize_counter_placeholder"),
		TEXT("Organize Counter (Placeholder)"),
		TEXT("Organize the counter as a self-initiated housekeeping preference."),
		ENPCAutonomyActivityCategory::Preference,
		TEXT("/Game/Characters/OriginalCafeMaid/Animations/Anim_KA_Idle12_LeaningForward_OCM.Anim_KA_Idle12_LeaningForward_OCM"),
		3.0f,
		-2.0f,
		75.0f);
	if (!FindRuntimeActivity(TEXT("check_supplies")))
	{
		AddPlaceholder(
			TEXT("check_supplies_placeholder"),
			TEXT("Check Supplies (Placeholder)"),
			TEXT("Check the supplies kept at the counter and mentally note their condition."),
			ENPCAutonomyActivityCategory::Preference,
			TEXT("/Game/Characters/OriginalCafeMaid/Animations/Anim_KA_Idle03_LookAtHands_OCM.Anim_KA_Idle03_LookAtHands_OCM"),
			2.0f,
			-1.0f,
			65.0f);
	}

	if (FNPCAutonomyActivityDefinition* Activity = FindRuntimeActivity(TEXT("hum_at_counter_placeholder")))
	{
		Activity->AmbientDrive = ENPCAutonomyAmbientDrive::PositiveEmotion;
		Activity->AmbientBaseWeight = 5.0f;
	}
	if (FNPCAutonomyActivityDefinition* Activity = FindRuntimeActivity(TEXT("straighten_uniform_placeholder")))
	{
		Activity->AmbientDrive = ENPCAutonomyAmbientDrive::Neutral;
		Activity->AmbientBaseWeight = 4.0f;
	}
	if (FNPCAutonomyActivityDefinition* Activity = FindRuntimeActivity(TEXT("compose_self_at_counter_placeholder")))
	{
		Activity->AmbientDrive = ENPCAutonomyAmbientDrive::NegativeEmotion;
		Activity->AmbientBaseWeight = 6.0f;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[Autonomy] runtime activity library ready. Activities=%d AmbientPlaceholders=3 PlanPlaceholders=3"),
		RuntimeActivities.Num());
}

const TArray<FNPCAutonomyActivityDefinition>& UNPCAutonomyComponent::GetConfiguredActivities() const
{
	if (!RuntimeActivities.IsEmpty())
	{
		return RuntimeActivities;
	}
	if (LoadedActionSet && !LoadedActionSet->Activities.IsEmpty())
	{
		return LoadedActionSet->Activities;
	}
	return Activities;
}

void UNPCAutonomyComponent::UpdateInternalState(float DeltaSeconds)
{
	if (DeltaSeconds <= 0.0f)
	{
		return;
	}

	const bool bAutonomyContext = ServiceManager && IsServiceStateAutonomyAllowed(ServiceManager->CurrentState);
	const bool bQuietIdle = bAutonomyContext
		&& !bActivityRunning
		&& !bSeatInvitationApproachInProgress
		&& !bSeatInvitationPending
		&& ActionQueueComponent
		&& !ActionQueueComponent->IsBusyForAutonomy();
	const float MotiveTimeScale = bFastValidationMode
		? FMath::Max(FastValidationMotiveTimeScale, 1.0f)
		: 1.0f;
	const float MotiveDeltaSeconds = DeltaSeconds * MotiveTimeScale;

	if (bQuietIdle)
	{
		MotiveState.Boredom += BoredomIncreasePerIdleSecond * MotiveDeltaSeconds;
		ResourceState.Energy += IdleEnergyRecoveryPerSecond * DeltaSeconds;
	}
	else if (ServiceManager && !bAutonomyContext)
	{
		MotiveState.Boredom -= BoredomIncreasePerIdleSecond * 0.5f * DeltaSeconds;
	}

	const bool bOutstandingTask = HasOutstandingTask();
	const bool bTaskIsAlreadyBeingHandled = bOutstandingTask
		&& (ActivePlan.Status == ENPCAutonomyPlanStatus::Active
			|| ActivePlan.Status == ENPCAutonomyPlanStatus::Suspended
			|| (bActivityRunning && !bCurrentActivityIsAmbient));
	if (bOutstandingTask && !bTaskIsAlreadyBeingHandled)
	{
		MotiveState.Duty += DutyIncreasePerSecondWithTask * MotiveDeltaSeconds;
	}
	else if (!bOutstandingTask)
	{
		MotiveState.Duty -= DutyDecayPerSecond * MotiveDeltaSeconds;
	}

	const bool bPlayerPresent = ServiceManager && ServiceManager->CurrentPlayerActor != nullptr;
	const float SocialAttentionTarget = bPlayerPresent
		? (ServiceManager->CurrentState == ECafeServiceState::WaitingForOrder ? 75.0f : 45.0f)
		: 15.0f;
	MotiveState.SocialAttention = FMath::FInterpConstantTo(
		MotiveState.SocialAttention,
		SocialAttentionTarget,
		DeltaSeconds,
		20.0f);

	MotiveState.Boredom = FMath::Clamp(MotiveState.Boredom, 0.0f, 100.0f);
	MotiveState.Duty = FMath::Clamp(MotiveState.Duty, 0.0f, 100.0f);
	MotiveState.SocialAttention = FMath::Clamp(MotiveState.SocialAttention, 0.0f, 100.0f);
	ResourceState.Energy = FMath::Clamp(ResourceState.Energy, 0.0f, 100.0f);
}

bool UNPCAutonomyComponent::HasOutstandingTask() const
{
	for (const FNPCAutonomyActivityDefinition& Activity : GetConfiguredActivities())
	{
		if (Activity.bEnabled
			&& Activity.Category == ENPCAutonomyActivityCategory::Task
			&& !Activity.RequiredWorldStateTag.IsNone()
			&& GetWorldStateTag(Activity.RequiredWorldStateTag))
		{
			return true;
		}
	}
	return false;
}

bool UNPCAutonomyComponent::ShouldRequestNewDecision(FString& OutTrigger) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (DecisionMode == ENPCAutonomyDecisionMode::FixedIntervalSingleAction)
	{
		OutTrigger = TEXT("fixed_interval_baseline");
		return true;
	}

	const double TimeSinceLastRequest = World->GetTimeSeconds() - LastDecisionRequestWorldTime;
	if (TimeSinceLastRequest < FMath::Max(MinimumSecondsBetweenLLMDecisions, 0.0f))
	{
		return false;
	}

	if (bMeaningfulContextChangePending)
	{
		OutTrigger = PendingDecisionTrigger.IsEmpty() ? TEXT("meaningful_context_change") : PendingDecisionTrigger;
		return true;
	}
	if (World->GetTimeSeconds() < PlanningSuppressedUntilWorldTime)
	{
		return false;
	}
	if (MotiveState.Duty >= DutyDecisionThreshold)
	{
		OutTrigger = TEXT("duty_threshold");
		return true;
	}
	if (MotiveState.Boredom >= CurrentBoredomDecisionThreshold)
	{
		OutTrigger = TEXT("boredom_threshold");
		return true;
	}

	return false;
}

void UNPCAutonomyComponent::RequestNewDecision(const FString& Trigger)
{
	const TArray<FString> AvailableActivityIDs = BuildAvailableActivityIDs();
	if (AvailableActivityIDs.IsEmpty() || !AutonomyLLMClient)
	{
		ScheduleNextDecision(DecisionErrorRetrySeconds);
		return;
	}

	AutonomyLLMClient->OpenAIModelName = AutonomyModelName;
	LastDecisionTrigger = Trigger;
	LastRequestedAvailableActions = AvailableActivityIDs;
	LastDecisionContextJson = BuildDecisionContextJson(AvailableActivityIDs);
	bDecisionPending = true;
	PendingDecisionRequestId = AutonomyLLMClient->SendAutonomyDecisionMessage(LastDecisionContextJson);
	if (PendingDecisionRequestId <= 0)
	{
		bDecisionPending = false;
		PendingDecisionRequestId = 0;
		ScheduleNextDecision(DecisionErrorRetrySeconds);
		return;
	}

	bMeaningfulContextChangePending = false;
	PendingDecisionTrigger = TEXT("");
	if (const UWorld* World = GetWorld())
	{
		LastDecisionRequestWorldTime = World->GetTimeSeconds();
	}
	++TotalDecisionRequests;
	TotalContextCharactersSent += LastDecisionContextJson.Len();
	NextDecisionEarliestWorldTime = TNumericLimits<double>::Max();
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("trigger"), LastDecisionTrigger);
	Payload->SetStringField(TEXT("context_json"), LastDecisionContextJson);
	Payload->SetNumberField(TEXT("available_action_count"), AvailableActivityIDs.Num());
	RecordResearchEvent(TEXT("plan_requested"), Payload);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[AutonomyGate] LLM requested. Trigger=%s Boredom=%.1f/%.1f Duty=%.1f Energy=%.1f Available=%d"),
		*LastDecisionTrigger,
		MotiveState.Boredom,
		CurrentBoredomDecisionThreshold,
		MotiveState.Duty,
		ResourceState.Energy,
		AvailableActivityIDs.Num());
}

void UNPCAutonomyComponent::MarkMeaningfulContextChange(const FString& Trigger)
{
	bMeaningfulContextChangePending = true;
	PendingDecisionTrigger = Trigger.TrimStartAndEnd();
}

void UNPCAutonomyComponent::RollNextBoredomThreshold()
{
	const float Minimum = FMath::Min(BoredomDecisionThresholdMin, BoredomDecisionThresholdMax);
	const float Maximum = FMath::Max(BoredomDecisionThresholdMin, BoredomDecisionThresholdMax);
	CurrentBoredomDecisionThreshold = FMath::FRandRange(Minimum, Maximum);
}

void UNPCAutonomyComponent::UpdateFastValidationDebugMessage()
{
	if (!bFastValidationMode || !bShowFastValidationDebug || !GEngine)
	{
		return;
	}

	FString Status;
	FColor Color = FColor::Cyan;
	if (!bAutonomyEnabled)
	{
		Status = TEXT("disabled");
		Color = FColor::Red;
	}
	else if (ServiceManager && !IsServiceStateAutonomyAllowed(ServiceManager->CurrentState))
	{
		Status = FString::Printf(TEXT("blocked by service: %s"), *GetServiceStateName());
		Color = FColor::Yellow;
	}
	else if (bEnableMultiStepPlanValidationScenario
		&& bWaitForPlayerEncounterBeforeValidation
		&& !bHasEncounteredPlayer)
	{
		Status = TEXT("waiting for player encounter before visible test");
		Color = FColor::Yellow;
	}
	else if (bAppraisalPending)
	{
		Status = FString::Printf(TEXT("appraising memory: %s"), *PendingAppraisalMemory.EventTag.ToString());
		Color = FColor::Green;
	}
	else if (bDecisionPending)
	{
		Status = FString::Printf(TEXT("LLM thinking: %s"), *LastDecisionTrigger);
		Color = FColor::Green;
	}
	else if (bActivityRunning)
	{
		if (RuntimePhase == ENPCAutonomyRuntimePhase::EmotionReaction)
		{
			Status = FString::Printf(TEXT("showing memory reaction: %s"), *CurrentExpressionIntent);
		}
		else if (bCurrentActivityIsAmbient)
		{
			Status = FString::Printf(TEXT("ambient behavior: %s"), *CurrentActivityID);
		}
		else
		{
			Status = FString::Printf(
				TEXT("plan %d/%d running: %s"),
				ActivePlan.CurrentStepIndex + 1,
				ActivePlan.Steps.Num(),
				*CurrentActivityID);
		}
		Color = FColor::Green;
	}
	else if (ActivePlan.Status == ENPCAutonomyPlanStatus::Suspended)
	{
		Status = FString::Printf(
			TEXT("plan suspended at %d/%d: %s"),
			ActivePlan.CurrentStepIndex + 1,
			ActivePlan.Steps.Num(),
			*ActivePlan.Goal);
		Color = FColor::Yellow;
	}
	else if (ActivePlan.Status == ENPCAutonomyPlanStatus::Active)
	{
		Status = FString::Printf(
			TEXT("plan %d/%d preparing: %s"),
			ActivePlan.CurrentStepIndex + 1,
			ActivePlan.Steps.Num(),
			*ActivePlan.Goal);
	}
	else if (ActionQueueComponent && ActionQueueComponent->IsBusyForAutonomy())
	{
		Status = TEXT("waiting for current NPC action");
	}
	else
	{
		Status = HasOutstandingTask() ? TEXT("task pressure building") : TEXT("state monitoring");
	}

	const float MotiveTimeScale = FMath::Max(FastValidationMotiveTimeScale, 1.0f);
	const float AcceleratedBoredomRate = BoredomIncreasePerIdleSecond * MotiveTimeScale;
	const bool bBoredomIncreasing =
		ServiceManager
		&& IsServiceStateAutonomyAllowed(ServiceManager->CurrentState)
		&& !bActivityRunning
		&& ActionQueueComponent
		&& !ActionQueueComponent->IsBusyForAutonomy();
	const float EstimatedBoredomSeconds = bBoredomIncreasing && AcceleratedBoredomRate > KINDA_SMALL_NUMBER
		? FMath::Max((CurrentBoredomDecisionThreshold - MotiveState.Boredom) / AcceleratedBoredomRate, 0.0f)
		: -1.0f;
	const FString BoredomEstimate = EstimatedBoredomSeconds >= 0.0f
		? FString::Printf(TEXT("~%.0fs"), EstimatedBoredomSeconds)
		: TEXT("paused");

	GEngine->AddOnScreenDebugMessage(
		FastValidationDebugMessageKey,
		FMath::Max(EvaluationIntervalSeconds * 1.5f, 1.0f),
		Color,
		FString::Printf(
			TEXT("Autonomy State Test | %s | Boredom %.1f/%.1f (%s) | Duty %.1f/%.1f | Energy %.1f | Emotion %s | Expression %s"),
			*Status,
			MotiveState.Boredom,
			CurrentBoredomDecisionThreshold,
			*BoredomEstimate,
			MotiveState.Duty,
			DutyDecisionThreshold,
			ResourceState.Energy,
			*CurrentEmotionLabel,
			*CurrentExpressionIntent));
}

bool UNPCAutonomyComponent::CanRunAutonomy() const
{
	if (!bAutonomyEnabled
		|| bDecisionPending
		|| bAppraisalPending
		|| bActivityRunning
		|| bSeatInvitationApproachInProgress
		|| bSeatInvitationPending
		|| !ServiceManager
		|| !MaidNPCActor
		|| !ActionQueueComponent
		|| !AutonomyLLMClient)
	{
		return false;
	}
	if (bEnableMultiStepPlanValidationScenario
		&& bWaitForPlayerEncounterBeforeValidation
		&& !bHasEncounteredPlayer)
	{
		return false;
	}

	return IsServiceStateAutonomyAllowed(ServiceManager->CurrentState)
		&& !ActionQueueComponent->IsBusyForAutonomy();
}

bool UNPCAutonomyComponent::IsServiceStateAutonomyAllowed(ECafeServiceState State) const
{
	switch (State)
	{
	case ECafeServiceState::Idle:
	case ECafeServiceState::WaitingForOrder:
	case ECafeServiceState::Served:
		return true;

	default:
		return false;
	}
}

TArray<FString> UNPCAutonomyComponent::BuildAvailableActivityIDs() const
{
	TArray<FString> AvailableActivityIDs;
	for (const FNPCAutonomyActivityDefinition& Activity : GetConfiguredActivities())
	{
		if (bEnableAmbientBehaviorLayer
			&& DecisionMode == ENPCAutonomyDecisionMode::EventTriggeredShortPlan
			&& (Activity.Category == ENPCAutonomyActivityCategory::Leisure
				|| Activity.Category == ENPCAutonomyActivityCategory::Expression))
		{
			continue;
		}
		if (IsActivityAvailable(Activity))
		{
			AvailableActivityIDs.Add(Activity.ActivityID.TrimStartAndEnd());
		}
	}
	return AvailableActivityIDs;
}

bool UNPCAutonomyComponent::IsActivityAvailable(const FNPCAutonomyActivityDefinition& Activity) const
{
	const FString ActivityID = Activity.ActivityID.TrimStartAndEnd();
	if (!Activity.bEnabled || ActivityID.IsEmpty())
	{
		return false;
	}
	if (ResourceState.Energy + KINDA_SMALL_NUMBER < Activity.MinimumEnergy)
	{
		return false;
	}
	if (ActivityID.Equals(TEXT("rest_on_sofa"), ESearchCase::IgnoreCase))
	{
		if (!ForcedSeatID.IsEmpty())
		{
			bool bForcedSeatAvailable = false;
			if (UWorld* World = GetWorld())
			{
				for (TActorIterator<AFurnitureMarkerActor> It(World); It; ++It)
				{
					const AFurnitureMarkerActor* Seat = *It;
					if (Seat
						&& Seat->FurnitureID.Equals(ForcedSeatID, ESearchCase::IgnoreCase)
						&& Seat->bAllowNPCSit
						&& Seat->IsSeatAvailableFor(MaidNPCActor))
					{
						bForcedSeatAvailable = true;
						break;
					}
				}
			}
			if (!bForcedSeatAvailable)
			{
				return false;
			}
		}
		else if (ResourceState.Energy > SofaRestEnergyThreshold || FindAvailableRestSeatID().IsEmpty())
		{
			return false;
		}
	}
	else if (ActivityID.Equals(TEXT("sit_with_player"), ESearchCase::IgnoreCase))
	{
		const bool bManualSeatOverride = !ForcedSeatID.IsEmpty();
		if ((!bManualSeatOverride
				&& bRequireServedStateForNaturalSocialSit
				&& (!ServiceManager || ServiceManager->CurrentState != ECafeServiceState::Served))
			|| FindAvailableSeatOppositePlayerID().IsEmpty())
		{
			return false;
		}
	}

	if (!Activity.RequiredWorldStateTag.IsNone() && !GetWorldStateTag(Activity.RequiredWorldStateTag))
	{
		return false;
	}

	if (!Activity.TargetSceneTag.TrimStartAndEnd().IsEmpty() && !HasSceneTargetTag(Activity.TargetSceneTag))
	{
		return false;
	}

	UWorld* World = GetWorld();
	const double CurrentTime = World ? World->GetTimeSeconds() : 0.0;
	if (const double* CooldownUntil = ActivityCooldownUntil.Find(ActivityID))
	{
		return CurrentTime >= *CooldownUntil;
	}

	return true;
}

FString UNPCAutonomyComponent::FindAvailableRestSeatID() const
{
	UWorld* World = GetWorld();
	if (!World || !MaidNPCActor)
	{
		return TEXT("");
	}

	const AFurnitureMarkerActor* BestSeat = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();
	for (TActorIterator<AFurnitureMarkerActor> It(World); It; ++It)
	{
		const AFurnitureMarkerActor* Seat = *It;
		if (!Seat
			|| !Seat->bAllowNPCSit
			|| !Seat->IsSeatAvailableFor(MaidNPCActor)
			|| (!Seat->FurnitureType.Equals(TEXT("sofa"), ESearchCase::IgnoreCase)
				&& !ContainsTagIgnoreCase(Seat->FurnitureTags, TEXT("npc_rest"))))
		{
			continue;
		}

		const float DistanceSq = FVector::DistSquared(
			MaidNPCActor->GetActorLocation(),
			Seat->GetNPCStandTransform().GetLocation());
		if (!BestSeat
			|| Seat->Priority > BestSeat->Priority
			|| (Seat->Priority == BestSeat->Priority && DistanceSq < BestDistanceSq))
		{
			BestSeat = Seat;
			BestDistanceSq = DistanceSq;
		}
	}

	return BestSeat ? BestSeat->FurnitureID : TEXT("");
}

FString UNPCAutonomyComponent::FindAvailableSeatOppositePlayerID() const
{
	UWorld* World = GetWorld();
	AActor* PlayerActor = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	const UPlayerSeatComponent* PlayerSeatComponent = PlayerActor
		? PlayerActor->FindComponentByClass<UPlayerSeatComponent>()
		: nullptr;
	if (!World
		|| !MaidNPCActor
		|| !PlayerSeatComponent
		|| !PlayerSeatComponent->bIsSeated
		|| PlayerSeatComponent->CurrentTableID.TrimStartAndEnd().IsEmpty())
	{
		return TEXT("");
	}

	const AFurnitureMarkerActor* BestSeat = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();
	for (TActorIterator<AFurnitureMarkerActor> It(World); It; ++It)
	{
		const AFurnitureMarkerActor* Seat = *It;
		if (!Seat
			|| !Seat->bAllowNPCSit
			|| Seat->FurnitureID.Equals(PlayerSeatComponent->CurrentSeatID, ESearchCase::IgnoreCase)
			|| !Seat->RelatedTableID.Equals(PlayerSeatComponent->CurrentTableID, ESearchCase::IgnoreCase)
			|| !Seat->IsSeatAvailableFor(MaidNPCActor))
		{
			continue;
		}

		const float DistanceSq = FVector::DistSquared(
			MaidNPCActor->GetActorLocation(),
			Seat->GetNPCStandTransform().GetLocation());
		if (!BestSeat
			|| Seat->Priority > BestSeat->Priority
			|| (Seat->Priority == BestSeat->Priority && DistanceSq < BestDistanceSq))
		{
			BestSeat = Seat;
			BestDistanceSq = DistanceSq;
		}
	}

	return BestSeat ? BestSeat->FurnitureID : TEXT("");
}

bool UNPCAutonomyComponent::HasSceneTargetTag(const FString& TargetTag) const
{
	const FString TrimmedTargetTag = TargetTag.TrimStartAndEnd();
	UWorld* World = GetWorld();
	if (!World || TrimmedTargetTag.IsEmpty())
	{
		return TrimmedTargetTag.IsEmpty();
	}

	for (TActorIterator<AFurnitureMarkerActor> It(World); It; ++It)
	{
		const AFurnitureMarkerActor* Furniture = *It;
		if (Furniture && ContainsTagIgnoreCase(Furniture->FurnitureTags, TrimmedTargetTag))
		{
			return true;
		}
	}

	for (TActorIterator<ACafeTargetPointActor> It(World); It; ++It)
	{
		const ACafeTargetPointActor* TargetPoint = *It;
		if (TargetPoint && TargetPoint->bIsAvailable && TargetPoint->HasTagString(TrimmedTargetTag))
		{
			return true;
		}
	}

	for (TActorIterator<AWaterablePlantActor> It(World); It; ++It)
	{
		const AWaterablePlantActor* Plant = *It;
		if (Plant && Plant->IsAvailableForTag(TrimmedTargetTag))
		{
			return true;
		}
	}

	for (TActorIterator<ATableMarkerActor> It(World); It; ++It)
	{
		const ATableMarkerActor* Table = *It;
		if (Table
			&& (Table->GetTableID().Equals(TrimmedTargetTag, ESearchCase::IgnoreCase)
				|| ContainsTagIgnoreCase(Table->GetTableTags(), TrimmedTargetTag)))
		{
			return true;
		}
	}

	return false;
}

bool UNPCAutonomyComponent::IsMaidNearSceneTargetTag(const FString& TargetTag, float AcceptanceRadius) const
{
	const FString TrimmedTargetTag = TargetTag.TrimStartAndEnd();
	UWorld* World = GetWorld();
	if (!World || !MaidNPCActor || TrimmedTargetTag.IsEmpty())
	{
		return false;
	}

	const FVector MaidLocation = MaidNPCActor->GetActorLocation();
	const float AcceptanceRadiusSq = FMath::Square(FMath::Max(AcceptanceRadius, 0.0f));
	auto IsNear = [&MaidLocation, AcceptanceRadiusSq](const FVector& TargetLocation)
	{
		return FVector::DistSquared2D(MaidLocation, TargetLocation) <= AcceptanceRadiusSq;
	};

	for (TActorIterator<AFurnitureMarkerActor> It(World); It; ++It)
	{
		const AFurnitureMarkerActor* Furniture = *It;
		if (Furniture
			&& ContainsTagIgnoreCase(Furniture->FurnitureTags, TrimmedTargetTag)
			&& IsNear(Furniture->GetNPCStandTransform().GetLocation()))
		{
			return true;
		}
	}

	for (TActorIterator<ACafeTargetPointActor> It(World); It; ++It)
	{
		const ACafeTargetPointActor* TargetPoint = *It;
		if (TargetPoint
			&& TargetPoint->bIsAvailable
			&& TargetPoint->HasTagString(TrimmedTargetTag)
			&& IsNear(TargetPoint->GetTargetTransform().GetLocation()))
		{
			return true;
		}
	}

	for (TActorIterator<AWaterablePlantActor> It(World); It; ++It)
	{
		const AWaterablePlantActor* Plant = *It;
		if (Plant
			&& Plant->IsAvailableForTag(TrimmedTargetTag)
			&& IsNear(Plant->GetPlantTargetLocation()))
		{
			return true;
		}
	}

	for (TActorIterator<ATableMarkerActor> It(World); It; ++It)
	{
		const ATableMarkerActor* Table = *It;
		if (Table
			&& (Table->GetTableID().Equals(TrimmedTargetTag, ESearchCase::IgnoreCase)
				|| ContainsTagIgnoreCase(Table->GetTableTags(), TrimmedTargetTag))
			&& IsNear(Table->GetActorLocation()))
		{
			return true;
		}
	}

	return false;
}

void UNPCAutonomyComponent::SeedPositiveValidationMemory()
{
	if (!bEnableMemoryEmotionCycle || !bSeedPositiveMemoryForValidation || !MemoryComponent)
	{
		return;
	}

	int32 SeededCount = 0;
	const auto SeedIfMissing = [this, &SeededCount](
		FName EventTag,
		const TCHAR* Summary,
		FName LocationTag,
		const TArray<FName>& CueTags,
		float Valence,
		float Arousal,
		float Importance)
	{
		const bool bAlreadySeeded = MemoryComponent->Memories.ContainsByPredicate([EventTag](const FNPCMemoryEntry& Memory)
		{
			return Memory.EventTag == EventTag;
		});
		if (bAlreadySeeded)
		{
			return;
		}

		MemoryComponent->RecordMemoryEvent(
			EventTag,
			Summary,
			LocationTag,
			CueTags,
			Valence,
			Arousal,
			Importance,
			false);
		++SeededCount;
	};

	SeedIfMissing(
		FName(TEXT("demo_customer_praise")),
		TEXT("A customer who had been having a difficult day told Sumiha that her calm attention made them feel safe and welcome."),
		FName(TEXT("dining_area")),
		TArray<FName>{ FName(TEXT("player")), FName(TEXT("customer")), FName(TEXT("served")) },
		0.90f,
		0.55f,
		0.92f);

	SeedIfMissing(
		FName(TEXT("demo_rainy_evening_conversation")),
		TEXT("On a quiet rainy evening, a regular customer stayed at the counter and thanked Sumiha for listening without rushing them."),
		FName(TEXT("counter")),
		TArray<FName>{ FName(TEXT("counter")), FName(TEXT("idle")) },
		0.72f,
		0.28f,
		0.82f);

	SeedIfMissing(
		FName(TEXT("demo_service_mistake")),
		TEXT("Sumiha once hurried and delivered an order to the wrong table; the customer's disappointed expression made her resolve to slow down and check carefully."),
		FName(TEXT("dining_area")),
		TArray<FName>{ FName(TEXT("player")), FName(TEXT("waitingfororder")) },
		-0.75f,
		0.72f,
		0.90f);

	SeedIfMissing(
		FName(TEXT("demo_revived_flower")),
		TEXT("A wilted flower recovered after several days of Sumiha's care, and seeing its new bud made her quietly proud."),
		FName(TEXT("plant_area")),
		TArray<FName>{ FName(TEXT("flower_needs_water")), FName(TEXT("idle")) },
		0.68f,
		0.40f,
		0.78f);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[MemoryEmotionTest] demo memories ready. Added=%d Total=%d"),
		SeededCount,
		MemoryComponent->Memories.Num());
}

TArray<FName> UNPCAutonomyComponent::BuildRecallCueTags() const
{
	TArray<FName> CueTags;
	if (!LastKnownNPCZone.IsEmpty())
	{
		CueTags.AddUnique(FName(*LastKnownNPCZone));
	}
	CueTags.AddUnique(FName(*GetServiceStateName().ToLower()));

	if (ServiceManager && ServiceManager->CurrentPlayerActor)
	{
		CueTags.AddUnique(FName(TEXT("player")));
		CueTags.AddUnique(FName(TEXT("customer")));
		CueTags.AddUnique(FName(*ResolvePlayerActivity().ToLower()));
	}

	for (const TPair<FName, bool>& Pair : WorldStateTags)
	{
		if (Pair.Value)
		{
			CueTags.AddUnique(Pair.Key);
		}
	}
	if (HasOutstandingTask())
	{
		CueTags.AddUnique(FName(TEXT("task")));
		CueTags.AddUnique(FName(TEXT("service")));
	}
	return CueTags;
}

float UNPCAutonomyComponent::CalculateRecallAttentionAvailability() const
{
	if (!ServiceManager || bActivityRunning || bDecisionPending || bAppraisalPending)
	{
		return 0.0f;
	}

	float Attention = 0.0f;
	switch (ServiceManager->CurrentState)
	{
	case ECafeServiceState::Idle:
		Attention = 0.85f;
		break;
	case ECafeServiceState::Served:
		Attention = 0.7f;
		break;
	case ECafeServiceState::WaitingForOrder:
		Attention = 0.5f;
		break;
	default:
		return 0.0f;
	}

	if (HasOutstandingTask())
	{
		Attention *= 0.8f;
	}
	return FMath::Clamp(Attention, 0.0f, 1.0f);
}

bool UNPCAutonomyComponent::TryBeginSpontaneousRecall(float DeltaSeconds)
{
	LastSpontaneousRecallOpportunityProbability = 0.0f;
	if (!bEnableSpontaneousRecall
		|| DeltaSeconds <= 0.0f
		|| !MemoryComponent
		|| MemoryComponent->Memories.IsEmpty()
		|| (bWaitForPlayerEncounterBeforeValidation && !bHasEncounteredPlayer))
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (!World
		|| World->GetTimeSeconds() - LastRecallAppraisalWorldTime
			< FMath::Max(MinimumSecondsBetweenRecallAppraisals, 0.0f))
	{
		return false;
	}

	const float Attention = CalculateRecallAttentionAvailability();
	if (Attention <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float BoredomFactor = FMath::Lerp(
		0.35f,
		1.0f,
		FMath::Clamp(MotiveState.Boredom / 100.0f, 0.0f, 1.0f));
	const float EnergyFactor = FMath::Lerp(
		0.5f,
		1.0f,
		FMath::Clamp(ResourceState.Energy / 100.0f, 0.0f, 1.0f));
	const bool bPostPlanRecallBoostActive = World->GetTimeSeconds() < PostPlanRecallBoostUntilWorldTime;
	const float RecallOpportunityMultiplier = bPostPlanRecallBoostActive
		? FMath::Max(PostPlanRecallOpportunityMultiplier, 1.0f)
		: 1.0f;
	const float OpportunityRatePerSecond =
		FMath::Max(BaseSpontaneousRecallOpportunitiesPerMinute, 0.0f)
		/ 60.0f
		* Attention
		* BoredomFactor
		* EnergyFactor
		* RecallOpportunityMultiplier;
	LastSpontaneousRecallOpportunityProbability = FMath::Clamp(
		1.0f - FMath::Exp(-OpportunityRatePerSecond * DeltaSeconds),
		0.0f,
		1.0f);
	const float OpportunityRoll = FMath::FRand();
	if (OpportunityRoll > LastSpontaneousRecallOpportunityProbability)
	{
		return false;
	}

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetNumberField(TEXT("attention"), Attention);
	Payload->SetNumberField(TEXT("boredom"), MotiveState.Boredom);
	Payload->SetNumberField(TEXT("energy"), ResourceState.Energy);
	Payload->SetNumberField(TEXT("opportunity_probability"), LastSpontaneousRecallOpportunityProbability);
	Payload->SetNumberField(TEXT("random_roll"), OpportunityRoll);
	Payload->SetBoolField(TEXT("post_plan_boost"), bPostPlanRecallBoostActive);
	RecordResearchEvent(TEXT("spontaneous_recall_opportunity"), Payload);
	UE_LOG(
		LogTemp,
		Verbose,
		TEXT("[MemoryEmotion] spontaneous recall opportunity opened. Probability=%.4f Roll=%.4f Attention=%.2f"),
		LastSpontaneousRecallOpportunityProbability,
		OpportunityRoll,
		Attention);
	return TryBeginMemoryAppraisal(TEXT("spontaneous_recall"));
}

const FNPCAutonomyActivityDefinition* UNPCAutonomyComponent::SelectAmbientActivity(
	FString& OutScoreSummary) const
{
	OutScoreSummary = TEXT("");
	TArray<const FNPCAutonomyActivityDefinition*> Candidates;
	TArray<float> Weights;
	TArray<FString> ScoreParts;
	float TotalWeight = 0.0f;
	const float Boredom01 = FMath::Clamp(MotiveState.Boredom / 100.0f, 0.0f, 1.0f);
	const float Energy01 = FMath::Clamp(ResourceState.Energy / 100.0f, 0.0f, 1.0f);
	const FNPCEmotionState EmotionState = EmotionComponent
		? EmotionComponent->GetCurrentState()
		: FNPCEmotionState();
	const FString PerformanceTone = ResolveCurrentPerformanceTone();
	const bool bPositiveEmotion = PerformanceTone == TEXT("content")
		|| PerformanceTone == TEXT("cheerful_activated");
	const bool bNegativeEmotion = PerformanceTone == TEXT("subdued")
		|| PerformanceTone == TEXT("tense");

	for (const FNPCAutonomyActivityDefinition& Activity : GetConfiguredActivities())
	{
		if ((Activity.Category != ENPCAutonomyActivityCategory::Leisure
				&& Activity.Category != ENPCAutonomyActivityCategory::Expression)
			|| !IsActivityAvailable(Activity))
		{
			continue;
		}

		const FString ActivityID = Activity.ActivityID.TrimStartAndEnd();
		if ((Activity.AmbientDrive == ENPCAutonomyAmbientDrive::PositiveEmotion && !bPositiveEmotion)
			|| (Activity.AmbientDrive == ENPCAutonomyAmbientDrive::NegativeEmotion && !bNegativeEmotion))
		{
			continue;
		}

		const float BaseContribution = FMath::Max(Activity.AmbientBaseWeight, 0.1f);
		const float StimulationContribution =
			Activity.StimulationGain * (0.2f + Boredom01 * 0.8f);
		float DriveContribution = 0.0f;
		switch (Activity.AmbientDrive)
		{
		case ENPCAutonomyAmbientDrive::BoredomRelief:
			DriveContribution = Boredom01 * 18.0f + Energy01 * 4.0f;
			break;
		case ENPCAutonomyAmbientDrive::EnergyRecovery:
			DriveContribution = (1.0f - Energy01) * 30.0f - Energy01 * 8.0f;
			break;
		case ENPCAutonomyAmbientDrive::Curiosity:
			DriveContribution = Boredom01 * 10.0f + Energy01 * 5.0f;
			break;
		case ENPCAutonomyAmbientDrive::PositiveEmotion:
			DriveContribution = 12.0f + EmotionState.Arousal * 6.0f;
			break;
		case ENPCAutonomyAmbientDrive::NegativeEmotion:
			DriveContribution = 12.0f + EmotionState.Arousal * 8.0f;
			break;
		default:
			DriveContribution = 3.0f;
			break;
		}

		float EnergyContribution = 0.0f;
		if (Activity.EnergyDelta > 0.0f)
		{
			EnergyContribution = Activity.EnergyDelta * (1.0f - Energy01) * 0.7f;
		}
		else if (Activity.EnergyDelta < 0.0f && Energy01 < 0.3f)
		{
			EnergyContribution = -FMath::Abs(Activity.EnergyDelta) * (0.3f - Energy01) * 8.0f;
		}

		float LocationContribution = 0.0f;
		if (!Activity.TargetSceneTag.TrimStartAndEnd().IsEmpty()
			&& Activity.TargetSceneTag.Equals(LastKnownNPCZone, ESearchCase::IgnoreCase))
		{
			LocationContribution = 2.0f;
		}

		float RecencyMultiplier = 1.0f;
		for (int32 Index = RecentActions.Num() - 1; Index >= 0; --Index)
		{
			if (!RecentActions[Index].Equals(ActivityID, ESearchCase::IgnoreCase))
			{
				continue;
			}
			const int32 RecencyRank = RecentActions.Num() - 1 - Index;
			RecencyMultiplier = RecencyRank == 0 ? 0.15f : RecencyRank == 1 ? 0.45f : 0.7f;
			break;
		}

		const float RandomContribution = FMath::FRandRange(0.0f, 2.5f);
		float Score = BaseContribution
			+ StimulationContribution
			+ DriveContribution
			+ EnergyContribution
			+ LocationContribution;
		Score = Score * RecencyMultiplier + RandomContribution;
		const float Weight = FMath::Max(Score, 0.1f);
		Candidates.Add(&Activity);
		Weights.Add(Weight);
		TotalWeight += Weight;
		ScoreParts.Add(FString::Printf(
			TEXT("%s{base=%.1f,stim=%.1f,drive=%.1f,energy=%.1f,location=%.1f,recency=%.2f,noise=%.1f,final=%.1f}"),
			*ActivityID,
			BaseContribution,
			StimulationContribution,
			DriveContribution,
			EnergyContribution,
			LocationContribution,
			RecencyMultiplier,
			RandomContribution,
			Weight));
	}

	OutScoreSummary = FString::Join(ScoreParts, TEXT(","));
	if (Candidates.IsEmpty() || TotalWeight <= KINDA_SMALL_NUMBER)
	{
		return nullptr;
	}

	float SelectionRoll = FMath::FRandRange(0.0f, TotalWeight);
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		SelectionRoll -= Weights[Index];
		if (SelectionRoll <= 0.0f)
		{
			return Candidates[Index];
		}
	}
	return Candidates.Last();
}

bool UNPCAutonomyComponent::TryStartAmbientBehavior(float DeltaSeconds)
{
	const bool bForcePhoneValidation = bForceFirstPhoneActivityForValidation
		&& !bPhoneValidationActivityConsumed;
	if (!bEnableAmbientBehaviorLayer
		|| DeltaSeconds <= 0.0f
		|| (!bForcePhoneValidation && HasOutstandingTask())
		|| ActivePlan.Status != ENPCAutonomyPlanStatus::None)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const double CurrentTime = World->GetTimeSeconds();
	const double QuietIdleSeconds = FMath::Max(CurrentTime - LastActivityEndWorldTime, 0.0);
	if (!bForcePhoneValidation
		&& QuietIdleSeconds < FMath::Max(MinimumSecondsBetweenAmbientActivities, 0.0f))
	{
		return false;
	}

	const float Attention = CalculateRecallAttentionAvailability();
	if (!bForcePhoneValidation && Attention <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float BoredomFactor = FMath::Lerp(
		0.55f,
		1.35f,
		FMath::Clamp(MotiveState.Boredom / 100.0f, 0.0f, 1.0f));
	const float EnergyFactor = FMath::Lerp(
		0.75f,
		1.0f,
		FMath::Clamp(ResourceState.Energy / 100.0f, 0.0f, 1.0f));
	const float OpportunityRatePerSecond =
		FMath::Max(AmbientOpportunitiesPerMinute, 0.0f)
		/ 60.0f
		* Attention
		* BoredomFactor
		* EnergyFactor;
	const float OpportunityProbability = FMath::Clamp(
		1.0f - FMath::Exp(-OpportunityRatePerSecond * DeltaSeconds),
		0.0f,
		1.0f);
	const float OpportunityRoll = FMath::FRand();
	const bool bForcedByQuietIdle = QuietIdleSeconds >= FMath::Max(MaximumQuietIdleSecondsBeforeAmbient, 1.0f);
	if (!bForcePhoneValidation
		&& !bForcedByQuietIdle
		&& OpportunityRoll > OpportunityProbability)
	{
		return false;
	}

	FString ScoreSummary;
	const FNPCAutonomyActivityDefinition* SelectedActivity = bForcePhoneValidation
		? FindActivityDefinition(TEXT("phone_at_counter"))
		: SelectAmbientActivity(ScoreSummary);
	if (bForcePhoneValidation)
	{
		ScoreSummary = TEXT("forced_phone_prop_validation");
	}
	if (!SelectedActivity)
	{
		return false;
	}

	if (!StartActivity(
		*SelectedActivity,
		bForcePhoneValidation ? TEXT("phone_prop_validation") : TEXT("ambient_local_policy"),
		true))
	{
		return false;
	}
	if (bForcePhoneValidation)
	{
		bPhoneValidationActivityConsumed = true;
	}

	++TotalAmbientActivitiesStarted;
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("activity"), SelectedActivity->ActivityID);
	Payload->SetNumberField(TEXT("boredom"), MotiveState.Boredom);
	Payload->SetNumberField(TEXT("energy"), ResourceState.Energy);
	Payload->SetStringField(TEXT("performance_tone"), ResolveCurrentPerformanceTone());
	Payload->SetNumberField(TEXT("attention"), Attention);
	Payload->SetNumberField(TEXT("quiet_idle_seconds"), QuietIdleSeconds);
	Payload->SetNumberField(TEXT("opportunity_probability"), OpportunityProbability);
	Payload->SetNumberField(TEXT("random_roll"), OpportunityRoll);
	Payload->SetBoolField(TEXT("forced_by_quiet_idle"), bForcedByQuietIdle);
	Payload->SetBoolField(TEXT("forced_phone_validation"), bForcePhoneValidation);
	Payload->SetStringField(TEXT("candidate_scores"), ScoreSummary);
	RecordResearchEvent(TEXT("ambient_activity_selected"), Payload);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[Ambient] selected. Activity=%s QuietIdle=%.1fs Probability=%.3f Roll=%.3f Forced=%s PhoneValidation=%s Scores=%s"),
		*SelectedActivity->ActivityID,
		QuietIdleSeconds,
		OpportunityProbability,
		OpportunityRoll,
		bForcedByQuietIdle ? TEXT("true") : TEXT("false"),
		bForcePhoneValidation ? TEXT("true") : TEXT("false"),
		*ScoreSummary);
	return true;
}

FString UNPCAutonomyComponent::BuildMemoryAppraisalContextJson(
	const FNPCMemoryEntry& Memory,
	const FNPCRecallScore& Score) const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("character"), CharacterDescription);

	TSharedPtr<FJsonObject> MemoryObject = MakeShared<FJsonObject>();
	MemoryObject->SetStringField(TEXT("event"), Memory.EventTag.ToString());
	MemoryObject->SetStringField(TEXT("summary"), Memory.Summary);
	MemoryObject->SetStringField(TEXT("location"), Memory.LocationTag.ToString());
	MemoryObject->SetNumberField(TEXT("original_valence"), Memory.Valence);
	MemoryObject->SetNumberField(TEXT("original_arousal"), Memory.Arousal);
	MemoryObject->SetNumberField(TEXT("importance"), Memory.Importance);
	MemoryObject->SetNumberField(TEXT("recall_count"), Memory.RecallCount);
	MemoryObject->SetNumberField(TEXT("recall_probability"), Score.RecallProbability);
	Root->SetObjectField(TEXT("recalled_memory"), MemoryObject);

	TSharedPtr<FJsonObject> SituationObject = MakeShared<FJsonObject>();
	SituationObject->SetStringField(TEXT("npc_location"), LastKnownNPCZone);
	SituationObject->SetStringField(TEXT("service_state"), GetServiceStateName());
	SituationObject->SetBoolField(
		TEXT("player_present"),
		ServiceManager && ServiceManager->CurrentPlayerActor != nullptr);
	SituationObject->SetStringField(TEXT("player_activity"), ResolvePlayerActivity());
	SituationObject->SetBoolField(TEXT("outstanding_task"), HasOutstandingTask());
	Root->SetObjectField(TEXT("current_situation"), SituationObject);

	if (EmotionComponent)
	{
		const FNPCEmotionState EmotionState = EmotionComponent->GetCurrentState();
		TSharedPtr<FJsonObject> EmotionObject = MakeShared<FJsonObject>();
		EmotionObject->SetStringField(TEXT("emotion"), EmotionState.EmotionLabel);
		EmotionObject->SetNumberField(TEXT("short_term_valence"), EmotionState.ShortTermValence);
		EmotionObject->SetNumberField(TEXT("arousal"), EmotionState.Arousal);
		EmotionObject->SetNumberField(TEXT("mood_valence"), EmotionState.MoodValence);
		Root->SetObjectField(TEXT("current_emotion"), EmotionObject);
	}

	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Json;
}

bool UNPCAutonomyComponent::TryBeginMemoryAppraisal(const FString& PlanningTrigger)
{
	if (!bEnableMemoryEmotionCycle
		|| !MemoryComponent
		|| !EmotionComponent
		|| !AutonomyLLMClient
		|| MemoryComponent->Memories.IsEmpty()
		|| (bWaitForPlayerEncounterBeforeValidation && !bHasEncounteredPlayer))
	{
		return false;
	}
	const bool bForceRecall = bForceFirstValidationRecall && !bValidationRecallConsumed;
	if (!bForceRecall)
	{
		const UWorld* World = GetWorld();
		if (World
			&& World->GetTimeSeconds() - LastRecallAppraisalWorldTime
				< FMath::Max(MinimumSecondsBetweenRecallAppraisals, 0.0f))
		{
			return false;
		}
	}

	const float AttentionAvailability = CalculateRecallAttentionAvailability();
	if (AttentionAvailability <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	FNPCMemoryEntry RecalledMemory;
	FNPCRecallScore RecallScore;
	if (!MemoryComponent->TryRecallBestMemory(
		BuildRecallCueTags(),
		AttentionAvailability,
		bForceRecall,
		RecalledMemory,
		RecallScore))
	{
		return false;
	}
	if (bForceRecall)
	{
		bValidationRecallConsumed = true;
	}

	AutonomyLLMClient->OpenAIModelName = AutonomyModelName;
	PendingAppraisalMemory = RecalledMemory;
	PendingRecallScore = RecallScore;
	PendingPlanningTriggerAfterAppraisal = PlanningTrigger;
	const FString AppraisalContextJson = BuildMemoryAppraisalContextJson(RecalledMemory, RecallScore);
	PendingAppraisalRequestId = AutonomyLLMClient->SendMemoryAppraisalMessage(AppraisalContextJson);
	if (PendingAppraisalRequestId <= 0)
	{
		PendingPlanningTriggerAfterAppraisal = TEXT("");
		return false;
	}

	bAppraisalPending = true;
	bMeaningfulContextChangePending = false;
	PendingDecisionTrigger = TEXT("");
	NextDecisionEarliestWorldTime = TNumericLimits<double>::Max();
	++TotalAppraisalRequests;
	if (const UWorld* World = GetWorld())
	{
		LastRecallAppraisalWorldTime = World->GetTimeSeconds();
	}
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("trigger"), PlanningTrigger);
	Payload->SetStringField(TEXT("memory_id"), RecalledMemory.MemoryID);
	Payload->SetStringField(TEXT("memory_event"), RecalledMemory.EventTag.ToString());
	Payload->SetNumberField(TEXT("recall_probability"), RecallScore.RecallProbability);
	Payload->SetNumberField(TEXT("attention"), RecallScore.AttentionAvailability);
	Payload->SetStringField(TEXT("context_json"), AppraisalContextJson);
	RecordResearchEvent(TEXT("appraisal_requested"), Payload);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[MemoryEmotion] appraisal started. Trigger=%s Memory=%s Probability=%.3f Attention=%.3f"),
		*PlanningTrigger,
		*RecalledMemory.EventTag.ToString(),
		RecallScore.RecallProbability,
		RecallScore.AttentionAvailability);
	return true;
}

bool UNPCAutonomyComponent::TryStartVisibleMemoryReaction()
{
	const FNPCEmotionState EmotionState = EmotionComponent
		? EmotionComponent->GetCurrentState()
		: FNPCEmotionState();
	const FString NormalizedIntent = CurrentExpressionIntent.TrimStartAndEnd().ToLower();
	const bool bStrongNegativeReaction = EmotionState.ShortTermValence <= -0.15f
		&& EmotionState.Arousal >= HighArousalNegativeReactionThreshold;
	const bool bStrongAlertReaction = NormalizedIntent == TEXT("alert")
		&& EmotionState.Arousal >= HighArousalNegativeReactionThreshold;
	const bool bShouldBlockPlanning = bPlayVisibleMemoryReaction
		&& bAllowBlockingStrongMemoryReactions
		&& (bStrongNegativeReaction || bStrongAlertReaction);
	const bool bUseConcurrentOverlay = bEnableConcurrentExpressionOverlay && !bShouldBlockPlanning;
	const float OverlayLeadDuration = bUseConcurrentOverlay
		? FMath::Max(ConcurrentExpressionLeadDurationSeconds, 0.1f)
		: FMath::Max(ExpressionMorphDurationSeconds, 0.1f);

	ApplyExpressionMorphs(CurrentExpressionIntent, false, OverlayLeadDuration);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			170042,
			FMath::Max(OverlayLeadDuration, 3.0f),
			FColor::Emerald,
			FString::Printf(
				TEXT("MEMORY EXPRESSION | %s | %s | %s"),
				*CurrentEmotionLabel,
				*CurrentExpressionIntent,
				bShouldBlockPlanning ? TEXT("blocking") : TEXT("concurrent")));
	}

	TSharedRef<FJsonObject> PerformancePayload = MakeShared<FJsonObject>();
	PerformancePayload->SetStringField(TEXT("emotion"), CurrentEmotionLabel);
	PerformancePayload->SetStringField(TEXT("expression_intent"), CurrentExpressionIntent);
	PerformancePayload->SetBoolField(TEXT("blocking"), bShouldBlockPlanning);
	PerformancePayload->SetNumberField(TEXT("valence"), EmotionState.ShortTermValence);
	PerformancePayload->SetNumberField(TEXT("arousal"), EmotionState.Arousal);
	PerformancePayload->SetNumberField(TEXT("lead_duration_seconds"), OverlayLeadDuration);
	RecordResearchEvent(TEXT("expression_overlay_started"), PerformancePayload);

	if (bUseConcurrentOverlay)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[ExpressionOverlay] non-blocking expression started. Emotion=%s Intent=%s LeadDuration=%.1fs"),
			*CurrentEmotionLabel,
			*CurrentExpressionIntent,
			OverlayLeadDuration);
		return false;
	}

	if (!bPlayVisibleMemoryReaction || !ActionQueueComponent || !CanRunAutonomy())
	{
		return false;
	}

	TSoftObjectPtr<UAnimSequenceBase>* AnimationAsset = &NeutralMemoryReactionAnimation;
	if (EmotionState.ShortTermValence >= 0.2f)
	{
		AnimationAsset = &PositiveMemoryReactionAnimation;
	}
	else if (EmotionState.ShortTermValence <= -0.15f)
	{
		AnimationAsset = EmotionState.Arousal >= HighArousalNegativeReactionThreshold
			? &HighArousalNegativeMemoryReactionAnimation
			: &NegativeMemoryReactionAnimation;
	}

	UAnimSequenceBase* ReactionAnimation = AnimationAsset->LoadSynchronous();
	if (!ReactionAnimation)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MemoryEmotion] visible reaction animation could not be loaded."));
		return false;
	}
	if (!IsAnimationCompatibleWithMaid(ReactionAnimation))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[MemoryEmotion] skipped incompatible visible reaction animation. Animation=%s Skeleton=%s"),
			*ReactionAnimation->GetName(),
			ReactionAnimation->GetSkeleton() ? *ReactionAnimation->GetSkeleton()->GetName() : TEXT("none"));
		return false;
	}

	CurrentActivityID = FString::Printf(TEXT("memory_reaction_%s"), *CurrentExpressionIntent);
	bActivityRunning = true;
	bCurrentActivityIsAmbient = false;
	RuntimePhase = ENPCAutonomyRuntimePhase::EmotionReaction;
	ActiveActivityAnimation = ReactionAnimation;
	ActionQueueComponent->ClearActionQueue();
	ActionQueueComponent->EnqueueAnimation(ReactionAnimation, FName(TEXT("DefaultSlot")), 1.0f, 4.0f);
	ActionQueueComponent->StartActionQueue();
	if (!ActionQueueComponent->bIsRunningQueue)
	{
		if (RuntimePhase != ENPCAutonomyRuntimePhase::EmotionReaction)
		{
			return true;
		}
		bActivityRunning = false;
		bCurrentActivityIsAmbient = false;
		CurrentActivityID = TEXT("");
		RuntimePhase = ENPCAutonomyRuntimePhase::None;
		ActiveActivityAnimation = nullptr;
		return false;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[MemoryEmotion] visible reaction started. Animation=%s Emotion=%s Expression=%s"),
		*ReactionAnimation->GetName(),
		*CurrentEmotionLabel,
		*CurrentExpressionIntent);
	return true;
}

bool UNPCAutonomyComponent::IsAnimationCompatibleWithMaid(const UAnimSequenceBase* Animation) const
{
	if (!Animation || !MaidNPCActor)
	{
		return false;
	}

	const USkeleton* AnimationSkeleton = Animation->GetSkeleton();
	TArray<USkeletalMeshComponent*> MeshComponents;
	MaidNPCActor->GetComponents<USkeletalMeshComponent>(MeshComponents);
	for (const USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		const USkeletalMesh* SkeletalMesh = MeshComponent ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
		if (MeshComponent
			&& MeshComponent->GetAnimInstance()
			&& SkeletalMesh
			&& (!AnimationSkeleton || SkeletalMesh->GetSkeleton() == AnimationSkeleton))
		{
			return true;
		}
	}
	return false;
}

void UNPCAutonomyComponent::ContinuePlanningAfterAppraisal()
{
	const FString OriginalTrigger = PendingPlanningTriggerAfterAppraisal;
	PendingPlanningTriggerAfterAppraisal = TEXT("");
	if (OriginalTrigger.IsEmpty())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[MemoryEmotion] duplicate planning continuation ignored."));
		return;
	}
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[MemoryEmotion] appraisal performance ready; continuing to planning. OriginalTrigger=%s"),
		*OriginalTrigger);
	MarkMeaningfulContextChange(FString::Printf(
		TEXT("memory_appraised:%s"),
		*PendingAppraisalMemory.EventTag.ToString()));
	ScheduleNextDecision(0.0f);
}

void UNPCAutonomyComponent::SchedulePlanSurfaceThought(
	const FString& SurfaceThought,
	float DelaySeconds)
{
	if (!bShowPlanSurfaceThoughts)
	{
		return;
	}

	FString CleanThought = SurfaceThought.TrimStartAndEnd();
	CleanThought.ReplaceInline(TEXT("\r"), TEXT(" "));
	CleanThought.ReplaceInline(TEXT("\n"), TEXT(" "));
	const bool bWrappedInFullWidthParens = CleanThought.Len() >= 2
		&& CleanThought.StartsWith(TEXT("（"))
		&& CleanThought.EndsWith(TEXT("）"));
	const bool bWrappedInAsciiParens = CleanThought.Len() >= 2
		&& CleanThought.StartsWith(TEXT("("))
		&& CleanThought.EndsWith(TEXT(")"));
	if (bWrappedInFullWidthParens || bWrappedInAsciiParens)
	{
		CleanThought = CleanThought.Mid(1, CleanThought.Len() - 2).TrimStartAndEnd();
	}
	if (CleanThought.IsEmpty())
	{
		return;
	}

	PendingPlanSurfaceThought = CleanThought.Left(160);
	UWorld* World = GetWorld();
	if (!World || DelaySeconds <= KINDA_SMALL_NUMBER)
	{
		DisplayPendingPlanSurfaceThought();
		return;
	}

	World->GetTimerManager().ClearTimer(PlanThoughtDisplayTimerHandle);
	World->GetTimerManager().SetTimer(
		PlanThoughtDisplayTimerHandle,
		this,
		&UNPCAutonomyComponent::DisplayPendingPlanSurfaceThought,
		FMath::Max(DelaySeconds, 0.1f),
		false);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[PlanThought] scheduled. Delay=%.1fs Text=%s"),
		DelaySeconds,
		*PendingPlanSurfaceThought);
}

void UNPCAutonomyComponent::DisplayPendingPlanSurfaceThought()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlanThoughtDisplayTimerHandle);
	}
	const FString SurfaceThought = PendingPlanSurfaceThought.TrimStartAndEnd();
	PendingPlanSurfaceThought = TEXT("");
	if (SurfaceThought.IsEmpty() || ActivePlan.Status == ENPCAutonomyPlanStatus::None)
	{
		return;
	}

	bool bDisplayed = false;
	if (ULLMClient* DisplayClient = ULLMClient::GetMostRecentInteractiveClient())
	{
		DisplayClient->ShowSilentDisplayText(
			FString::Printf(TEXT("（心の声：%s）"), *SurfaceThought));
		bDisplayed = true;
	}

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("goal"), ActivePlan.Goal);
	Payload->SetStringField(TEXT("performance_tone"), ResolveCurrentPerformanceTone());
	Payload->SetStringField(TEXT("surface_thought"), SurfaceThought);
	Payload->SetBoolField(TEXT("displayed"), bDisplayed);
	RecordResearchEvent(TEXT("plan_surface_thought_presented"), Payload);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[PlanThought] presented=%s Tone=%s Text=%s"),
		bDisplayed ? TEXT("true") : TEXT("false"),
		*ResolveCurrentPerformanceTone(),
		*SurfaceThought);
}

FString UNPCAutonomyComponent::BuildFallbackPlanSurfaceThought(const TArray<FString>& Steps) const
{
	if (Steps.IsEmpty())
	{
		return TEXT("");
	}

	const FString FirstStep = Steps[0].TrimStartAndEnd();
	if (FirstStep.Contains(TEXT("table"), ESearchCase::IgnoreCase)
		|| FirstStep.Contains(TEXT("dining"), ESearchCase::IgnoreCase))
	{
		return TEXT("まずはテーブルまわりをきれいにしておこう。");
	}
	if (FirstStep.Contains(TEXT("flower"), ESearchCase::IgnoreCase)
		|| FirstStep.Contains(TEXT("plant"), ESearchCase::IgnoreCase))
	{
		return TEXT("今のうちに花の様子を見ておこう。");
	}

	const FNPCAutonomyActivityDefinition* Activity = FindActivityDefinition(FirstStep);
	if (Activity && Activity->Category == ENPCAutonomyActivityCategory::Task)
	{
		return TEXT("まずは今必要な仕事を片づけよう。");
	}
	if (Activity && Activity->Category == ENPCAutonomyActivityCategory::Preference)
	{
		return TEXT("今のうちに店内の様子を整えておこう。");
	}
	return TEXT("少し気分転換をしようかな。");
}

void UNPCAutonomyComponent::ApplyExpressionMorphs(
	const FString& ExpressionIntent,
	bool bHoldUntilPlanEnds,
	float DurationSecondsOverride)
{
	if (!MaidNPCActor || ExpressionMorphStrength <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ExpressionMorphResetTimerHandle);
	}

	const FString NormalizedIntent = ExpressionIntent.TrimStartAndEnd().ToLower();
	TMap<FName, float> PresetWeights;
	if (NormalizedIntent == TEXT("smile"))
	{
		PresetWeights.Add(FName(TEXT("Joy")), 0.9f);
		PresetWeights.Add(FName(TEXT("笑眉毛")), 0.2f);
		PresetWeights.Add(FName(TEXT("Fcl_ALL_Joy")), 0.9f);
	}
	else if (NormalizedIntent == TEXT("hum"))
	{
		PresetWeights.Add(FName(TEXT("Fun")), 0.75f);
		PresetWeights.Add(FName(TEXT("Joy")), 0.25f);
		PresetWeights.Add(FName(TEXT("笑眉毛")), 0.2f);
		PresetWeights.Add(FName(TEXT("Fcl_ALL_Fun")), 0.75f);
		PresetWeights.Add(FName(TEXT("Fcl_ALL_Joy")), 0.25f);
	}
	else if (NormalizedIntent == TEXT("sigh"))
	{
		PresetWeights.Add(FName(TEXT("Sorrow")), 0.85f);
		PresetWeights.Add(FName(TEXT("倒八字眉毛")), 0.2f);
		PresetWeights.Add(FName(TEXT("Fcl_ALL_Sorrow")), 0.85f);
	}
	else if (NormalizedIntent == TEXT("pensive"))
	{
		// Keep this visibly distinct from sadness: the gaze shifts up and aside,
		// the mouth tightens, and only one eye narrows slightly in concentration.
		PresetWeights.Add(FName(TEXT("look_up")), 0.65f);
		PresetWeights.Add(FName(TEXT("look_right")), 0.45f);
		PresetWeights.Add(FName(TEXT("困惑闭嘴")), 0.7f);
		PresetWeights.Add(FName(TEXT("困惑闭眼R")), 0.22f);
		PresetWeights.Add(FName(TEXT("Fcl_MTH_Close")), 0.25f);
		PresetWeights.Add(FName(TEXT("Fcl_EYE_Joy_R")), 0.12f);
		PresetWeights.Add(FName(TEXT("Fcl_BRW_Sorrow")), 0.1f);
	}
	else if (NormalizedIntent == TEXT("angry"))
	{
		PresetWeights.Add(FName(TEXT("Angry")), 0.85f);
		PresetWeights.Add(FName(TEXT("怒眉毛")), 0.25f);
		PresetWeights.Add(FName(TEXT("Fcl_ALL_Angry")), 0.85f);
	}
	else if (NormalizedIntent == TEXT("alert"))
	{
		PresetWeights.Add(FName(TEXT("look_up")), 0.45f);
		PresetWeights.Add(FName(TEXT("怒眉毛")), 0.12f);
		PresetWeights.Add(FName(TEXT("Fcl_ALL_Surprised")), 0.25f);
	}
	else if (NormalizedIntent == TEXT("startled"))
	{
		PresetWeights.Add(FName(TEXT("look_up")), 0.85f);
		PresetWeights.Add(FName(TEXT("困惑闭嘴")), 0.8f);
		PresetWeights.Add(FName(TEXT("Fcl_ALL_Surprised")), 0.9f);
	}
	else if (NormalizedIntent == TEXT("blush"))
	{
		PresetWeights.Add(FName(TEXT("脸红")), 0.85f);
		PresetWeights.Add(FName(TEXT("Joy")), 0.18f);
		PresetWeights.Add(FName(TEXT("look_down")), 0.22f);
		PresetWeights.Add(FName(TEXT("Fcl_ALL_Joy")), 0.18f);
	}
	else if (NormalizedIntent == TEXT("startled_blush"))
	{
		PresetWeights.Add(FName(TEXT("脸红")), 0.9f);
		PresetWeights.Add(FName(TEXT("look_up")), 0.55f);
		PresetWeights.Add(FName(TEXT("困惑闭嘴")), 0.3f);
		PresetWeights.Add(FName(TEXT("Fcl_ALL_Surprised")), 0.75f);
		PresetWeights.Add(FName(TEXT("Fcl_ALL_Joy")), 0.12f);
	}
	else
	{
		return;
	}

	TMap<FName, float> VerifiedTargetWeights;
	TArray<USkeletalMeshComponent*> MeshComponents;
	MaidNPCActor->GetComponents<USkeletalMeshComponent>(MeshComponents);
	for (const TPair<FName, float>& PresetWeight : PresetWeights)
	{
		bool bMorphFound = false;
		for (const USkeletalMeshComponent* MeshComponent : MeshComponents)
		{
			const USkeletalMesh* SkeletalMesh = MeshComponent ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
			if (SkeletalMesh && SkeletalMesh->FindMorphTarget(PresetWeight.Key))
			{
				bMorphFound = true;
				break;
			}
		}
		if (bMorphFound)
		{
			VerifiedTargetWeights.Add(
				PresetWeight.Key,
				FMath::Clamp(PresetWeight.Value * ExpressionMorphStrength, 0.0f, 1.0f));
		}
	}

	bExpressionOverlayActive = !VerifiedTargetWeights.IsEmpty();
	bExpressionOverlayHeldByPlan = bExpressionOverlayActive && bHoldUntilPlanEnds;
	if (!bExpressionOverlayActive)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ExpressionOverlay] no compatible morph target found. Intent=%s Actor=%s"),
			*NormalizedIntent,
			*MaidNPCActor->GetName());
		return;
	}
	StartExpressionMorphBlend(VerifiedTargetWeights, ExpressionFadeInDurationSeconds);
	TArray<FString> AppliedTargetDescriptions;
	for (const TPair<FName, float>& TargetWeight : VerifiedTargetWeights)
	{
		AppliedTargetDescriptions.Add(FString::Printf(TEXT("%s=%.2f"), *TargetWeight.Key.ToString(), TargetWeight.Value));
	}

	const float AppliedDuration = DurationSecondsOverride > 0.0f
		? DurationSecondsOverride
		: ExpressionMorphDurationSeconds;
	if (!bExpressionOverlayHeldByPlan)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				ExpressionMorphResetTimerHandle,
				this,
				&UNPCAutonomyComponent::ResetExpressionMorphs,
				FMath::Max(AppliedDuration, 0.1f),
				false);
		}
	}
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[ExpressionOverlay] preset blending in. Intent=%s Targets=%d Weights=[%s] Strength=%.2f FadeIn=%.2fs Duration=%s"),
		*NormalizedIntent,
		VerifiedTargetWeights.Num(),
		*FString::Join(AppliedTargetDescriptions, TEXT(", ")),
		ExpressionMorphStrength,
		ExpressionFadeInDurationSeconds,
		bExpressionOverlayHeldByPlan
			? TEXT("until_plan_ends")
			: *FString::Printf(TEXT("%.1fs"), AppliedDuration));
}

void UNPCAutonomyComponent::ResetExpressionMorphs()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ExpressionMorphResetTimerHandle);
	}
	bExpressionOverlayActive = false;
	bExpressionOverlayHeldByPlan = false;
	if (CurrentExpressionMorphWeights.IsEmpty() && ExpressionBlendTargetWeights.IsEmpty())
	{
		ClearExpressionMorphsImmediately();
		return;
	}

	StartExpressionMorphBlend(TMap<FName, float>(), ExpressionFadeOutDurationSeconds);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[ExpressionOverlay] preset fading out. Duration=%.2fs"),
		ExpressionFadeOutDurationSeconds);
}

void UNPCAutonomyComponent::StartExpressionMorphBlend(
	const TMap<FName, float>& TargetWeights,
	float BlendDurationSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !MaidNPCActor)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(ExpressionMorphBlendTimerHandle);
	ExpressionBlendStartWeights = CurrentExpressionMorphWeights;
	ExpressionBlendTargetWeights = TargetWeights;
	ExpressionBlendStartWorldTime = World->GetTimeSeconds();
	ExpressionBlendDurationSeconds = FMath::Max(BlendDurationSeconds, 0.0f);
	for (const TPair<FName, float>& Pair : ExpressionBlendStartWeights)
	{
		AppliedExpressionMorphs.AddUnique(Pair.Key);
	}
	for (const TPair<FName, float>& Pair : ExpressionBlendTargetWeights)
	{
		AppliedExpressionMorphs.AddUnique(Pair.Key);
	}

	if (ExpressionBlendDurationSeconds <= KINDA_SMALL_NUMBER)
	{
		UpdateExpressionMorphBlend();
		return;
	}

	World->GetTimerManager().SetTimer(
		ExpressionMorphBlendTimerHandle,
		this,
		&UNPCAutonomyComponent::UpdateExpressionMorphBlend,
		1.0f / 60.0f,
		true);
	UpdateExpressionMorphBlend();
}

void UNPCAutonomyComponent::UpdateExpressionMorphBlend()
{
	UWorld* World = GetWorld();
	if (!World || !MaidNPCActor)
	{
		return;
	}

	const float LinearAlpha = ExpressionBlendDurationSeconds <= KINDA_SMALL_NUMBER
		? 1.0f
		: FMath::Clamp(
			static_cast<float>((World->GetTimeSeconds() - ExpressionBlendStartWorldTime) / ExpressionBlendDurationSeconds),
			0.0f,
			1.0f);
	const float SmoothAlpha = LinearAlpha * LinearAlpha * (3.0f - 2.0f * LinearAlpha);
	TSet<FName> MorphNames;
	for (const TPair<FName, float>& Pair : ExpressionBlendStartWeights)
	{
		MorphNames.Add(Pair.Key);
	}
	for (const TPair<FName, float>& Pair : ExpressionBlendTargetWeights)
	{
		MorphNames.Add(Pair.Key);
	}

	CurrentExpressionMorphWeights.Reset();
	for (const FName MorphName : MorphNames)
	{
		const float StartWeight = ExpressionBlendStartWeights.FindRef(MorphName);
		const float TargetWeight = ExpressionBlendTargetWeights.FindRef(MorphName);
		const float CurrentWeight = FMath::Lerp(StartWeight, TargetWeight, SmoothAlpha);
		SetExpressionMorphWeight(MorphName, CurrentWeight);
		if (CurrentWeight > KINDA_SMALL_NUMBER)
		{
			CurrentExpressionMorphWeights.Add(MorphName, CurrentWeight);
		}
	}

	if (LinearAlpha >= 1.0f)
	{
		World->GetTimerManager().ClearTimer(ExpressionMorphBlendTimerHandle);
		CurrentExpressionMorphWeights = ExpressionBlendTargetWeights;
		AppliedExpressionMorphs.Reset();
		for (const TPair<FName, float>& Pair : CurrentExpressionMorphWeights)
		{
			if (Pair.Value > KINDA_SMALL_NUMBER)
			{
				AppliedExpressionMorphs.Add(Pair.Key);
			}
		}
		ExpressionBlendStartWeights.Reset();
		ExpressionBlendTargetWeights.Reset();
	}
}

void UNPCAutonomyComponent::SetExpressionMorphWeight(FName MorphName, float Weight)
{
	if (!MaidNPCActor || MorphName.IsNone())
	{
		return;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	MaidNPCActor->GetComponents<USkeletalMeshComponent>(MeshComponents);
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		USkeletalMesh* SkeletalMesh = MeshComponent ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
		if (MeshComponent && SkeletalMesh && SkeletalMesh->FindMorphTarget(MorphName))
		{
			MeshComponent->SetMorphTarget(MorphName, FMath::Clamp(Weight, 0.0f, 1.0f), false);
		}
	}
}

void UNPCAutonomyComponent::ClearExpressionMorphsImmediately()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ExpressionMorphResetTimerHandle);
		World->GetTimerManager().ClearTimer(ExpressionMorphBlendTimerHandle);
	}

	TSet<FName> MorphNames;
	for (const FName MorphName : AppliedExpressionMorphs)
	{
		MorphNames.Add(MorphName);
	}
	for (const TPair<FName, float>& Pair : CurrentExpressionMorphWeights)
	{
		MorphNames.Add(Pair.Key);
	}
	for (const TPair<FName, float>& Pair : ExpressionBlendStartWeights)
	{
		MorphNames.Add(Pair.Key);
	}
	for (const TPair<FName, float>& Pair : ExpressionBlendTargetWeights)
	{
		MorphNames.Add(Pair.Key);
	}
	for (const FName MorphName : MorphNames)
	{
		SetExpressionMorphWeight(MorphName, 0.0f);
	}

	AppliedExpressionMorphs.Reset();
	CurrentExpressionMorphWeights.Reset();
	ExpressionBlendStartWeights.Reset();
	ExpressionBlendTargetWeights.Reset();
	bExpressionOverlayActive = false;
	bExpressionOverlayHeldByPlan = false;
}

void UNPCAutonomyComponent::InitializeAutomaticBlinking()
{
	BlinkMorphNames.Reset();
	if (!bEnableAutomaticBlinking || !MaidNPCActor)
	{
		return;
	}

	TArray<FName> BilateralCandidates;
	FName UnifiedBlinkMorph = NAME_None;
	TArray<FString> AvailableMorphNames;
	TArray<USkeletalMeshComponent*> MeshComponents;
	MaidNPCActor->GetComponents<USkeletalMeshComponent>(MeshComponents);
	for (const USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		const USkeletalMesh* SkeletalMesh = MeshComponent ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
		if (!SkeletalMesh)
		{
			continue;
		}

		for (const TObjectPtr<UMorphTarget>& MorphTarget : SkeletalMesh->GetMorphTargets())
		{
			if (!MorphTarget)
			{
				continue;
			}
			const FName MorphName = MorphTarget->GetFName();
			const FString LowerName = MorphName.ToString().ToLower();
			AvailableMorphNames.AddUnique(MorphName.ToString());
			if (LowerName == TEXT("blink")
				|| LowerName == TEXT("eyeclose")
				|| LowerName == TEXT("eye_close")
				|| LowerName == TEXT("fcl_eye_close")
				|| LowerName == TEXT("闭眼lr"))
			{
				UnifiedBlinkMorph = MorphName;
			}
			else if (LowerName == TEXT("闭眼l")
				|| LowerName == TEXT("闭眼r")
				|| LowerName.Contains(TEXT("blink"))
				|| LowerName.Contains(TEXT("eyeclose"))
				|| LowerName.Contains(TEXT("eye_close")))
			{
				BilateralCandidates.AddUnique(MorphName);
			}
		}
	}

	if (!UnifiedBlinkMorph.IsNone())
	{
		BlinkMorphNames.Add(UnifiedBlinkMorph);
	}
	else
	{
		BlinkMorphNames = MoveTemp(BilateralCandidates);
	}

	if (BlinkMorphNames.IsEmpty())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[AutoBlink] disabled because no compatible blink morph was found. Available=%s"),
			*FString::Join(AvailableMorphNames, TEXT(",")));
		return;
	}

	TArray<FString> ResolvedBlinkNames;
	for (const FName MorphName : BlinkMorphNames)
	{
		ResolvedBlinkNames.Add(MorphName.ToString());
	}
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[AutoBlink] initialized. Morphs=%s Interval=%.1f-%.1fs ClosedDuration=%.2fs DoubleChance=%.2f"),
		*FString::Join(ResolvedBlinkNames, TEXT(",")),
		BlinkIntervalMinSeconds,
		BlinkIntervalMaxSeconds,
		BlinkClosedDurationSeconds,
		DoubleBlinkChance);
	ScheduleNextAutomaticBlink();
}

void UNPCAutonomyComponent::ScheduleNextAutomaticBlink(float DelayOverrideSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !bEnableAutomaticBlinking || BlinkMorphNames.IsEmpty())
	{
		return;
	}

	const float MinimumInterval = FMath::Max(BlinkIntervalMinSeconds, 0.1f);
	const float MaximumInterval = FMath::Max(BlinkIntervalMaxSeconds, MinimumInterval);
	const float Delay = DelayOverrideSeconds >= 0.0f
		? FMath::Max(DelayOverrideSeconds, 0.02f)
		: FMath::FRandRange(MinimumInterval, MaximumInterval);
	World->GetTimerManager().SetTimer(
		BlinkStartTimerHandle,
		this,
		&UNPCAutonomyComponent::StartAutomaticBlink,
		Delay,
		false);
}

void UNPCAutonomyComponent::StartAutomaticBlink()
{
	if (!bEnableAutomaticBlinking || BlinkMorphNames.IsEmpty())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BlinkStartTimerHandle);
	}
	SetBlinkMorphWeight(FMath::Clamp(BlinkStrength, 0.0f, 1.0f));
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			BlinkFinishTimerHandle,
			this,
			&UNPCAutonomyComponent::FinishAutomaticBlink,
			FMath::Max(BlinkClosedDurationSeconds, 0.02f),
			false);
	}
}

void UNPCAutonomyComponent::FinishAutomaticBlink()
{
	SetBlinkMorphWeight(0.0f);
	if (!bBlinkFollowUpPending && FMath::FRand() < FMath::Clamp(DoubleBlinkChance, 0.0f, 1.0f))
	{
		bBlinkFollowUpPending = true;
		ScheduleNextAutomaticBlink(0.16f);
		return;
	}

	bBlinkFollowUpPending = false;
	ScheduleNextAutomaticBlink();
}

void UNPCAutonomyComponent::SetBlinkMorphWeight(float Weight)
{
	if (!MaidNPCActor || BlinkMorphNames.IsEmpty())
	{
		return;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	MaidNPCActor->GetComponents<USkeletalMeshComponent>(MeshComponents);
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		USkeletalMesh* SkeletalMesh = MeshComponent ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
		if (!MeshComponent || !SkeletalMesh)
		{
			continue;
		}
		for (const FName MorphName : BlinkMorphNames)
		{
			if (SkeletalMesh->FindMorphTarget(MorphName))
			{
				MeshComponent->SetMorphTarget(MorphName, Weight, false);
			}
		}
	}
}

FString UNPCAutonomyComponent::BuildDecisionContextJson(const TArray<FString>& AvailableActivityIDs) const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> TriggerObject = MakeShared<FJsonObject>();
	TriggerObject->SetStringField(TEXT("type"), LastDecisionTrigger);
	Root->SetObjectField(TEXT("trigger"), TriggerObject);

	TSharedPtr<FJsonObject> NPCObject = MakeShared<FJsonObject>();
	NPCObject->SetStringField(TEXT("state"), TEXT("idle"));
	NPCObject->SetStringField(TEXT("location"), LastKnownNPCZone);
	NPCObject->SetStringField(TEXT("service_state"), GetServiceStateName());
	TSharedPtr<FJsonObject> MotiveObject = MakeShared<FJsonObject>();
	MotiveObject->SetNumberField(TEXT("boredom"), MotiveState.Boredom);
	MotiveObject->SetNumberField(TEXT("duty"), MotiveState.Duty);
	MotiveObject->SetNumberField(TEXT("social_attention"), MotiveState.SocialAttention);
	NPCObject->SetObjectField(TEXT("motives"), MotiveObject);
	TSharedPtr<FJsonObject> ResourceObject = MakeShared<FJsonObject>();
	ResourceObject->SetNumberField(TEXT("energy"), ResourceState.Energy);
	NPCObject->SetObjectField(TEXT("resources"), ResourceObject);
	if (EmotionComponent)
	{
		const FNPCEmotionState EmotionState = EmotionComponent->GetCurrentState();
		TSharedPtr<FJsonObject> EmotionObject = MakeShared<FJsonObject>();
		EmotionObject->SetStringField(TEXT("label"), EmotionState.EmotionLabel);
		EmotionObject->SetNumberField(TEXT("short_term_valence"), EmotionState.ShortTermValence);
		EmotionObject->SetNumberField(TEXT("arousal"), EmotionState.Arousal);
		EmotionObject->SetNumberField(TEXT("mood_valence"), EmotionState.MoodValence);
		EmotionObject->SetStringField(TEXT("expression_intent"), EmotionState.ExpressionIntent);
		NPCObject->SetObjectField(TEXT("emotion"), EmotionObject);
	}
	NPCObject->SetStringField(TEXT("performance_tone"), ResolveCurrentPerformanceTone());
	TArray<TSharedPtr<FJsonValue>> RecentActionValues;
	for (const FString& RecentAction : RecentActions)
	{
		RecentActionValues.Add(MakeShared<FJsonValueString>(RecentAction));
	}
	NPCObject->SetArrayField(TEXT("recent_actions"), RecentActionValues);
	Root->SetObjectField(TEXT("npc"), NPCObject);

	if (!LastAppraisedMemorySummary.TrimStartAndEnd().IsEmpty())
	{
		TSharedPtr<FJsonObject> MemoryObject = MakeShared<FJsonObject>();
		MemoryObject->SetStringField(TEXT("event"), PendingAppraisalMemory.EventTag.ToString());
		MemoryObject->SetStringField(TEXT("summary"), LastAppraisedMemorySummary);
		MemoryObject->SetStringField(TEXT("interpretation"), LastAppraisalInterpretation);
		MemoryObject->SetNumberField(TEXT("recall_probability"), PendingRecallScore.RecallProbability);
		Root->SetObjectField(TEXT("recently_appraised_memory"), MemoryObject);
	}

	const bool bPlayerPresent = ServiceManager && ServiceManager->CurrentPlayerActor != nullptr;
	TSharedPtr<FJsonObject> PlayerObject = MakeShared<FJsonObject>();
	PlayerObject->SetBoolField(TEXT("present"), bPlayerPresent);
	PlayerObject->SetStringField(TEXT("activity"), ResolvePlayerActivity());
	PlayerObject->SetStringField(TEXT("zone"), ResolvePlayerZone());
	PlayerObject->SetStringField(
		TEXT("attention"),
		PlayerContext.AttentionTarget.TrimStartAndEnd().IsEmpty() ? TEXT("unknown") : PlayerContext.AttentionTarget);
	PlayerObject->SetStringField(
		TEXT("emotion"),
		PlayerContext.Emotion.TrimStartAndEnd().IsEmpty() ? TEXT("unknown") : PlayerContext.Emotion);
	TArray<TSharedPtr<FJsonValue>> RecentEventValues;
	for (const FString& RecentEvent : PlayerContext.RecentEvents)
	{
		const FString TrimmedEvent = RecentEvent.TrimStartAndEnd();
		if (!TrimmedEvent.IsEmpty())
		{
			RecentEventValues.Add(MakeShared<FJsonValueString>(TrimmedEvent));
		}
	}
	if (RecentEventValues.IsEmpty())
	{
		const FString RecentEvent = PlayerContext.RecentEvent.TrimStartAndEnd();
		if (!RecentEvent.IsEmpty() && !RecentEvent.Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			RecentEventValues.Add(MakeShared<FJsonValueString>(RecentEvent));
		}
	}
	PlayerObject->SetArrayField(TEXT("recent_events"), RecentEventValues);
	Root->SetObjectField(TEXT("player"), PlayerObject);

	TSharedPtr<FJsonObject> WorldObject = MakeShared<FJsonObject>();
	for (const TPair<FName, bool>& Pair : WorldStateTags)
	{
		WorldObject->SetBoolField(Pair.Key.ToString(), Pair.Value);
	}
	Root->SetObjectField(TEXT("world"), WorldObject);

	TSharedPtr<FJsonObject> PlanObject = MakeShared<FJsonObject>();
	PlanObject->SetStringField(TEXT("status"),
		ActivePlan.Status == ENPCAutonomyPlanStatus::Suspended ? TEXT("suspended")
		: ActivePlan.Status == ENPCAutonomyPlanStatus::Active ? TEXT("active")
		: TEXT("none"));
	PlanObject->SetStringField(TEXT("goal"), ActivePlan.Goal);
	PlanObject->SetNumberField(TEXT("current_step_index"), ActivePlan.CurrentStepIndex);
	TArray<TSharedPtr<FJsonValue>> PlanStepValues;
	for (const FString& Step : ActivePlan.Steps)
	{
		PlanStepValues.Add(MakeShared<FJsonValueString>(Step));
	}
	PlanObject->SetArrayField(TEXT("steps"), PlanStepValues);
	Root->SetObjectField(TEXT("active_plan"), PlanObject);

	TSharedPtr<FJsonObject> PlanningObject = MakeShared<FJsonObject>();
	PlanningObject->SetNumberField(
		TEXT("max_steps"),
		DecisionMode == ENPCAutonomyDecisionMode::FixedIntervalSingleAction
			? 1
			: FMath::Clamp(MaxPlanSteps, 1, 6));
	PlanningObject->SetNumberField(
		TEXT("preferred_min_steps"),
		DecisionMode == ENPCAutonomyDecisionMode::FixedIntervalSingleAction
			? 1
			: FMath::Min(3, FMath::Clamp(MaxPlanSteps, 1, 6)));
	PlanningObject->SetBoolField(TEXT("must_be_interruptible"), true);
	Root->SetObjectField(TEXT("planning"), PlanningObject);

	TArray<TSharedPtr<FJsonValue>> AvailableActionValues;
	for (const FString& ActivityID : AvailableActivityIDs)
	{
		const FNPCAutonomyActivityDefinition* Activity = FindActivityDefinition(ActivityID);
		if (!Activity)
		{
			continue;
		}

		TSharedPtr<FJsonObject> ActivityObject = MakeShared<FJsonObject>();
		ActivityObject->SetStringField(TEXT("id"), ActivityID);
		ActivityObject->SetStringField(TEXT("category"), GetCategoryName(Activity->Category));
		ActivityObject->SetStringField(TEXT("target_scene_tag"), Activity->TargetSceneTag.TrimStartAndEnd());
		ActivityObject->SetStringField(TEXT("task_group"), Activity->TaskGroup.ToString());
		ActivityObject->SetNumberField(TEXT("task_order"), Activity->TaskOrder);
		ActivityObject->SetBoolField(TEXT("required_task_step"), Activity->bRequiredTaskStep);
		ActivityObject->SetStringField(TEXT("required_world_state"), Activity->RequiredWorldStateTag.ToString());
		ActivityObject->SetStringField(TEXT("clears_world_state_on_success"), Activity->WorldStateTagToClearOnSuccess.ToString());
		ActivityObject->SetNumberField(TEXT("stimulation_gain"), Activity->StimulationGain);
		ActivityObject->SetNumberField(TEXT("duty_relief"), Activity->DutyRelief);
		ActivityObject->SetNumberField(TEXT("energy_delta"), Activity->EnergyDelta);
		ActivityObject->SetNumberField(TEXT("minimum_energy"), Activity->MinimumEnergy);
		if (!Activity->LLMDescription.TrimStartAndEnd().IsEmpty())
		{
			ActivityObject->SetStringField(TEXT("description"), Activity->LLMDescription.TrimStartAndEnd());
		}
		AvailableActionValues.Add(MakeShared<FJsonValueObject>(ActivityObject));
	}
	Root->SetArrayField(TEXT("available_actions"), AvailableActionValues);

	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Json;
}

FString UNPCAutonomyComponent::ResolvePlayerActivity() const
{
	AActor* PlayerActor = ServiceManager ? ServiceManager->CurrentPlayerActor : nullptr;
	if (!PlayerActor)
	{
		return TEXT("absent");
	}

	if (ServiceManager->CurrentState == ECafeServiceState::GuidingToSeat)
	{
		return TEXT("following_npc");
	}
	const UPlayerSeatComponent* SeatComponent = PlayerActor->FindComponentByClass<UPlayerSeatComponent>();
	if (SeatComponent && SeatComponent->bIsSeated)
	{
		return TEXT("seated");
	}

	const FString ExplicitActivity = PlayerContext.Activity.TrimStartAndEnd();
	if (!ExplicitActivity.IsEmpty()
		&& !ExplicitActivity.Equals(TEXT("unknown"), ESearchCase::IgnoreCase)
		&& !ExplicitActivity.Equals(TEXT("seated"), ESearchCase::IgnoreCase))
	{
		return ExplicitActivity;
	}
	if (!SeatComponent && UNPCActionQueueComponent::IsLastKnownSeatedForService())
	{
		return TEXT("seated");
	}
	if (PlayerActor->GetVelocity().SizeSquared2D() > FMath::Square(10.0f))
	{
		return TEXT("walking");
	}
	return TEXT("idle");
}

FString UNPCAutonomyComponent::ResolvePlayerZone() const
{
	if (!ServiceManager || !ServiceManager->CurrentPlayerActor)
	{
		return TEXT("outside");
	}

	const FString ExplicitZone = PlayerContext.Zone.TrimStartAndEnd();
	if (!ExplicitZone.IsEmpty() && !ExplicitZone.Equals(TEXT("unknown"), ESearchCase::IgnoreCase))
	{
		return ExplicitZone;
	}

	if (UNPCActionQueueComponent::IsLastKnownPlayerInTableArea())
	{
		const FString TableID = UNPCActionQueueComponent::GetLastKnownCurrentServiceTableID();
		return TableID.IsEmpty() ? TEXT("table_area") : TableID;
	}

	if (ServiceManager->CurrentState == ECafeServiceState::WaitingForReceptionResponse
		|| ServiceManager->CurrentState == ECafeServiceState::ReceptionGreeting)
	{
		return TEXT("entrance");
	}
	return TEXT("dining_area");
}

FString UNPCAutonomyComponent::GetServiceStateName() const
{
	if (!ServiceManager)
	{
		return TEXT("unknown");
	}

	const UEnum* Enum = StaticEnum<ECafeServiceState>();
	return Enum
		? Enum->GetNameStringByValue(static_cast<int64>(ServiceManager->CurrentState))
		: TEXT("unknown");
}

FString UNPCAutonomyComponent::ResolveCurrentPerformanceTone() const
{
	const FNPCEmotionState EmotionState = EmotionComponent
		? EmotionComponent->GetCurrentState()
		: FNPCEmotionState();
	const float EffectiveValence = FMath::Abs(EmotionState.ShortTermValence) >= FMath::Abs(EmotionState.MoodValence)
		? EmotionState.ShortTermValence
		: EmotionState.MoodValence;

	if (EffectiveValence <= -0.15f)
	{
		return EmotionState.Arousal >= HighArousalNegativeReactionThreshold
			? TEXT("tense")
			: TEXT("subdued");
	}
	if (EffectiveValence >= 0.2f)
	{
		return EmotionState.Arousal >= 0.55f
			? TEXT("cheerful_activated")
			: TEXT("content");
	}
	return TEXT("neutral");
}

const FNPCAutonomyActivityDefinition* UNPCAutonomyComponent::FindActivityDefinition(const FString& ActivityID) const
{
	return GetConfiguredActivities().FindByPredicate([&ActivityID](const FNPCAutonomyActivityDefinition& Activity)
	{
		return Activity.ActivityID.TrimStartAndEnd().Equals(ActivityID, ESearchCase::IgnoreCase);
	});
}

bool UNPCAutonomyComponent::IsActionOffered(const FString& ActivityID) const
{
	return LastRequestedAvailableActions.ContainsByPredicate([&ActivityID](const FString& OfferedAction)
	{
		return OfferedAction.Equals(ActivityID.TrimStartAndEnd(), ESearchCase::IgnoreCase);
	});
}

bool UNPCAutonomyComponent::ValidateTaskPlanStructure(const TArray<FString>& Steps, FString& OutError) const
{
	OutError = TEXT("");
	TSet<FName> SelectedTaskGroups;
	TMap<FName, int32> LastOrderByTaskGroup;

	for (const FString& StepID : Steps)
	{
		const FNPCAutonomyActivityDefinition* Activity = FindActivityDefinition(StepID);
		if (!Activity || Activity->TaskGroup.IsNone())
		{
			continue;
		}

		SelectedTaskGroups.Add(Activity->TaskGroup);
		if (const int32* PreviousOrder = LastOrderByTaskGroup.Find(Activity->TaskGroup))
		{
			if (Activity->TaskOrder < *PreviousOrder)
			{
				OutError = FString::Printf(
					TEXT("task group %s is out of order at %s"),
					*Activity->TaskGroup.ToString(),
					*StepID);
				return false;
			}
		}
		LastOrderByTaskGroup.Add(Activity->TaskGroup, Activity->TaskOrder);
	}

	for (const FName& TaskGroup : SelectedTaskGroups)
	{
		for (const FNPCAutonomyActivityDefinition& Activity : GetConfiguredActivities())
		{
			const FString RequiredStepID = Activity.ActivityID.TrimStartAndEnd();
			if (Activity.TaskGroup != TaskGroup
				|| !Activity.bRequiredTaskStep
				|| !IsActionOffered(RequiredStepID))
			{
				continue;
			}

			const bool bRequiredStepIncluded = Steps.ContainsByPredicate([&RequiredStepID](const FString& Step)
			{
				return Step.Equals(RequiredStepID, ESearchCase::IgnoreCase);
			});
			if (!bRequiredStepIncluded)
			{
				OutError = FString::Printf(
					TEXT("task group %s is missing required step %s"),
					*TaskGroup.ToString(),
					*RequiredStepID);
				return false;
			}
		}
	}

	return true;
}

bool UNPCAutonomyComponent::ParsePlanDecision(
	const FString& SelectedAction,
	const FString& RawJson,
	FString& OutDecision,
	FString& OutGoal,
	TArray<FString>& OutSteps,
	FString& OutCommitment,
	FString& OutSurfaceThought) const
{
	OutDecision = TEXT("");
	OutGoal = TEXT("");
	OutSteps.Reset();
	OutCommitment = TEXT("low");
	OutSurfaceThought = TEXT("");

	TSharedPtr<FJsonObject> DecisionObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawJson);
	if (FJsonSerializer::Deserialize(Reader, DecisionObject) && DecisionObject.IsValid())
	{
		DecisionObject->TryGetStringField(TEXT("decision"), OutDecision);
		DecisionObject->TryGetStringField(TEXT("goal"), OutGoal);
		DecisionObject->TryGetStringField(TEXT("commitment"), OutCommitment);
		DecisionObject->TryGetStringField(TEXT("surface_thought"), OutSurfaceThought);

		const TArray<TSharedPtr<FJsonValue>>* StepValues = nullptr;
		if (DecisionObject->TryGetArrayField(TEXT("steps"), StepValues) && StepValues)
		{
			for (const TSharedPtr<FJsonValue>& StepValue : *StepValues)
			{
				FString Step;
				if (StepValue.IsValid() && StepValue->TryGetString(Step))
				{
					Step = Step.TrimStartAndEnd();
					if (!Step.IsEmpty())
					{
						OutSteps.Add(Step);
					}
				}
			}
		}
	}

	const FString TrimmedSelectedAction = SelectedAction.TrimStartAndEnd();
	if (OutSteps.IsEmpty()
		&& !TrimmedSelectedAction.IsEmpty()
		&& !TrimmedSelectedAction.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		OutSteps.Add(TrimmedSelectedAction);
	}
	if (OutDecision.TrimStartAndEnd().IsEmpty())
	{
		OutDecision = OutSteps.IsEmpty() ? TEXT("wait") : TEXT("start_plan");
	}

	OutDecision = OutDecision.TrimStartAndEnd();
	OutGoal = OutGoal.TrimStartAndEnd();
	OutCommitment = OutCommitment.TrimStartAndEnd().ToLower();
	OutSurfaceThought = OutSurfaceThought.TrimStartAndEnd();
	if (!OutCommitment.Equals(TEXT("medium")) && !OutCommitment.Equals(TEXT("low")))
	{
		OutCommitment = TEXT("low");
	}

	const int32 StepLimit = DecisionMode == ENPCAutonomyDecisionMode::FixedIntervalSingleAction
		? 1
		: FMath::Clamp(MaxPlanSteps, 1, 6);
	if (OutSteps.Num() > StepLimit)
	{
		OutSteps.SetNum(StepLimit);
	}

	return OutDecision.Equals(TEXT("wait"), ESearchCase::IgnoreCase) || !OutSteps.IsEmpty();
}

bool UNPCAutonomyComponent::TryStartCurrentPlanStep(bool bIsResume)
{
	if ((ActivePlan.Status != ENPCAutonomyPlanStatus::Active
			&& ActivePlan.Status != ENPCAutonomyPlanStatus::Suspended)
		|| !ActivePlan.Steps.IsValidIndex(ActivePlan.CurrentStepIndex))
	{
		return false;
	}

	const FString StepID = ActivePlan.Steps[ActivePlan.CurrentStepIndex].TrimStartAndEnd();
	const FNPCAutonomyActivityDefinition* Activity = FindActivityDefinition(StepID);
	if (!Activity || !IsActivityAvailable(*Activity))
	{
		return false;
	}

	ActivePlan.Status = ENPCAutonomyPlanStatus::Active;
	if (bIsResume)
	{
		++TotalPlansResumed;
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[AutonomyPlan] resumed. Goal=%s Step=%d/%d Action=%s"),
			*ActivePlan.Goal,
			ActivePlan.CurrentStepIndex + 1,
			ActivePlan.Steps.Num(),
			*StepID);
	}

	if (!StartActivity(*Activity, LastDecisionReason))
	{
		return false;
	}

	++TotalPlanStepsStarted;
	return true;
}

void UNPCAutonomyComponent::AdvanceOrFinishPlan()
{
	if (ActivePlan.Status != ENPCAutonomyPlanStatus::Active)
	{
		ScheduleNextDecision(DelayAfterActivitySeconds);
		return;
	}

	++ActivePlan.CurrentStepIndex;
	if (ActivePlan.Steps.IsValidIndex(ActivePlan.CurrentStepIndex))
	{
		ScheduleNextDecision(DelayBetweenPlanStepsSeconds);
		return;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[AutonomyPlan] completed. Goal=%s Steps=%d"),
		*ActivePlan.Goal,
		ActivePlan.Steps.Num());
	ClearActivePlan(TEXT("completed"));
	RollNextBoredomThreshold();
	if (UWorld* World = GetWorld())
	{
		const double CurrentTime = World->GetTimeSeconds();
		PlanningSuppressedUntilWorldTime = CurrentTime + FMath::Max(PostPlanFreeActivitySeconds, 0.0f);
		PostPlanRecallBoostUntilWorldTime = CurrentTime + FMath::Max(PostPlanRecallBoostDurationSeconds, 0.0f);
		NextIdleStatusLogWorldTime = CurrentTime;

		const float MotiveTimeScale = bFastValidationMode
			? FMath::Max(FastValidationMotiveTimeScale, 1.0f)
			: 1.0f;
		const float BoredomRate = BoredomIncreasePerIdleSecond * MotiveTimeScale;
		const float EstimatedSecondsToDecision = BoredomRate > KINDA_SMALL_NUMBER
			? FMath::Max((CurrentBoredomDecisionThreshold - MotiveState.Boredom) / BoredomRate, 0.0f)
			: -1.0f;

		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetNumberField(TEXT("boredom"), MotiveState.Boredom);
		Payload->SetNumberField(TEXT("next_boredom_threshold"), CurrentBoredomDecisionThreshold);
		Payload->SetNumberField(TEXT("estimated_seconds_to_plan_gate"), EstimatedSecondsToDecision);
		Payload->SetNumberField(TEXT("recall_boost_seconds"), PostPlanRecallBoostDurationSeconds);
		Payload->SetNumberField(TEXT("recall_boost_multiplier"), PostPlanRecallOpportunityMultiplier);
		RecordResearchEvent(TEXT("post_plan_idle_window_started"), Payload);

		UE_LOG(
			LogTemp,
			Log,
			TEXT("[AutonomyIdle] post-plan window started. Boredom=%.1f/%.1f EstimatedPlanGate=%.0fs FreeActivity=%.0fs RecallBoost=%.1fx for %.0fs"),
			MotiveState.Boredom,
			CurrentBoredomDecisionThreshold,
			EstimatedSecondsToDecision,
			PostPlanFreeActivitySeconds,
			PostPlanRecallOpportunityMultiplier,
			PostPlanRecallBoostDurationSeconds);
	}
	ScheduleNextDecision(DelayAfterActivitySeconds);
}

void UNPCAutonomyComponent::ClearActivePlan(const FString& Reason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlanThoughtDisplayTimerHandle);
	}
	PendingPlanSurfaceThought = TEXT("");
	const bool bReleasedExpressionOverlay = bExpressionOverlayHeldByPlan;
	const FString ClearedGoal = ActivePlan.Goal;
	if (ActivePlan.Status != ENPCAutonomyPlanStatus::None || !ActivePlan.Steps.IsEmpty())
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[AutonomyPlan] cleared. Reason=%s Goal=%s Progress=%d/%d"),
			*Reason,
			*ActivePlan.Goal,
			ActivePlan.CurrentStepIndex,
			ActivePlan.Steps.Num());
	}
	ActivePlan = FNPCAutonomyPlan();
	if (bReleasedExpressionOverlay)
	{
		ResetExpressionMorphs();
		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("reason"), Reason);
		Payload->SetStringField(TEXT("goal"), ClearedGoal);
		RecordResearchEvent(TEXT("expression_overlay_released"), Payload);
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[ExpressionOverlay] released with plan. Reason=%s Goal=%s"),
			*Reason,
			*ClearedGoal);
	}
}

bool UNPCAutonomyComponent::ShouldSuspendActivePlan(const FString& InterruptionReason) const
{
	if (ActivePlan.Status == ENPCAutonomyPlanStatus::None
		|| InterruptionReason.Equals(TEXT("end_play"), ESearchCase::IgnoreCase)
		|| InterruptionReason.Equals(TEXT("autonomy_disabled"), ESearchCase::IgnoreCase)
		|| InterruptionReason.Equals(TEXT("world_state_changed"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	if (ActivePlan.Commitment.Equals(TEXT("medium"), ESearchCase::IgnoreCase))
	{
		return true;
	}

	if (!bResumeTaskPlansAfterService)
	{
		return false;
	}
	for (int32 StepIndex = ActivePlan.CurrentStepIndex; StepIndex < ActivePlan.Steps.Num(); ++StepIndex)
	{
		const FNPCAutonomyActivityDefinition* Activity = FindActivityDefinition(ActivePlan.Steps[StepIndex]);
		if (Activity && Activity->Category == ENPCAutonomyActivityCategory::Task)
		{
			return true;
		}
	}
	return false;
}

void UNPCAutonomyComponent::SuspendOrDiscardPlan(const FString& InterruptionReason)
{
	if (ActivePlan.Status == ENPCAutonomyPlanStatus::None)
	{
		return;
	}

	if (ShouldSuspendActivePlan(InterruptionReason))
	{
		if (ActivePlan.Status != ENPCAutonomyPlanStatus::Suspended)
		{
			ActivePlan.Status = ENPCAutonomyPlanStatus::Suspended;
			++TotalPlanInterruptions;
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[AutonomyPlan] suspended. Reason=%s Goal=%s Step=%d/%d"),
				*InterruptionReason,
				*ActivePlan.Goal,
				ActivePlan.CurrentStepIndex + 1,
				ActivePlan.Steps.Num());
		}
		return;
	}

	++TotalPlanInterruptions;
	ClearActivePlan(FString::Printf(TEXT("interrupted:%s"), *InterruptionReason));
}

bool UNPCAutonomyComponent::StartActivity(
	const FNPCAutonomyActivityDefinition& Activity,
	const FString& Reason,
	bool bIsAmbient)
{
	if (!CanRunAutonomy() || !IsActivityAvailable(Activity) || !ActionQueueComponent)
	{
		return false;
	}

	const FString ActivityID = Activity.ActivityID.TrimStartAndEnd();
	if (ActivityID.Equals(TEXT("sit_with_player"), ESearchCase::IgnoreCase)
		&& !bSeatInvitationAcceptedForStart)
	{
		const FString SeatID = !ForcedSeatID.IsEmpty()
			? ForcedSeatID
			: FindAvailableSeatOppositePlayerID();
		return !SeatID.IsEmpty() && BeginSeatInvitation(Activity, SeatID, Reason);
	}

	CurrentActivityID = ActivityID;
	LastDecisionReason = Reason;
	bActivityRunning = true;
	bCurrentActivityIsAmbient = bIsAmbient;
	RuntimePhase = ENPCAutonomyRuntimePhase::None;
	if (UWorld* World = GetWorld())
	{
		ActivityCooldownUntil.Add(ActivityID, World->GetTimeSeconds() + FMath::Max(Activity.CooldownSeconds, 0.0f));
		if (bEnableMultiStepPlanValidationScenario
			&& bAutoInterruptMultiStepValidationPlan
			&& !bMultiStepValidationInterruptConsumed
			&& Activity.TaskGroup == FName(TEXT("dining_area_reset"))
			&& Activity.TaskOrder == 2)
		{
			bMultiStepValidationInterruptConsumed = true;
			World->GetTimerManager().SetTimer(
				MultiStepValidationInterruptTimerHandle,
				this,
				&UNPCAutonomyComponent::HandleMultiStepValidationAutoInterrupt,
				FMath::Max(MultiStepValidationInterruptDelaySeconds, 0.1f),
				false);
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[AutonomyPlanTest] simulated player call scheduled during step %d. Delay=%.1fs"),
				Activity.TaskOrder,
				MultiStepValidationInterruptDelaySeconds);
		}
	}

	OnAutonomyActivityStarted.Broadcast(ActivityID, Reason);
	if (bEnableConcurrentExpressionOverlay
		&& bExpressionOverlayActive
		&& !bExpressionOverlayHeldByPlan
		&& !bIsAmbient
		&& ActivePlan.Status == ENPCAutonomyPlanStatus::Active)
	{
		ApplyExpressionMorphs(CurrentExpressionIntent, true);
		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("activity"), ActivityID);
		Payload->SetStringField(TEXT("goal"), ActivePlan.Goal);
		Payload->SetStringField(TEXT("expression_intent"), CurrentExpressionIntent);
		RecordResearchEvent(TEXT("expression_overlay_bound_to_plan"), Payload);
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[ExpressionOverlay] bound to active plan. Intent=%s Activity=%s Goal=%s"),
			*CurrentExpressionIntent,
			*ActivityID,
			*ActivePlan.Goal);
	}
	if (bEnableConcurrentExpressionOverlay && bIsAmbient)
	{
		FString AmbientExpressionIntent;
		if (Activity.AmbientDrive == ENPCAutonomyAmbientDrive::PositiveEmotion)
		{
			AmbientExpressionIntent = TEXT("hum");
		}
		else if (Activity.AmbientDrive == ENPCAutonomyAmbientDrive::NegativeEmotion)
		{
			const FString NormalizedCurrentIntent = CurrentExpressionIntent.TrimStartAndEnd().ToLower();
			AmbientExpressionIntent = NormalizedCurrentIntent == TEXT("alert")
				|| NormalizedCurrentIntent == TEXT("pensive")
				|| NormalizedCurrentIntent == TEXT("sigh")
				? NormalizedCurrentIntent
				: TEXT("sigh");
		}

		if (!AmbientExpressionIntent.IsEmpty())
		{
			ApplyExpressionMorphs(
				AmbientExpressionIntent,
				false,
				FMath::Max(Activity.AnimationFallbackSeconds + 1.0f, 3.0f));
			TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("activity"), ActivityID);
			Payload->SetStringField(TEXT("performance_tone"), ResolveCurrentPerformanceTone());
			Payload->SetStringField(TEXT("expression_intent"), AmbientExpressionIntent);
			RecordResearchEvent(TEXT("ambient_emotion_performance_started"), Payload);
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[AmbientEmotion] performance started. Activity=%s Tone=%s Expression=%s"),
				*ActivityID,
				*ResolveCurrentPerformanceTone(),
				*AmbientExpressionIntent);
		}
	}
	ActionQueueComponent->ClearActionQueue();

	if (Activity.ExecutionMode == ENPCAutonomyExecutionMode::BlueprintEvent)
	{
		if (Activity.bMoveToTargetBeforeBlueprintEvent
			&& !Activity.TargetSceneTag.TrimStartAndEnd().IsEmpty()
			&& !IsMaidNearSceneTargetTag(Activity.TargetSceneTag))
		{
			ActionQueueComponent->EnqueueMoveToSceneTag(Activity.TargetSceneTag.TrimStartAndEnd());
			RuntimePhase = ENPCAutonomyRuntimePhase::BlueprintPreMove;
			ActionQueueComponent->StartActionQueue();
			if (!ActionQueueComponent->bIsRunningQueue)
			{
				FinishActivity(true);
				return false;
			}
		}
		else
		{
			BeginBlueprintActivityExecution(Activity);
		}

		UE_LOG(LogTemp, Log, TEXT("[Autonomy] blueprint activity started. Activity=%s"), *ActivityID);
		return true;
	}

	if (ActivityID.Equals(TEXT("rest_on_sofa"), ESearchCase::IgnoreCase)
		|| ActivityID.Equals(TEXT("sit_with_player"), ESearchCase::IgnoreCase))
	{
		const FString SeatID = !ForcedSeatID.IsEmpty()
			? ForcedSeatID
			: ActivityID.Equals(TEXT("rest_on_sofa"), ESearchCase::IgnoreCase)
				? FindAvailableRestSeatID()
				: FindAvailableSeatOppositePlayerID();
		if (SeatID.IsEmpty() || !StartSeatActivitySequence(Activity, SeatID))
		{
			FinishActivity(true);
			return false;
		}

		RuntimePhase = ENPCAutonomyRuntimePhase::BuiltInQueue;
		ActionQueueComponent->StartActionQueue();
		if (!ActionQueueComponent->bIsRunningQueue)
		{
			FinishActivity(true);
			return false;
		}

		UE_LOG(
			LogTemp,
			Log,
			TEXT("[Autonomy] seat activity started. Activity=%s Seat=%s"),
			*ActivityID,
			*SeatID);
		return true;
	}

	const bool bCollectTableware = ActivityID.Equals(TEXT("collect_tableware"), ESearchCase::IgnoreCase);
	const UNPCFoodCarryComponent* FoodCarryComponent = MaidNPCActor
		? MaidNPCActor->FindComponentByClass<UNPCFoodCarryComponent>()
		: nullptr;
	const bool bResumingTablewareReturn = bCollectTableware
		&& FoodCarryComponent
		&& FoodCarryComponent->IsCarryingFood();

	if (!bResumingTablewareReturn
		&& !Activity.TargetSceneTag.TrimStartAndEnd().IsEmpty()
		&& !IsMaidNearSceneTargetTag(Activity.TargetSceneTag))
	{
		ActionQueueComponent->EnqueueMoveToSceneTag(Activity.TargetSceneTag.TrimStartAndEnd());
	}

	ActiveActivityAnimation = bResumingTablewareReturn
		? nullptr
		: Activity.Animation.LoadSynchronous();
	UStaticMesh* HeldPropMesh = bResumingTablewareReturn
		? nullptr
		: Activity.HeldPropMesh.LoadSynchronous();
	if (HeldPropMesh)
	{
		ActionQueueComponent->EnqueueAttachHeldProp(
			HeldPropMesh,
			Activity.HeldPropSocketName,
			Activity.HeldPropRelativeTransform);
	}
	else if (!Activity.HeldPropMesh.IsNull())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[Autonomy] held prop could not be loaded for %s. Asset=%s"),
			*ActivityID,
			*Activity.HeldPropMesh.ToSoftObjectPath().ToString());
	}
	if (ActiveActivityAnimation)
	{
		ActionQueueComponent->EnqueueAnimation(
			ActiveActivityAnimation,
			Activity.AnimationSlotName,
			Activity.AnimationPlayRate,
			Activity.AnimationFallbackSeconds);
	}
	else if (!bResumingTablewareReturn)
	{
		ActionQueueComponent->EnqueueWait(Activity.AnimationFallbackSeconds);
		UE_LOG(LogTemp, Warning, TEXT("[Autonomy] animation not loaded for %s; using wait fallback."), *ActivityID);
	}
	if (HeldPropMesh)
	{
		ActionQueueComponent->EnqueueRemoveHeldProp();
	}

	if (bCollectTableware)
	{
		if (!bResumingTablewareReturn)
		{
			ActionQueueComponent->EnqueuePickUpPlacedFood(Activity.TargetSceneTag.TrimStartAndEnd());
		}
		ActionQueueComponent->EnqueueMoveToSceneTag(TEXT("counter"));
		ActionQueueComponent->EnqueuePlaceCarriedFoodAtCounter();
	}
	else if (Activity.bReturnToCounterAfterCompletion
		&& !Activity.TargetSceneTag.Equals(TEXT("counter"), ESearchCase::IgnoreCase))
	{
		ActionQueueComponent->EnqueueMoveToSceneTag(TEXT("counter"));
	}

	RuntimePhase = ENPCAutonomyRuntimePhase::BuiltInQueue;
	ActionQueueComponent->StartActionQueue();
	if (!ActionQueueComponent->bIsRunningQueue)
	{
		FinishActivity(true);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[Autonomy] activity started. Activity=%s"), *ActivityID);
	return true;
}

bool UNPCAutonomyComponent::StartSeatActivitySequence(
	const FNPCAutonomyActivityDefinition& Activity,
	const FString& SeatID)
{
	if (!ActionQueueComponent || SeatID.TrimStartAndEnd().IsEmpty())
	{
		return false;
	}

	UAnimSequenceBase* SitDown = SitDownAnimation.LoadSynchronous();
	UAnimSequenceBase* SeatedIdle = SeatedIdleAnimation.LoadSynchronous();
	UAnimSequenceBase* SeatedConversation = SeatedConversationAnimation.LoadSynchronous();
	UAnimSequenceBase* StandUp = StandUpAnimation.LoadSynchronous();
	if (!SitDown || !SeatedIdle || !StandUp
		|| !IsAnimationCompatibleWithMaid(SitDown)
		|| !IsAnimationCompatibleWithMaid(SeatedIdle)
		|| !IsAnimationCompatibleWithMaid(StandUp))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[NPCSeat] seat animations are missing or incompatible. Sit=%s Idle=%s Stand=%s"),
			SitDown ? *SitDown->GetName() : TEXT("none"),
			SeatedIdle ? *SeatedIdle->GetName() : TEXT("none"),
			StandUp ? *StandUp->GetName() : TEXT("none"));
		return false;
	}

	ActiveActivityAnimation = SeatedIdle;
	ActionQueueComponent->EnqueueMoveToSeatID(SeatID.TrimStartAndEnd());
	ActionQueueComponent->EnqueueEnterSeat();
	ActionQueueComponent->EnqueueAnimation(SitDown, FName(TEXT("DefaultSlot")), 1.0f, 3.0f);
	if (Activity.ActivityID.Equals(TEXT("sit_with_player"), ESearchCase::IgnoreCase)
		&& IsAnimationCompatibleWithMaid(SeatedConversation))
	{
		ActionQueueComponent->EnqueueAnimation(
			SeatedConversation,
			FName(TEXT("DefaultSlot")),
			1.0f,
			5.0f);
	}
	const int32 IdleLoopCount = Activity.ActivityID.Equals(TEXT("sit_with_player"), ESearchCase::IgnoreCase)
		? SocialSeatedIdleLoopCount
		: SeatedIdleLoopCount;
	ActionQueueComponent->EnqueueLoopedAnimation(
		SeatedIdle,
		FMath::Max(IdleLoopCount, 1),
		FName(TEXT("DefaultSlot")),
		1.0f,
		FMath::Max(Activity.AnimationFallbackSeconds, 3.0f));
	ActionQueueComponent->EnqueueAnimation(StandUp, FName(TEXT("DefaultSlot")), 1.0f, 3.0f);
	ActionQueueComponent->EnqueueExitSeat();
	return true;
}

bool UNPCAutonomyComponent::BeginSeatInvitation(
	const FNPCAutonomyActivityDefinition& Activity,
	const FString& SeatID,
	const FString& Reason)
{
	if (!ActionQueueComponent || SeatID.TrimStartAndEnd().IsEmpty())
	{
		return false;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SeatInvitationTimeoutTimerHandle);
	}
	bSeatInvitationApproachInProgress = true;
	bSeatInvitationPending = false;
	PendingSeatInvitationSeatID = SeatID.TrimStartAndEnd();
	LastDecisionReason = Reason;
	NextDecisionEarliestWorldTime = TNumericLimits<double>::Max();

	ActionQueueComponent->ClearActionQueue();
	ActionQueueComponent->EnqueueMoveToSeatID(PendingSeatInvitationSeatID);
	ActionQueueComponent->EnqueueFacePlayer();
	ActionQueueComponent->StartActionQueue();
	if (!ActionQueueComponent->bIsRunningQueue)
	{
		bSeatInvitationApproachInProgress = false;
		PendingSeatInvitationSeatID.Reset();
		ScheduleNextDecision(DecisionErrorRetrySeconds);
		return false;
	}

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("activity"), Activity.ActivityID.TrimStartAndEnd());
	Payload->SetStringField(TEXT("seat_id"), PendingSeatInvitationSeatID);
	Payload->SetStringField(TEXT("reason"), Reason);
	RecordResearchEvent(TEXT("seat_invitation_approach_started"), Payload);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[NPCSeatInvitation] approaching before asking. Seat=%s Reason=%s"),
		*PendingSeatInvitationSeatID,
		*Reason);
	return true;
}

void UNPCAutonomyComponent::PresentSeatInvitation()
{
	bSeatInvitationApproachInProgress = false;
	if (PendingSeatInvitationSeatID.IsEmpty() || FindAvailableSeatOppositePlayerID().IsEmpty())
	{
		CancelSeatInvitation(TEXT("player_or_seat_unavailable_before_question"), true, true);
		return;
	}

	bSeatInvitationPending = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			SeatInvitationTimeoutTimerHandle,
			this,
			&UNPCAutonomyComponent::HandleSeatInvitationTimeout,
			FMath::Max(SeatInvitationTimeoutSeconds, 5.0f),
			false);
	}

	ApplyExpressionMorphs(TEXT("smile"), false, 4.0f);
	const FString InvitationText = SeatInvitationText.TrimStartAndEnd().IsEmpty()
		? TEXT("少しゆっくりお話ししたいのですが、向かいの席に座ってもいいですか？")
		: SeatInvitationText.TrimStartAndEnd();
	if (ULLMClient* InteractiveClient = ULLMClient::GetMostRecentInteractiveClient())
	{
		InteractiveClient->SpeakDirectText(InvitationText);
	}
	else if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Cyan, InvitationText);
	}

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("seat_id"), PendingSeatInvitationSeatID);
	Payload->SetNumberField(TEXT("timeout_seconds"), SeatInvitationTimeoutSeconds);
	RecordResearchEvent(TEXT("seat_invitation_presented"), Payload);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[NPCSeatInvitation] waiting for explicit consent. Seat=%s Timeout=%.1fs"),
		*PendingSeatInvitationSeatID,
		SeatInvitationTimeoutSeconds);
}

void UNPCAutonomyComponent::HandleSeatInvitationTimeout()
{
	if (!bSeatInvitationPending)
	{
		return;
	}
	CancelSeatInvitation(TEXT("player_response_timeout"), true, true);
}

void UNPCAutonomyComponent::BeginSeatedConversationOpening()
{
	if (!bActivityRunning
		|| !CurrentActivityID.Equals(TEXT("sit_with_player"), ESearchCase::IgnoreCase)
		|| !ServiceManager)
	{
		return;
	}

	++SeatedConversationOpeningAttempts;
	const bool bRequested = ServiceManager->TriggerSeatedConversationOpportunity();
	if (!bRequested && SeatedConversationOpeningAttempts < 8)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				SeatedConversationOpeningTimerHandle,
				this,
				&UNPCAutonomyComponent::BeginSeatedConversationOpening,
				1.5f,
				false);
		}
	}
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetBoolField(TEXT("request_started"), bRequested);
	Payload->SetNumberField(TEXT("attempt"), SeatedConversationOpeningAttempts);
	RecordResearchEvent(TEXT("seated_conversation_opening_requested"), Payload);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[NPCSeat] seated conversation opening request started=%s Attempt=%d"),
		bRequested ? TEXT("true") : TEXT("false"),
		SeatedConversationOpeningAttempts);
}

void UNPCAutonomyComponent::CancelSeatInvitation(
	const FString& Reason,
	bool bClearPlan,
	bool bReturnToCounter)
{
	const FString CancelledSeatID = PendingSeatInvitationSeatID;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SeatInvitationTimeoutTimerHandle);
		if (Reason.Equals(TEXT("player_declined"), ESearchCase::IgnoreCase)
			|| Reason.Equals(TEXT("player_response_timeout"), ESearchCase::IgnoreCase))
		{
			ActivityCooldownUntil.Add(
				TEXT("sit_with_player"),
				World->GetTimeSeconds() + FMath::Max(SeatInvitationDeclineCooldownSeconds, 0.0f));
		}
	}

	bSeatInvitationApproachInProgress = false;
	bSeatInvitationPending = false;
	bSeatInvitationAcceptedForStart = false;
	PendingSeatInvitationSeatID.Reset();
	if (ActionQueueComponent)
	{
		ActionQueueComponent->StopActionQueue();
	}
	if (bClearPlan)
	{
		ClearActivePlan(FString::Printf(TEXT("seat_invitation_cancelled:%s"), *Reason));
	}
	if (bReturnToCounter)
	{
		ReturnToCounterAfterSeatInvitation();
	}
	ScheduleNextDecision(DelayAfterPlayerInteractionSeconds);

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("seat_id"), CancelledSeatID);
	Payload->SetStringField(TEXT("reason"), Reason);
	RecordResearchEvent(TEXT("seat_invitation_cancelled"), Payload);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[NPCSeatInvitation] cancelled. Seat=%s Reason=%s"),
		*CancelledSeatID,
		*Reason);
}

void UNPCAutonomyComponent::ReturnToCounterAfterSeatInvitation()
{
	if (!ActionQueueComponent || !HasSceneTargetTag(TEXT("counter")))
	{
		return;
	}
	ActionQueueComponent->EnqueueMoveToSceneTag(TEXT("counter"));
	ActionQueueComponent->StartActionQueue();
}

void UNPCAutonomyComponent::BeginBlueprintActivityExecution(const FNPCAutonomyActivityDefinition& Activity)
{
	if (!bActivityRunning || !CurrentActivityID.Equals(Activity.ActivityID.TrimStartAndEnd(), ESearchCase::IgnoreCase))
	{
		return;
	}

	RuntimePhase = ENPCAutonomyRuntimePhase::BlueprintExecuting;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			BlueprintActivityTimeoutTimerHandle,
			this,
			&UNPCAutonomyComponent::HandleBlueprintActivityTimeout,
			FMath::Max(Activity.BlueprintExecutionTimeoutSeconds, 1.0f),
			false);
	}

	OnBlueprintActivityRequested.Broadcast(CurrentActivityID, Activity.TargetSceneTag.TrimStartAndEnd());
	if (!OnBlueprintActivityRequested.IsBound())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[Autonomy] blueprint activity has no event listener and will time out. Activity=%s"),
			*CurrentActivityID);
	}
}

void UNPCAutonomyComponent::HandleBlueprintActivityTimeout()
{
	if (!bActivityRunning || RuntimePhase != ENPCAutonomyRuntimePhase::BlueprintExecuting)
	{
		return;
	}

	const FString TimedOutActivityID = CurrentActivityID;
	OnBlueprintActivityAborted.Broadcast(TimedOutActivityID, TEXT("timeout"));
	UE_LOG(LogTemp, Warning, TEXT("[Autonomy] blueprint activity timed out. Activity=%s"), *TimedOutActivityID);
	FinishActivity(true);
}

void UNPCAutonomyComponent::ApplyActivitySuccess(const FNPCAutonomyActivityDefinition& Activity)
{
	AWaterablePlantActor* WateredPlant = nullptr;
	const FString TargetSceneTag = Activity.TargetSceneTag.TrimStartAndEnd();
	if (!TargetSceneTag.IsEmpty() && MaidNPCActor)
	{
		float BestDistanceSq = TNumericLimits<float>::Max();
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AWaterablePlantActor> It(World); It; ++It)
			{
				AWaterablePlantActor* Plant = *It;
				if (!Plant || !Plant->IsAvailableForTag(TargetSceneTag))
				{
					continue;
				}

				const float DistanceSq = FVector::DistSquared2D(
					MaidNPCActor->GetActorLocation(),
					Plant->GetPlantTargetLocation());
				if (DistanceSq < BestDistanceSq)
				{
					BestDistanceSq = DistanceSq;
					WateredPlant = Plant;
				}
			}
		}
	}

	if (WateredPlant)
	{
		WateredPlant->WaterPlant(100.0f, false);
	}

	MotiveState.Boredom = FMath::Clamp(
		MotiveState.Boredom - FMath::Max(Activity.StimulationGain, 0.0f),
		0.0f,
		100.0f);
	MotiveState.Duty = FMath::Clamp(
		MotiveState.Duty - FMath::Max(Activity.DutyRelief, 0.0f),
		0.0f,
		100.0f);
	ResourceState.Energy = FMath::Clamp(
		ResourceState.Energy + Activity.EnergyDelta,
		0.0f,
		100.0f);

	if (!Activity.WorldStateTagToClearOnSuccess.IsNone())
	{
		WorldStateTags.Add(Activity.WorldStateTagToClearOnSuccess, false);
	}
	if (WateredPlant)
	{
		WateredPlant->RefreshAutonomyWorldState(false);
	}
	if (!Activity.TargetSceneTag.TrimStartAndEnd().IsEmpty())
	{
		LastKnownNPCZone = Activity.TargetSceneTag.TrimStartAndEnd();
	}
	RememberActivity(Activity.ActivityID.TrimStartAndEnd());

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[AutonomyState] action applied. Action=%s Boredom=%.1f Duty=%.1f Energy=%.1f"),
		*Activity.ActivityID,
		MotiveState.Boredom,
		MotiveState.Duty,
		ResourceState.Energy);
}

void UNPCAutonomyComponent::FinishActivity(bool bInterrupted)
{
	if (!bActivityRunning)
	{
		return;
	}

	const FString FinishedActivityID = CurrentActivityID;
	if (PendingInteractiveActivityID.Equals(FinishedActivityID, ESearchCase::IgnoreCase))
	{
		bAwaitingEmbodiedResponse = false;
		bResumePausedActivityAfterReply = false;
		PendingInteractiveActivityID = TEXT("");
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(EmbodiedResponseResumeTimerHandle);
		}
	}
	const bool bFinishedAmbientActivity = bCurrentActivityIsAmbient;
	const FNPCAutonomyActivityDefinition* FinishedActivity = FindActivityDefinition(FinishedActivityID);
	const bool bFinishedEmotionAmbientActivity = bFinishedAmbientActivity
		&& FinishedActivity
		&& (FinishedActivity->AmbientDrive == ENPCAutonomyAmbientDrive::PositiveEmotion
			|| FinishedActivity->AmbientDrive == ENPCAutonomyAmbientDrive::NegativeEmotion);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BlueprintActivityTimeoutTimerHandle);
		World->GetTimerManager().ClearTimer(SeatedConversationOpeningTimerHandle);
		LastActivityEndWorldTime = World->GetTimeSeconds();
	}
	bActivityRunning = false;
	bCurrentActivityIsAmbient = false;
	CurrentActivityID = TEXT("");
	ActiveActivityAnimation = nullptr;
	RuntimePhase = ENPCAutonomyRuntimePhase::None;
	OnAutonomyActivityFinished.Broadcast(FinishedActivityID, bInterrupted);
	if (bInterrupted)
	{
		if (bFinishedAmbientActivity)
		{
			if (bFinishedEmotionAmbientActivity)
			{
				ResetExpressionMorphs();
			}
			ScheduleNextDecision(MinimumSecondsBetweenAmbientActivities);
			return;
		}
		ClearActivePlan(TEXT("plan_step_failed"));
		MarkMeaningfulContextChange(TEXT("plan_step_failed"));
		ScheduleNextDecision(DecisionErrorRetrySeconds);
		return;
	}
	if (bFinishedAmbientActivity)
	{
		if (bFinishedEmotionAmbientActivity)
		{
			ResetExpressionMorphs();
		}
		UE_LOG(LogTemp, Log, TEXT("[Ambient] completed. Activity=%s"), *FinishedActivityID);
		ScheduleNextDecision(MinimumSecondsBetweenAmbientActivities);
		return;
	}

	AdvanceOrFinishPlan();
}

void UNPCAutonomyComponent::ScheduleNextDecision(float DelaySeconds)
{
	const UWorld* World = GetWorld();
	const double CurrentTime = World ? World->GetTimeSeconds() : 0.0;
	NextDecisionEarliestWorldTime = CurrentTime + FMath::Max(DelaySeconds, 0.0f);
}

void UNPCAutonomyComponent::RememberActivity(const FString& ActivityID)
{
	if (ActivityID.IsEmpty())
	{
		return;
	}

	RecentActions.Add(ActivityID);
	RecentActionCompletedWorldTimes.Add(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	while (RecentActions.Num() > 6)
	{
		RecentActions.RemoveAt(0);
		if (!RecentActionCompletedWorldTimes.IsEmpty())
		{
			RecentActionCompletedWorldTimes.RemoveAt(0);
		}
	}
}

FString UNPCAutonomyComponent::GetCategoryName(ENPCAutonomyActivityCategory Category)
{
	switch (Category)
	{
	case ENPCAutonomyActivityCategory::Task:
		return TEXT("task");
	case ENPCAutonomyActivityCategory::Preference:
		return TEXT("preference");
	case ENPCAutonomyActivityCategory::Expression:
		return TEXT("expression");
	default:
		return TEXT("leisure");
	}
}

void UNPCAutonomyComponent::RecordResearchEvent(
	const FString& EventType,
	const TSharedRef<FJsonObject>& Payload) const
{
	if (ResearchLoggerComponent)
	{
		ResearchLoggerComponent->RecordStructuredEvent(EventType, SerializeResearchPayload(Payload));
	}
}

float UNPCAutonomyComponent::CalculateCurrentActivityFocus() const
{
	if (!bActivityRunning || CurrentActivityID.TrimStartAndEnd().IsEmpty())
	{
		return 0.1f;
	}

	const FString ActivityID = CurrentActivityID.TrimStartAndEnd().ToLower();
	if (ActivityID == TEXT("phone_at_counter"))
	{
		return 0.85f;
	}
	if (ActivityID == TEXT("review_shift_notes"))
	{
		return 0.8f;
	}
	if (ActivityID == TEXT("water_flower") || ActivityID == TEXT("inspect_table_area"))
	{
		return 0.65f;
	}
	if (ActivityID == TEXT("collect_tableware") || ActivityID == TEXT("tidy_table_area"))
	{
		return 0.55f;
	}
	if (ActivityID == TEXT("rest_on_sofa"))
	{
		return 0.2f;
	}
	if (ActivityID == TEXT("sit_with_player"))
	{
		return 0.35f;
	}

	const FNPCAutonomyActivityDefinition* Activity = FindActivityDefinition(ActivityID);
	if (!Activity)
	{
		return 0.5f;
	}

	switch (Activity->Category)
	{
	case ENPCAutonomyActivityCategory::Task:
		return 0.55f;
	case ENPCAutonomyActivityCategory::Preference:
		return 0.6f;
	case ENPCAutonomyActivityCategory::Expression:
		return 0.5f;
	default:
		return 0.45f;
	}
}

float UNPCAutonomyComponent::CalculateExpectedConversationLevel() const
{
	if (!ServiceManager)
	{
		return 0.0f;
	}
	if (bSeatInvitationPending)
	{
		return 1.0f;
	}
	if (bActivityRunning
		&& CurrentActivityID.Equals(TEXT("sit_with_player"), ESearchCase::IgnoreCase))
	{
		return 0.9f;
	}

	switch (ServiceManager->CurrentState)
	{
	case ECafeServiceState::WaitingForReceptionResponse:
		return 1.0f;
	case ECafeServiceState::ReceptionGreeting:
	case ECafeServiceState::WaitingForPlayerSeated:
		return 0.8f;
	case ECafeServiceState::WaitingForOrder:
		return 0.55f;
	case ECafeServiceState::Served:
		return 0.25f;
	case ECafeServiceState::Idle:
		return 0.0f;
	default:
		return 0.7f;
	}
}

float UNPCAutonomyComponent::CalculatePlayerRearFactor(AActor*& OutPlayerActor, float& OutDistance) const
{
	OutPlayerActor = ServiceManager ? ServiceManager->CurrentPlayerActor : nullptr;
	if (!OutPlayerActor)
	{
		OutPlayerActor = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	}
	OutDistance = -1.0f;
	if (!MaidNPCActor || !OutPlayerActor)
	{
		return 0.0f;
	}

	FVector Forward = MaidNPCActor->GetActorForwardVector();
	FVector ToPlayer = OutPlayerActor->GetActorLocation() - MaidNPCActor->GetActorLocation();
	Forward.Z = 0.0f;
	ToPlayer.Z = 0.0f;
	OutDistance = ToPlayer.Size();
	if (!Forward.Normalize() || !ToPlayer.Normalize())
	{
		return 0.0f;
	}

	const float FacingDot = FMath::Clamp(FVector::DotProduct(Forward, ToPlayer), -1.0f, 1.0f);
	return FMath::Clamp((1.0f - FacingDot) * 0.5f, 0.0f, 1.0f);
}

void UNPCAutonomyComponent::PreparePlayerSpeechStartle()
{
	if (bStartlePreparedForCurrentInteraction)
	{
		return;
	}
	bStartlePreparedForCurrentInteraction = true;
	bPendingPlayerSpeechStartle = false;
	PendingStartleFocus = 0.0f;
	PendingStartleUnexpectedness = 0.0f;
	PendingStartleRearFactor = 0.0f;
	PendingStartleExpectedConversation = 0.0f;
	PendingStartleProbability = 0.0f;
	PendingStartleRoll = 1.0f;
	PendingStartleIntensity = 0.0f;
	PendingStartlePlayerDistance = -1.0f;

	ResolveReferences();
	UWorld* World = GetWorld();
	if (!World || !MaidNPCActor)
	{
		return;
	}

	const double CurrentWorldTime = World->GetTimeSeconds();
	const double SilenceSeconds = LastPlayerSpeechWorldTime < -999999.0
		? PlayerSpeechStartleFullUnexpectednessSeconds
		: FMath::Max(CurrentWorldTime - LastPlayerSpeechWorldTime, 0.0);
	LastPlayerSpeechWorldTime = CurrentWorldTime;

	PendingStartleFocus = CalculateCurrentActivityFocus();
	PendingStartleUnexpectedness = FMath::Clamp(
		static_cast<float>(SilenceSeconds) / FMath::Max(PlayerSpeechStartleFullUnexpectednessSeconds, 0.1f),
		0.0f,
		1.0f);
	PendingStartleExpectedConversation = CalculateExpectedConversationLevel();
	AActor* PlayerActor = nullptr;
	PendingStartleRearFactor = CalculatePlayerRearFactor(PlayerActor, PendingStartlePlayerDistance);
	PendingStartleIntensity = FMath::Clamp(
		PendingStartleFocus * 0.45f
			+ PendingStartleRearFactor * 0.35f
			+ PendingStartleUnexpectedness * 0.2f,
		0.0f,
		1.0f);

	const bool bPlayerInRange = PlayerActor
		&& PendingStartlePlayerDistance >= 0.0f
		&& PendingStartlePlayerDistance <= FMath::Max(PlayerSpeechStartleMaximumDistance, 1.0f);
	const bool bCooldownReady = CurrentWorldTime - LastPlayerSpeechStartleWorldTime
		>= FMath::Max(PlayerSpeechStartleCooldownSeconds, 0.0f);
	if (bPlayerInRange)
	{
		PendingStartleProbability = FMath::Clamp(
			PlayerSpeechStartleBaseChance
				+ PendingStartleFocus * PlayerSpeechStartleFocusWeight
				+ PendingStartleUnexpectedness * PlayerSpeechStartleUnexpectednessWeight
				+ PendingStartleRearFactor * PlayerSpeechStartleRearWeight
				- PendingStartleExpectedConversation * PlayerSpeechStartleExpectedConversationPenalty,
			0.0f,
			FMath::Clamp(PlayerSpeechStartleMaximumChance, 0.0f, 1.0f));
	}

	PendingStartleRoll = FMath::FRand();
	const bool bForcedStartle = bForceNextPlayerSpeechStartle && bPlayerInRange;
	if (bForcedStartle)
	{
		bForceNextPlayerSpeechStartle = false;
	}
	bPendingPlayerSpeechStartle = bEnablePlayerSpeechStartle
		&& bPlayerInRange
		&& (bForcedStartle || (bCooldownReady && PendingStartleRoll <= PendingStartleProbability));
	if (bPendingPlayerSpeechStartle)
	{
		LastPlayerSpeechStartleWorldTime = CurrentWorldTime;
		ApplyExpressionMorphs(
			TEXT("startled"),
			false,
			PlayerSpeechStartleExpressionSeconds * FMath::Lerp(0.8f, 1.3f, PendingStartleIntensity));
	}

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetBoolField(TEXT("triggered"), bPendingPlayerSpeechStartle);
	Payload->SetBoolField(TEXT("forced"), bForcedStartle);
	Payload->SetBoolField(TEXT("cooldown_ready"), bCooldownReady);
	Payload->SetNumberField(TEXT("focus"), PendingStartleFocus);
	Payload->SetNumberField(TEXT("unexpectedness"), PendingStartleUnexpectedness);
	Payload->SetNumberField(TEXT("rear_factor"), PendingStartleRearFactor);
	Payload->SetNumberField(TEXT("expected_conversation"), PendingStartleExpectedConversation);
	Payload->SetNumberField(TEXT("probability"), PendingStartleProbability);
	Payload->SetNumberField(TEXT("roll"), PendingStartleRoll);
	Payload->SetNumberField(TEXT("intensity"), PendingStartleIntensity);
	Payload->SetNumberField(TEXT("player_distance"), PendingStartlePlayerDistance);
	Payload->SetStringField(TEXT("activity"), CurrentActivityID);
	RecordResearchEvent(TEXT("player_speech_startle_evaluated"), Payload);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[PlayerSpeechStartle] Triggered=%s Activity=%s Focus=%.2f Unexpected=%.2f Rear=%.2f Expected=%.2f Probability=%.2f Roll=%.2f Distance=%.1f"),
		bPendingPlayerSpeechStartle ? TEXT("true") : TEXT("false"),
		*CurrentActivityID,
		PendingStartleFocus,
		PendingStartleUnexpectedness,
		PendingStartleRearFactor,
		PendingStartleExpectedConversation,
		PendingStartleProbability,
		PendingStartleRoll,
		PendingStartlePlayerDistance);
}

void UNPCAutonomyComponent::HandleInteractiveLLMRequestStarted()
{
	StopInteractiveFacingPlayer();
	PreparePlayerSpeechStartle();
	bStartlePreparedForCurrentInteraction = false;
	if (bSeatInvitationPending)
	{
		UE_LOG(LogTemp, Log, TEXT("[NPCSeatInvitation] player response received; preserving the pending consent gate."));
		return;
	}

	if (CanPreserveActivityForEmbodiedResponse())
	{
		bAwaitingEmbodiedResponse = true;
		bResumePausedActivityAfterReply = false;
		PendingInteractiveActivityID = CurrentActivityID;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(EmbodiedResponseResumeTimerHandle);
		}

		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("activity"), PendingInteractiveActivityID);
		Payload->SetStringField(TEXT("phase"), TEXT("awaiting_llm_policy"));
		RecordResearchEvent(TEXT("embodied_response_interaction_started"), Payload);
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[EmbodiedResponse] preserving activity until LLM policy arrives. Activity=%s"),
			*PendingInteractiveActivityID);
		return;
	}

	NotifyPlayerInteraction();
}

bool UNPCAutonomyComponent::CanPreserveActivityForEmbodiedResponse() const
{
	return bEnableEmbodiedResponseCoordination
		&& bActivityRunning
		&& (bPreserveAutonomyActivityUntilLLMResponse
			|| (bPreservePhoneActivityUntilLLMResponse
				&& CurrentActivityID.Equals(TEXT("phone_at_counter"), ESearchCase::IgnoreCase)));
}

void UNPCAutonomyComponent::HandleEmbodiedResponseRequested(const FNPCEmbodiedResponseSignal& Signal)
{
	const bool bLocalStartleTriggered = bPendingPlayerSpeechStartle;
	if (!bEnableEmbodiedResponseCoordination)
	{
		if (bAwaitingEmbodiedResponse)
		{
			NotifyPlayerInteraction();
		}
		bPendingPlayerSpeechStartle = false;
		return;
	}

	const FString PreservedActivityID = PendingInteractiveActivityID;
	const bool bPreservedActivityStillRunning = bAwaitingEmbodiedResponse
		&& bActivityRunning
		&& CurrentActivityID.Equals(PreservedActivityID, ESearchCase::IgnoreCase);
	bAwaitingEmbodiedResponse = false;
	PendingInteractiveActivityID = TEXT("");

	FString Policy = Signal.BaseActionPolicy.TrimStartAndEnd().ToLower();
	if (Policy != TEXT("continue")
		&& Policy != TEXT("soft_pause")
		&& Policy != TEXT("stop")
		&& Policy != TEXT("transition"))
	{
		Policy = TEXT("stop");
	}

	const FString PropIntent = Signal.PropIntent.TrimStartAndEnd().ToLower();
	const bool bShushRequested = Signal.LeftHandIntent.TrimStartAndEnd().Equals(TEXT("shush"), ESearchCase::IgnoreCase)
		|| Signal.RightHandIntent.TrimStartAndEnd().Equals(TEXT("shush"), ESearchCase::IgnoreCase);
	if (bPreservedActivityStillRunning
		&& PreservedActivityID.Equals(TEXT("phone_at_counter"), ESearchCase::IgnoreCase)
		&& bShushRequested
		&& (Policy == TEXT("continue") || Policy == TEXT("soft_pause")))
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[EmbodiedResponse] full-body shush requires a transition; phone activity will stop before the gesture."));
		Policy = TEXT("transition");
	}
	if ((PropIntent == TEXT("hide") || PropIntent == TEXT("remove"))
		&& (Policy == TEXT("continue") || Policy == TEXT("soft_pause")))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[EmbodiedResponse] conflict resolved: prop=%s is incompatible with policy=%s; policy changed to transition."),
			*PropIntent,
			*Policy);
		Policy = TEXT("transition");
	}

	bResumePausedActivityAfterReply = false;
	bool bBasePolicyApplied = !bPreservedActivityStillRunning;
	const bool bTerminatesPreservedActivity = bPreservedActivityStillRunning
		&& (Policy == TEXT("stop") || Policy == TEXT("transition"));
	if (bPreservedActivityStillRunning)
	{
		if (bTerminatesPreservedActivity)
		{
			InterruptAutonomy(FString::Printf(TEXT("embodied_response_%s"), *Policy));
			bBasePolicyApplied = true;
		}
		else if (Policy == TEXT("soft_pause"))
		{
			bBasePolicyApplied = ActionQueueComponent && ActionQueueComponent->PauseActiveQueuedAnimation();
			bResumePausedActivityAfterReply = bBasePolicyApplied && Signal.bResumeAfterReply;
			if (bResumePausedActivityAfterReply)
			{
				if (!Signal.bHasSpokenReply)
				{
					ResumePausedActivityAfterInteraction();
				}
				else if (UWorld* World = GetWorld())
				{
					World->GetTimerManager().SetTimer(
						EmbodiedResponseResumeTimerHandle,
						this,
						&UNPCAutonomyComponent::ResumePausedActivityAfterInteraction,
						FMath::Max(EmbodiedResponseResumeFallbackSeconds, 1.0f),
						false);
				}
			}
		}
		else
		{
			bBasePolicyApplied = true;
		}
	}

	const bool bAllowBodyRotation = !bPreservedActivityStillRunning || bTerminatesPreservedActivity;
	bool bGestureHandledInternally = false;
	if (Signal.bEnabled)
	{
		const FString FaceIntent = Signal.FaceIntent.TrimStartAndEnd().ToLower();
		if (bLocalStartleTriggered)
		{
			const FString StartleFaceIntent = FaceIntent == TEXT("startled_blush")
				? TEXT("startled_blush")
				: TEXT("startled");
			HandleSocialExpressionRequested(
				StartleFaceIntent,
				TEXT("player speech startled the NPC"),
				PlayerSpeechStartleExpressionSeconds * FMath::Lerp(0.8f, 1.3f, PendingStartleIntensity));
		}
		else if (!FaceIntent.IsEmpty() && FaceIntent != TEXT("neutral"))
		{
			HandleSocialExpressionRequested(FaceIntent, Signal.ReactionStyle, 8.0f);
		}
		else if (FaceIntent == TEXT("neutral"))
		{
			HandleSocialExpressionRequested(TEXT("neutral"), Signal.ReactionStyle, 0.1f);
		}
		ApplyEmbodiedGazeIntent(Signal.GazeIntent, bAllowBodyRotation);
		bGestureHandledInternally = TryExecuteInternalEmbodiedGesture(Signal, Policy);

		auto DispatchHandGesture = [this, &PreservedActivityID, bGestureHandledInternally](const FString& Channel, const FString& Gesture)
		{
			const FString NormalizedGesture = Gesture.TrimStartAndEnd().ToLower();
			if (NormalizedGesture.IsEmpty()
				|| NormalizedGesture == TEXT("none")
				|| NormalizedGesture == TEXT("keep_current"))
			{
				return;
			}
			if (bGestureHandledInternally && NormalizedGesture == TEXT("shush"))
			{
				return;
			}

			if (PreservedActivityID.Equals(TEXT("phone_at_counter"), ESearchCase::IgnoreCase)
				&& Channel == TEXT("right_hand")
				&& NormalizedGesture == TEXT("shush"))
			{
				UE_LOG(LogTemp, Warning, TEXT("[EmbodiedResponse] right-hand shush rejected because the phone occupies that hand."));
				return;
			}

			OnEmbodiedGestureRequested.Broadcast(Channel, NormalizedGesture);
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[EmbodiedResponse] gesture requested. Channel=%s Tag=%s Bound=%s"),
				*Channel,
				*NormalizedGesture,
				OnEmbodiedGestureRequested.IsBound() ? TEXT("true") : TEXT("false"));
		};
		DispatchHandGesture(TEXT("left_hand"), Signal.LeftHandIntent);
		DispatchHandGesture(TEXT("right_hand"), Signal.RightHandIntent);
	}
	else if (bLocalStartleTriggered)
	{
		HandleSocialExpressionRequested(
			TEXT("startled"),
			TEXT("player speech startled the NPC"),
			PlayerSpeechStartleExpressionSeconds * FMath::Lerp(0.8f, 1.3f, PendingStartleIntensity));
	}

	if (bTerminatesPreservedActivity)
	{
		ApplyEmbodiedGazeIntent(TEXT("player"), true);
	}
	if (bLocalStartleTriggered && bTerminatesPreservedActivity && !bGestureHandledInternally)
	{
		bGestureHandledInternally = TryExecuteStandaloneStartleReaction();
	}

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("activity"), PreservedActivityID);
	Payload->SetStringField(TEXT("reaction_style"), Signal.ReactionStyle);
	Payload->SetStringField(TEXT("base_action_policy"), Policy);
	Payload->SetStringField(TEXT("face"), Signal.FaceIntent);
	Payload->SetStringField(TEXT("gaze"), Signal.GazeIntent);
	Payload->SetStringField(TEXT("left_hand"), Signal.LeftHandIntent);
	Payload->SetStringField(TEXT("right_hand"), Signal.RightHandIntent);
	Payload->SetStringField(TEXT("voice"), Signal.VoiceIntent);
	Payload->SetStringField(TEXT("prop"), Signal.PropIntent);
	Payload->SetBoolField(TEXT("base_policy_applied"), bBasePolicyApplied);
	Payload->SetBoolField(TEXT("gesture_animation_applied"), bGestureHandledInternally);
	Payload->SetBoolField(TEXT("local_startle_triggered"), bLocalStartleTriggered);
	Payload->SetBoolField(TEXT("body_rotation_allowed"), bAllowBodyRotation);
	Payload->SetBoolField(TEXT("faced_player_after_stopping"), bTerminatesPreservedActivity);
	Payload->SetBoolField(TEXT("resume_after_reply"), bResumePausedActivityAfterReply);
	Payload->SetStringField(TEXT("reason"), Signal.Reason);
	RecordResearchEvent(TEXT("embodied_response_resolved"), Payload);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[EmbodiedResponse] resolved. Activity=%s Policy=%s Applied=%s Face=%s Gaze=%s Voice=%s Resume=%s"),
		*PreservedActivityID,
		*Policy,
		bBasePolicyApplied ? TEXT("true") : TEXT("false"),
		*Signal.FaceIntent,
		*Signal.GazeIntent,
		*Signal.VoiceIntent,
		bResumePausedActivityAfterReply ? TEXT("true") : TEXT("false"));
	bPendingPlayerSpeechStartle = false;
}

void UNPCAutonomyComponent::HandleInteractiveReplyFinished()
{
	ResumePausedActivityAfterInteraction();
	StopInteractiveFacingPlayer();
}

void UNPCAutonomyComponent::ResumePausedActivityAfterInteraction()
{
	if (!bResumePausedActivityAfterReply)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EmbodiedResponseResumeTimerHandle);
	}
	bResumePausedActivityAfterReply = false;
	const bool bResumed = bActivityRunning
		&& ActionQueueComponent
		&& ActionQueueComponent->ResumeActiveQueuedAnimation();
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[EmbodiedResponse] resume after reply. Activity=%s Resumed=%s"),
		*CurrentActivityID,
		bResumed ? TEXT("true") : TEXT("false"));
}

void UNPCAutonomyComponent::ApplyEmbodiedGazeIntent(const FString& GazeIntent, bool bAllowBodyRotation)
{
	if (!MaidNPCActor)
	{
		return;
	}

	const FString NormalizedGaze = GazeIntent.TrimStartAndEnd().ToLower();
	if (NormalizedGaze != TEXT("player") && NormalizedGaze != TEXT("alternate_phone_player"))
	{
		return;
	}
	if (!bAllowBodyRotation)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[EmbodiedResponse] player-facing body rotation suppressed because the current activity continues."));
		return;
	}

	UNPCFaceTargetComponent* FaceTargetComponent = MaidNPCActor->FindComponentByClass<UNPCFaceTargetComponent>();
	AActor* PlayerActor = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (FaceTargetComponent && PlayerActor)
	{
		FaceTargetComponent->StartFacingActor(PlayerActor);
		bInteractiveFacingPlayer = true;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(InteractiveFacingFallbackTimerHandle);
			World->GetTimerManager().SetTimer(
				InteractiveFacingFallbackTimerHandle,
				this,
				&UNPCAutonomyComponent::StopInteractiveFacingPlayer,
				FMath::Max(InteractiveFacingFallbackSeconds, 1.0f),
				false);
		}
	}
}

void UNPCAutonomyComponent::StopInteractiveFacingPlayer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(InteractiveFacingFallbackTimerHandle);
	}

	if (!bInteractiveFacingPlayer)
	{
		return;
	}

	bInteractiveFacingPlayer = false;
	if (MaidNPCActor)
	{
		if (UNPCFaceTargetComponent* FaceTargetComponent = MaidNPCActor->FindComponentByClass<UNPCFaceTargetComponent>())
		{
			FaceTargetComponent->StopFacingActor();
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[EmbodiedResponse] released temporary player-facing target."));
}

bool UNPCAutonomyComponent::TryExecuteStandaloneStartleReaction()
{
	if (!ActionQueueComponent || ActionQueueComponent->bIsRunningQueue)
	{
		return false;
	}

	UAnimSequenceBase* StartleAnimation = StartledReactionAnimation.LoadSynchronous();
	if (!IsAnimationCompatibleWithMaid(StartleAnimation))
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlayerSpeechStartle] full-body startle animation is missing or incompatible."));
		return false;
	}

	ActionQueueComponent->ClearActionQueue();
	ActionQueueComponent->EnqueueAnimation(
		StartleAnimation,
		FName(TEXT("DefaultSlot")),
		1.0f,
		2.0f);
	ActionQueueComponent->StartActionQueue();
	const bool bStarted = ActionQueueComponent->bIsRunningQueue;
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[PlayerSpeechStartle] standalone full-body reaction started=%s"),
		bStarted ? TEXT("true") : TEXT("false"));
	return bStarted;
}

bool UNPCAutonomyComponent::HasInternalEmbodiedGestureExecutor() const
{
	return bEnableInternalEmbodiedGestureAnimations && !ShushGestureAnimation.IsNull();
}

bool UNPCAutonomyComponent::TryExecuteInternalEmbodiedGesture(
	const FNPCEmbodiedResponseSignal& Signal,
	const FString& AppliedPolicy)
{
	if (!HasInternalEmbodiedGestureExecutor() || !ActionQueueComponent)
	{
		return false;
	}

	const bool bShushRequested = Signal.LeftHandIntent.TrimStartAndEnd().Equals(TEXT("shush"), ESearchCase::IgnoreCase)
		|| Signal.RightHandIntent.TrimStartAndEnd().Equals(TEXT("shush"), ESearchCase::IgnoreCase);
	if (!bShushRequested)
	{
		return false;
	}
	if (AppliedPolicy != TEXT("transition") && AppliedPolicy != TEXT("stop"))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[EmbodiedResponse] internal shush skipped because policy %s keeps the base full-body animation."),
			*AppliedPolicy);
		return false;
	}
	if (ActionQueueComponent->bIsRunningQueue)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbodiedResponse] internal shush skipped because the action queue is busy."));
		return false;
	}

	UAnimSequenceBase* ShushAnimation = ShushGestureAnimation.LoadSynchronous();
	if (!IsAnimationCompatibleWithMaid(ShushAnimation))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbodiedResponse] shush animation is missing or incompatible with the maid skeleton."));
		return false;
	}

	ActionQueueComponent->ClearActionQueue();
	const FString FaceIntent = Signal.FaceIntent.TrimStartAndEnd().ToLower();
	const FString ReactionStyle = Signal.ReactionStyle.TrimStartAndEnd().ToLower();
	const bool bPlayStartledLeadIn = FaceIntent == TEXT("startled_blush")
		|| ReactionStyle.Contains(TEXT("startl"))
		|| ReactionStyle.Contains(TEXT("embarrass"));
	if (bPlayStartledLeadIn)
	{
		UAnimSequenceBase* StartledAnimation = StartledReactionAnimation.LoadSynchronous();
		if (IsAnimationCompatibleWithMaid(StartledAnimation))
		{
			ActionQueueComponent->EnqueueAnimation(
				StartledAnimation,
				FName(TEXT("DefaultSlot")),
				1.0f,
				2.0f);
		}
	}
	ActionQueueComponent->EnqueueAnimation(
		ShushAnimation,
		FName(TEXT("DefaultSlot")),
		1.0f,
		6.0f);
	ActionQueueComponent->StartActionQueue();

	const bool bStarted = ActionQueueComponent->bIsRunningQueue;
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[EmbodiedResponse] internal shush sequence started. StartledLeadIn=%s Started=%s"),
		bPlayStartledLeadIn ? TEXT("true") : TEXT("false"),
		bStarted ? TEXT("true") : TEXT("false"));
	return bStarted;
}

void UNPCAutonomyComponent::HandleSocialExpressionRequested(
	const FString& ExpressionIntent,
	const FString& ExpressionDescription,
	float DurationSeconds)
{
	const FString NormalizedIntent = ExpressionIntent.TrimStartAndEnd().ToLower();
	if (NormalizedIntent.IsEmpty())
	{
		return;
	}
	if (NormalizedIntent == TEXT("blink"))
	{
		StartAutomaticBlink();
		UE_LOG(LogTemp, Log, TEXT("[AutoBlink] manual blink requested by dialogue command."));
	}
	else if (NormalizedIntent == TEXT("neutral"))
	{
		ResetExpressionMorphs();
	}
	else
	{
		ApplyExpressionMorphs(NormalizedIntent, false, FMath::Max(DurationSeconds, 0.1f));
	}
	OnNPCExpressionIntentChanged.Broadcast(NormalizedIntent, CurrentEmotionLabel);

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("expression_intent"), NormalizedIntent);
	Payload->SetStringField(TEXT("description"), ExpressionDescription);
	Payload->SetNumberField(TEXT("duration_seconds"), DurationSeconds);
	const bool bPerformanceApplied = NormalizedIntent == TEXT("blink")
		? !BlinkMorphNames.IsEmpty()
		: (NormalizedIntent == TEXT("neutral") || bExpressionOverlayActive);
	Payload->SetBoolField(TEXT("morph_applied"), bPerformanceApplied);
	RecordResearchEvent(TEXT("social_expression_applied"), Payload);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[ExpressionOverlay] social expression requested. Intent=%s Description=%s Applied=%s Duration=%.1fs"),
		*NormalizedIntent,
		*ExpressionDescription,
		bPerformanceApplied ? TEXT("true") : TEXT("false"),
		DurationSeconds);
}

void UNPCAutonomyComponent::HandleInteractionMemoryExtracted(const FNPCInteractionMemorySignal& Signal)
{
	if (!Signal.bShouldRemember || !MemoryComponent)
	{
		return;
	}

	TArray<FName> CueTags = Signal.CueTags;
	CueTags.AddUnique(FName(TEXT("player")));
	CueTags.AddUnique(FName(TEXT("dialogue")));
	CueTags.AddUnique(FName(TEXT("service")));
	const FString PlayerZone = ResolvePlayerZone().TrimStartAndEnd().ToLower();
	if (!PlayerZone.IsEmpty())
	{
		CueTags.AddUnique(FName(*PlayerZone));
	}

	FString VoiceEmotion = UEmotionClient::GetLatestPlayerVoiceEmotion(30.0f).TrimStartAndEnd().ToLower();
	if (!VoiceEmotion.IsEmpty() && !VoiceEmotion.Equals(TEXT("unknown"), ESearchCase::IgnoreCase))
	{
		VoiceEmotion.ReplaceInline(TEXT(" "), TEXT("_"));
		CueTags.AddUnique(FName(*FString::Printf(TEXT("voice_emotion_%s"), *VoiceEmotion)));
		PlayerContext.Emotion = VoiceEmotion;
	}

	const FName EventTag = Signal.EventTag.IsNone()
		? FName(TEXT("player_meaningful_interaction"))
		: Signal.EventTag;
	const FString Summary = Signal.Summary.TrimStartAndEnd().IsEmpty()
		? FString::Printf(TEXT("A meaningful customer interaction occurred: %s"), *Signal.PlayerUtterance)
		: Signal.Summary.TrimStartAndEnd();
	MemoryComponent->RecordMemoryEvent(
		EventTag,
		Summary,
		PlayerZone.IsEmpty() ? FName(TEXT("unknown")) : FName(*PlayerZone),
		CueTags,
		Signal.Valence,
		Signal.Arousal,
		Signal.Importance,
		false);

	PlayerContext.RecentEvent = EventTag.ToString();
	PlayerContext.RecentEvents.Add(EventTag.ToString());
	while (PlayerContext.RecentEvents.Num() > 4)
	{
		PlayerContext.RecentEvents.RemoveAt(0);
	}
	ScheduleNextDecision(DelayAfterPlayerInteractionSeconds);

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("event_tag"), EventTag.ToString());
	Payload->SetStringField(TEXT("summary"), Summary);
	Payload->SetStringField(TEXT("player_utterance"), Signal.PlayerUtterance);
	Payload->SetStringField(TEXT("voice_emotion"), VoiceEmotion.IsEmpty() ? TEXT("unknown") : VoiceEmotion);
	Payload->SetNumberField(TEXT("valence"), Signal.Valence);
	Payload->SetNumberField(TEXT("arousal"), Signal.Arousal);
	Payload->SetNumberField(TEXT("importance"), Signal.Importance);
	RecordResearchEvent(TEXT("interaction_memory_extracted"), Payload);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[MemoryEmotion] meaningful dialogue stored. Event=%s VoiceEmotion=%s Importance=%.2f"),
		*EventTag.ToString(),
		*VoiceEmotion,
		Signal.Importance);
}

void UNPCAutonomyComponent::HandleMultiStepValidationAutoInterrupt()
{
	UE_LOG(LogTemp, Log, TEXT("[AutonomyPlanTest] simulating player interaction interrupt."));
	NotifyPlayerInteraction();
}

void UNPCAutonomyComponent::HandleServiceStateChanged(ECafeServiceState NewState)
{
	if (!bHasEncounteredPlayer && ServiceManager && ServiceManager->CurrentPlayerActor)
	{
		bHasEncounteredPlayer = true;
		UE_LOG(LogTemp, Log, TEXT("[AutonomyPlanTest] player encounter observed; visible validation is now enabled."));
	}
	if (!IsServiceStateAutonomyAllowed(NewState))
	{
		ResourceState.Energy = FMath::Clamp(
			ResourceState.Energy - FMath::Max(ServiceTransitionEnergyCost, 0.0f),
			0.0f,
			100.0f);
		InterruptAutonomy(TEXT("service_priority"));
		return;
	}
	if (NewState == ECafeServiceState::Served
		&& !FindAvailableSeatOppositePlayerID().IsEmpty())
	{
		MarkMeaningfulContextChange(TEXT("player_seated_social_opportunity"));
		ScheduleNextDecision(InitialIdleDelaySeconds);
		UE_LOG(LogTemp, Log, TEXT("[NPCSeatInvitation] social seating opportunity queued after service."));
		return;
	}

	if (!bActivityRunning && !bDecisionPending)
	{
		ScheduleNextDecision(
			ActivePlan.Status == ENPCAutonomyPlanStatus::Suspended
				? DelayAfterPlayerInteractionSeconds
				: InitialIdleDelaySeconds);
	}
}

void UNPCAutonomyComponent::HandleActionQueueFinished(bool bSuccess)
{
	if (bSeatInvitationApproachInProgress)
	{
		if (bSuccess)
		{
			PresentSeatInvitation();
		}
		else
		{
			CancelSeatInvitation(TEXT("approach_failed"), true, false);
		}
		return;
	}
	if (!bActivityRunning || bInterruptingActivity)
	{
		return;
	}
	if (RuntimePhase == ENPCAutonomyRuntimePhase::EmotionReaction)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[MemoryEmotion] visible reaction queue finished. Success=%s"),
			bSuccess ? TEXT("true") : TEXT("false"));
		bActivityRunning = false;
		bCurrentActivityIsAmbient = false;
		CurrentActivityID = TEXT("");
		ActiveActivityAnimation = nullptr;
		RuntimePhase = ENPCAutonomyRuntimePhase::None;
		if (const UWorld* World = GetWorld())
		{
			LastActivityEndWorldTime = World->GetTimeSeconds();
		}
		ContinuePlanningAfterAppraisal();
		return;
	}

	const FString FinishedActivityID = CurrentActivityID;
	const FNPCAutonomyActivityDefinition* Activity = FindActivityDefinition(FinishedActivityID);
	if (!Activity)
	{
		FinishActivity(true);
		return;
	}

	switch (RuntimePhase)
	{
	case ENPCAutonomyRuntimePhase::BuiltInQueue:
		if (bSuccess)
		{
			ApplyActivitySuccess(*Activity);
			if (Activity->bReturnToCounterAfterCompletion)
			{
				LastKnownNPCZone = TEXT("counter");
			}
		}
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[Autonomy] built-in activity finished. Activity=%s Success=%s"),
			*FinishedActivityID,
			bSuccess ? TEXT("true") : TEXT("false"));
		FinishActivity(!bSuccess);
		break;

	case ENPCAutonomyRuntimePhase::BlueprintPreMove:
		if (!bSuccess)
		{
			FinishActivity(true);
			break;
		}
		if (!Activity->TargetSceneTag.TrimStartAndEnd().IsEmpty())
		{
			LastKnownNPCZone = Activity->TargetSceneTag.TrimStartAndEnd();
		}
		BeginBlueprintActivityExecution(*Activity);
		break;

	case ENPCAutonomyRuntimePhase::BlueprintReturnMove:
		if (bSuccess)
		{
			LastKnownNPCZone = TEXT("counter");
		}
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[Autonomy] blueprint activity return finished. Activity=%s Success=%s"),
			*FinishedActivityID,
			bSuccess ? TEXT("true") : TEXT("false"));
		FinishActivity(!bSuccess);
		break;

	default:
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[Autonomy] action queue finished in an unexpected phase. Activity=%s"),
			*FinishedActivityID);
		FinishActivity(true);
		break;
	}
}

void UNPCAutonomyComponent::HandleAutonomyDecisionReceived(
	int32 RequestId,
	const FString& SelectedAction,
	const FString& Reason,
	const FString& RawJson)
{
	if (!bDecisionPending || RequestId != PendingDecisionRequestId)
	{
		return;
	}

	bDecisionPending = false;
	PendingDecisionRequestId = 0;
	if (AutonomyLLMClient)
	{
		TotalAutonomyPromptTokens += AutonomyLLMClient->LastAutonomyPromptTokens;
		TotalAutonomyCompletionTokens += AutonomyLLMClient->LastAutonomyCompletionTokens;
		TotalAutonomyCachedPromptTokens += AutonomyLLMClient->LastAutonomyCachedPromptTokens;
	}
	TSharedRef<FJsonObject> DecisionPayload = MakeShared<FJsonObject>();
	DecisionPayload->SetStringField(TEXT("trigger"), LastDecisionTrigger);
	DecisionPayload->SetStringField(TEXT("selected_action"), SelectedAction);
	DecisionPayload->SetStringField(TEXT("reason"), Reason);
	DecisionPayload->SetStringField(TEXT("raw_json"), RawJson);
	DecisionPayload->SetNumberField(
		TEXT("prompt_tokens"),
		AutonomyLLMClient ? AutonomyLLMClient->LastAutonomyPromptTokens : 0);
	DecisionPayload->SetNumberField(
		TEXT("completion_tokens"),
		AutonomyLLMClient ? AutonomyLLMClient->LastAutonomyCompletionTokens : 0);
	DecisionPayload->SetNumberField(
		TEXT("cached_prompt_tokens"),
		AutonomyLLMClient ? AutonomyLLMClient->LastAutonomyCachedPromptTokens : 0);
	RecordResearchEvent(TEXT("plan_decision_received"), DecisionPayload);

	if (!CanRunAutonomy())
	{
		ScheduleNextDecision(DelayAfterPlayerInteractionSeconds);
		return;
	}

	FString Decision;
	FString Goal;
	TArray<FString> ProposedSteps;
	FString Commitment;
	FString SurfaceThought;
	if (!ParsePlanDecision(
		SelectedAction,
		RawJson,
		Decision,
		Goal,
		ProposedSteps,
		Commitment,
		SurfaceThought))
	{
		UE_LOG(LogTemp, Warning, TEXT("[AutonomyPlan] could not parse decision. Raw=%s"), *RawJson);
		ScheduleNextDecision(DecisionErrorRetrySeconds);
		return;
	}

	if (Decision.Equals(TEXT("wait"), ESearchCase::IgnoreCase) || ProposedSteps.IsEmpty())
	{
		ClearActivePlan(TEXT("llm_wait"));
		LastDecisionReason = Reason;
		RollNextBoredomThreshold();
		ScheduleNextDecision(WaitDecisionCooldownSeconds);
		UE_LOG(LogTemp, Log, TEXT("[AutonomyPlan] LLM chose wait. Reason=%s"), *Reason);
		return;
	}

	TArray<FString> ValidatedSteps;
	for (const FString& ProposedStep : ProposedSteps)
	{
		const FString StepID = ProposedStep.TrimStartAndEnd();
		const FNPCAutonomyActivityDefinition* Activity = FindActivityDefinition(StepID);
		const bool bAlreadyIncluded = ValidatedSteps.ContainsByPredicate([&StepID](const FString& ExistingStep)
		{
			return ExistingStep.Equals(StepID, ESearchCase::IgnoreCase);
		});
		if (bAlreadyIncluded)
		{
			continue;
		}
		if (!IsActionOffered(StepID) || !Activity || !IsActivityAvailable(*Activity))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[AutonomyPlan] rejected unavailable plan step. Action=%s Raw=%s"),
				*StepID,
				*RawJson);
			ScheduleNextDecision(DecisionErrorRetrySeconds);
			return;
		}
		ValidatedSteps.Add(Activity->ActivityID.TrimStartAndEnd());
	}

	if (ValidatedSteps.IsEmpty())
	{
		ScheduleNextDecision(DecisionErrorRetrySeconds);
		return;
	}

	FString TaskPlanValidationError;
	if (!ValidateTaskPlanStructure(ValidatedSteps, TaskPlanValidationError))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[AutonomyPlan] rejected malformed task plan. Error=%s Raw=%s"),
			*TaskPlanValidationError,
			*RawJson);
		MarkMeaningfulContextChange(TEXT("invalid_task_plan_retry"));
		ScheduleNextDecision(DecisionErrorRetrySeconds);
		return;
	}

	LastDecisionReason = Reason;
	ActivePlan.Goal = Goal.IsEmpty() ? Reason : Goal;
	ActivePlan.Steps = ValidatedSteps;
	ActivePlan.CurrentStepIndex = 0;
	ActivePlan.Commitment = Commitment;
	ActivePlan.Trigger = LastDecisionTrigger;
	ActivePlan.Status = ENPCAutonomyPlanStatus::Active;
	++TotalPlansCreated;
	if (SurfaceThought.IsEmpty())
	{
		SurfaceThought = BuildFallbackPlanSurfaceThought(ValidatedSteps);
	}
	const bool bPlanFollowsMemory = LastDecisionTrigger.StartsWith(
		TEXT("memory_appraised"),
		ESearchCase::IgnoreCase);
	SchedulePlanSurfaceThought(
		SurfaceThought,
		bPlanFollowsMemory ? PlanThoughtDelayAfterMemorySeconds : 0.0f);

	OnAutonomyDecisionMade.Broadcast(ValidatedSteps[0], Reason);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[AutonomyPlan] created. Trigger=%s Goal=%s Commitment=%s Tone=%s SurfaceThought=%s Steps=%s"),
		*LastDecisionTrigger,
		*ActivePlan.Goal,
		*ActivePlan.Commitment,
		*ResolveCurrentPerformanceTone(),
		*SurfaceThought,
		*FString::Join(ActivePlan.Steps, TEXT(",")));

	if (!TryStartCurrentPlanStep())
	{
		ClearActivePlan(TEXT("first_step_failed_to_start"));
		MarkMeaningfulContextChange(TEXT("plan_start_failed"));
		ScheduleNextDecision(DecisionErrorRetrySeconds);
	}
}

void UNPCAutonomyComponent::HandleAutonomyDecisionError(int32 RequestId, const FString& ErrorMessage)
{
	if (!bDecisionPending || RequestId != PendingDecisionRequestId)
	{
		return;
	}

	bDecisionPending = false;
	PendingDecisionRequestId = 0;
	if (AutonomyLLMClient)
	{
		TotalAutonomyPromptTokens += AutonomyLLMClient->LastAutonomyPromptTokens;
		TotalAutonomyCompletionTokens += AutonomyLLMClient->LastAutonomyCompletionTokens;
		TotalAutonomyCachedPromptTokens += AutonomyLLMClient->LastAutonomyCachedPromptTokens;
	}
	if (DecisionMode == ENPCAutonomyDecisionMode::EventTriggeredShortPlan)
	{
		MarkMeaningfulContextChange(LastDecisionTrigger.IsEmpty() ? TEXT("decision_error_retry") : LastDecisionTrigger);
	}
	UE_LOG(LogTemp, Warning, TEXT("[Autonomy] decision failed: %s"), *ErrorMessage);
	ScheduleNextDecision(DecisionErrorRetrySeconds);
}

void UNPCAutonomyComponent::HandleMemoryAppraisalReceived(int32 RequestId, const FString& RawJson)
{
	if (!bAppraisalPending || RequestId != PendingAppraisalRequestId)
	{
		return;
	}

	bAppraisalPending = false;
	PendingAppraisalRequestId = 0;
	if (AutonomyLLMClient)
	{
		TotalAppraisalPromptTokens += AutonomyLLMClient->LastAppraisalPromptTokens;
		TotalAppraisalCompletionTokens += AutonomyLLMClient->LastAppraisalCompletionTokens;
	}
	TSharedRef<FJsonObject> AppraisalPayload = MakeShared<FJsonObject>();
	AppraisalPayload->SetStringField(TEXT("memory_id"), PendingAppraisalMemory.MemoryID);
	AppraisalPayload->SetStringField(TEXT("memory_event"), PendingAppraisalMemory.EventTag.ToString());
	AppraisalPayload->SetStringField(TEXT("raw_json"), RawJson);
	AppraisalPayload->SetNumberField(
		TEXT("prompt_tokens"),
		AutonomyLLMClient ? AutonomyLLMClient->LastAppraisalPromptTokens : 0);
	AppraisalPayload->SetNumberField(
		TEXT("completion_tokens"),
		AutonomyLLMClient ? AutonomyLLMClient->LastAppraisalCompletionTokens : 0);
	AppraisalPayload->SetNumberField(
		TEXT("cached_prompt_tokens"),
		AutonomyLLMClient ? AutonomyLLMClient->LastAppraisalCachedPromptTokens : 0);
	RecordResearchEvent(TEXT("appraisal_received"), AppraisalPayload);

	TSharedPtr<FJsonObject> AppraisalObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawJson);
	FString EmotionLabel;
	FString ExpressionIntent;
	FString Interpretation;
	FString SurfaceThought;
	double ValenceDelta = 0.0;
	double ArousalDelta = 0.0;
	double MoodDelta = 0.0;
	const bool bValidAppraisal = FJsonSerializer::Deserialize(Reader, AppraisalObject)
		&& AppraisalObject.IsValid()
		&& AppraisalObject->TryGetStringField(TEXT("emotion"), EmotionLabel)
		&& AppraisalObject->TryGetNumberField(TEXT("valence_delta"), ValenceDelta)
		&& AppraisalObject->TryGetNumberField(TEXT("arousal_delta"), ArousalDelta)
		&& AppraisalObject->TryGetNumberField(TEXT("mood_delta"), MoodDelta)
		&& AppraisalObject->TryGetStringField(TEXT("expression_intent"), ExpressionIntent)
		&& AppraisalObject->TryGetStringField(TEXT("interpretation"), Interpretation);

	if (!bValidAppraisal || !EmotionComponent)
	{
		const FString OriginalTrigger = PendingPlanningTriggerAfterAppraisal;
		PendingPlanningTriggerAfterAppraisal = TEXT("");
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[MemoryEmotion] invalid appraisal response; continuing to planning. Raw=%s"),
			*RawJson);
		MarkMeaningfulContextChange(FString::Printf(TEXT("appraisal_failed:%s"), *OriginalTrigger));
		ScheduleNextDecision(0.0f);
		return;
	}

	static const TArray<FString> AllowedExpressionIntents = {
		TEXT("neutral"), TEXT("smile"), TEXT("hum"), TEXT("sigh"), TEXT("alert"), TEXT("pensive")
	};
	ExpressionIntent = ExpressionIntent.TrimStartAndEnd().ToLower();
	if (!AllowedExpressionIntents.Contains(ExpressionIntent))
	{
		ExpressionIntent = TEXT("neutral");
	}
	AppraisalObject->TryGetStringField(TEXT("thought"), SurfaceThought);

	EmotionComponent->ApplyAppraisal(
		EmotionLabel,
		static_cast<float>(FMath::Clamp(ValenceDelta, -1.0, 1.0)),
		static_cast<float>(FMath::Clamp(ArousalDelta, -1.0, 1.0)),
		static_cast<float>(FMath::Clamp(MoodDelta, -0.3, 0.3)),
		ExpressionIntent,
		Interpretation);

	const FNPCEmotionState EmotionState = EmotionComponent->GetCurrentState();
	LastAppraisedMemorySummary = PendingAppraisalMemory.Summary;
	LastAppraisalInterpretation = Interpretation.TrimStartAndEnd();
	CurrentEmotionLabel = EmotionState.EmotionLabel;
	CurrentExpressionIntent = EmotionState.ExpressionIntent;
	OnNPCExpressionIntentChanged.Broadcast(CurrentExpressionIntent, CurrentEmotionLabel);

	SurfaceThought = SurfaceThought.TrimStartAndEnd();
	const bool bWrappedInFullWidthParens = SurfaceThought.Len() >= 2
		&& SurfaceThought.StartsWith(TEXT("（"))
		&& SurfaceThought.EndsWith(TEXT("）"));
	const bool bWrappedInAsciiParens = SurfaceThought.Len() >= 2
		&& SurfaceThought.StartsWith(TEXT("("))
		&& SurfaceThought.EndsWith(TEXT(")"));
	if (bWrappedInFullWidthParens || bWrappedInAsciiParens)
	{
		SurfaceThought = SurfaceThought.Mid(1, SurfaceThought.Len() - 2).TrimStartAndEnd();
	}
	if (SurfaceThought.IsEmpty())
	{
		if (CurrentExpressionIntent == TEXT("smile") || CurrentExpressionIntent == TEXT("hum"))
		{
			SurfaceThought = TEXT("あの時のことを思い出すと、少し嬉しくなるな。");
		}
		else if (CurrentExpressionIntent == TEXT("sigh"))
		{
			SurfaceThought = TEXT("あの時のことを思い出すと、少し気持ちが沈むな。");
		}
		else if (CurrentExpressionIntent == TEXT("alert"))
		{
			SurfaceThought = TEXT("あの時のことが、まだ少し気にかかるな。");
		}
		else if (CurrentExpressionIntent == TEXT("pensive"))
		{
			SurfaceThought = TEXT("あの時のことを、もう少し考えてみよう。");
		}
		else
		{
			SurfaceThought = TEXT("ふと、あの時のことを思い出した。");
		}
	}

	bool bThoughtDisplayed = false;
	if (ULLMClient* DisplayClient = ULLMClient::GetMostRecentInteractiveClient())
	{
		DisplayClient->ShowSilentDisplayText(
			FString::Printf(TEXT("（心の声：%s）"), *SurfaceThought));
		bThoughtDisplayed = true;
	}
	TSharedRef<FJsonObject> ThoughtPayload = MakeShared<FJsonObject>();
	ThoughtPayload->SetStringField(TEXT("memory_id"), PendingAppraisalMemory.MemoryID);
	ThoughtPayload->SetStringField(TEXT("memory_event"), PendingAppraisalMemory.EventTag.ToString());
	ThoughtPayload->SetStringField(TEXT("thought"), SurfaceThought);
	ThoughtPayload->SetBoolField(TEXT("displayed"), bThoughtDisplayed);
	RecordResearchEvent(TEXT("memory_thought_presented"), ThoughtPayload);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[MemoryThought] presented=%s text=%s"),
		bThoughtDisplayed ? TEXT("true") : TEXT("false"),
		*SurfaceThought);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[MemoryEmotion] appraisal completed. OriginalTrigger=%s Memory=%s Emotion=%s Expression=%s"),
		*PendingPlanningTriggerAfterAppraisal,
		*PendingAppraisalMemory.EventTag.ToString(),
		*CurrentEmotionLabel,
		*CurrentExpressionIntent);
	if (!TryStartVisibleMemoryReaction())
	{
		ContinuePlanningAfterAppraisal();
	}
}

void UNPCAutonomyComponent::HandleMemoryAppraisalError(int32 RequestId, const FString& ErrorMessage)
{
	if (!bAppraisalPending || RequestId != PendingAppraisalRequestId)
	{
		return;
	}

	bAppraisalPending = false;
	PendingAppraisalRequestId = 0;
	if (AutonomyLLMClient)
	{
		TotalAppraisalPromptTokens += AutonomyLLMClient->LastAppraisalPromptTokens;
		TotalAppraisalCompletionTokens += AutonomyLLMClient->LastAppraisalCompletionTokens;
	}
	TSharedRef<FJsonObject> ErrorPayload = MakeShared<FJsonObject>();
	ErrorPayload->SetStringField(TEXT("memory_id"), PendingAppraisalMemory.MemoryID);
	ErrorPayload->SetStringField(TEXT("memory_event"), PendingAppraisalMemory.EventTag.ToString());
	ErrorPayload->SetStringField(TEXT("error"), ErrorMessage);
	RecordResearchEvent(TEXT("appraisal_error"), ErrorPayload);

	const FString OriginalTrigger = PendingPlanningTriggerAfterAppraisal;
	PendingPlanningTriggerAfterAppraisal = TEXT("");
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[MemoryEmotion] appraisal failed; continuing to planning. Trigger=%s Error=%s"),
		*OriginalTrigger,
		*ErrorMessage);
	MarkMeaningfulContextChange(FString::Printf(TEXT("appraisal_failed:%s"), *OriginalTrigger));
	ScheduleNextDecision(0.0f);
}
