// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterablePlantActor.h"

#include "CafeServiceManagerActor.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "NPCAutonomyComponent.h"
#include "NPCResearchLoggerComponent.h"
#include "PlayerSeatComponent.h"
#include "PlantMoistureBarWidget.h"

namespace
{
	// Console multiplier for the moisture decay (0 freezes decay, 10 = ten times faster). Debug only.
	float GCafePlantDecayScale = 1.0f;
	FAutoConsoleVariableRef CVarCafePlantDecayScale(
		TEXT("Cafe.Plant.DecayScale"),
		GCafePlantDecayScale,
		TEXT("Multiplier for plant moisture decay. 0 freezes decay, 10 = ten times faster."),
		ECVF_Default);

	AWaterablePlantActor* FindPlantByID(UWorld* World, const FString& PlantID)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<AWaterablePlantActor> It(World); It; ++It)
		{
			AWaterablePlantActor* Plant = *It;
			if (Plant && Plant->PlantID.Equals(PlantID, ESearchCase::IgnoreCase))
			{
				return Plant;
			}
		}
		return nullptr;
	}

	UNPCAutonomyComponent* FindAutonomyComponent(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<ACafeServiceManagerActor> It(World); It; ++It)
		{
			if (UNPCAutonomyComponent* Autonomy = (*It)->FindComponentByClass<UNPCAutonomyComponent>())
			{
				return Autonomy;
			}
		}
		return nullptr;
	}

	void PrintPlantConsoleLine(const FString& Line)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlantConsole] %s"), *Line);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 8.0f, FColor::Cyan, Line);
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GCafePlantListCommand(
	TEXT("Cafe.Plant.List"),
	TEXT("List every waterable plant: moisture, tier, needs-water state, reservation."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		int32 Count = 0;
		for (TActorIterator<AWaterablePlantActor> It(World); It; ++It)
		{
			const AWaterablePlantActor* Plant = *It;
			if (!Plant)
			{
				continue;
			}
			++Count;
			PrintPlantConsoleLine(FString::Printf(
				TEXT("%s moisture=%.1f tier=%s needs=%d reserved=%s enabled=%d prio=%d"),
				*Plant->PlantID,
				Plant->GetMoisture(),
				*Plant->GetMoistureTierName(),
				Plant->NeedsWater() ? 1 : 0,
				Plant->IsReserved() ? *Plant->GetReservedByName() : TEXT("-"),
				Plant->bEnabled ? 1 : 0,
				Plant->Priority));
		}
		PrintPlantConsoleLine(FString::Printf(TEXT("plants=%d decayScale=%.2f"), Count, GCafePlantDecayScale));
	}));

static FAutoConsoleCommandWithWorldAndArgs GCafePlantSetCommand(
	TEXT("Cafe.Plant.Set"),
	TEXT("Cafe.Plant.Set <PlantID|all> <moisture 0-100>: set moisture (debug)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2)
		{
			PrintPlantConsoleLine(TEXT("usage: Cafe.Plant.Set <PlantID|all> <moisture 0-100>"));
			return;
		}
		const float Moisture = FCString::Atof(*Args[1]);
		int32 Changed = 0;
		for (TActorIterator<AWaterablePlantActor> It(World); It; ++It)
		{
			AWaterablePlantActor* Plant = *It;
			if (Plant && (Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase)
				|| Plant->PlantID.Equals(Args[0], ESearchCase::IgnoreCase)))
			{
				Plant->SetWaterNeed(100.0f - Moisture, false);
				++Changed;
			}
		}
		PrintPlantConsoleLine(FString::Printf(TEXT("set %s moisture=%.1f (plants changed=%d)"), *Args[0], Moisture, Changed));
	}));

static FAutoConsoleCommandWithWorldAndArgs GCafePlantClearCommand(
	TEXT("Cafe.Plant.Clear"),
	TEXT("Cafe.Plant.Clear <PlantID>: explicitly dismiss the needs-water state (debug)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AWaterablePlantActor* Plant = Args.Num() > 0 ? FindPlantByID(World, Args[0]) : nullptr;
		if (!Plant)
		{
			PrintPlantConsoleLine(TEXT("usage: Cafe.Plant.Clear <PlantID> (plant not found)"));
			return;
		}
		Plant->ClearNeedsWater(TEXT("console"));
		PrintPlantConsoleLine(FString::Printf(TEXT("%s needs=%d moisture=%.1f"),
			*Plant->PlantID, Plant->NeedsWater() ? 1 : 0, Plant->GetMoisture()));
	}));

static FAutoConsoleCommandWithWorldAndArgs GCafeConcernsCommand(
	TEXT("Cafe.Concerns"),
	TEXT("Show the NPC's current concerns (things on her mind), their strengths and the planning pressure."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UNPCAutonomyComponent* Autonomy = FindAutonomyComponent(World);
		if (!Autonomy)
		{
			PrintPlantConsoleLine(TEXT("autonomy component not found"));
			return;
		}
		PrintPlantConsoleLine(Autonomy->DescribeConcerns());
	}));

static FAutoConsoleCommandWithWorldAndArgs GCafePlantNpcWaterCommand(
	TEXT("Cafe.Plant.NpcWater"),
	TEXT("Cafe.Plant.NpcWater <PlantID>: make the NPC start watering that plant now (debug)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UNPCAutonomyComponent* Autonomy = FindAutonomyComponent(World);
		if (!Autonomy || Args.Num() < 1)
		{
			PrintPlantConsoleLine(TEXT("usage: Cafe.Plant.NpcWater <PlantID> (autonomy component missing?)"));
			return;
		}
		const bool bStarted = Autonomy->ForceWaterPlantForValidation(Args[0]);
		PrintPlantConsoleLine(FString::Printf(TEXT("NpcWater %s started=%d"), *Args[0], bStarted ? 1 : 0));
	}));

AWaterablePlantActor::AWaterablePlantActor()
{
	PrimaryActorTick.bCanEverTick = true;
	// 0.1 moisture/s decay does not need per-frame work.
	PrimaryActorTick.TickInterval = 0.25f;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	PlantMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlantMesh"));
	PlantMesh->SetupAttachment(SceneRoot);
	PlantMesh->SetCollisionProfileName(TEXT("BlockAll"));
	PlantMesh->SetCanEverAffectNavigation(true);

	FacingArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("FacingArrow"));
	FacingArrow->SetupAttachment(SceneRoot);
	FacingArrow->SetArrowColor(FColor(60, 190, 90));
	FacingArrow->SetArrowSize(1.25f);
	FacingArrow->SetHiddenInGame(true);

	PlantInteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("PlantInteractionBox"));
	PlantInteractionBox->SetupAttachment(SceneRoot);
	PlantInteractionBox->SetBoxExtent(FVector(40.0, 40.0, 40.0));
	PlantInteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PlantInteractionBox->SetCollisionObjectType(ECC_WorldDynamic);
	PlantInteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	PlantInteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PlantInteractionBox->SetGenerateOverlapEvents(false);
	PlantInteractionBox->SetCanEverAffectNavigation(false);

	MoistureBarComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("MoistureBar"));
	MoistureBarComponent->SetupAttachment(SceneRoot);
	MoistureBarComponent->SetWidgetClass(UPlantMoistureBarWidget::StaticClass());
	MoistureBarComponent->SetWidgetSpace(EWidgetSpace::Screen);
	MoistureBarComponent->SetPivot(FVector2D(0.5, 1.0));
	MoistureBarComponent->SetDrawSize(FVector2D(200.0, 26.0));
	MoistureBarComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MoistureBarComponent->SetCastShadow(false);

	bShowMoistureBar = true;
	MoistureBarHeightOffset = 80.0f;
	MoistureBarDrawSize = FVector2D(200.0, 26.0);
	MoistureBarMaxDistance = 1500.0f;
	bMoistureBarOnlyWhenNotFull = false;

	bAllowPlayerWatering = true;
	PlayerWateringMoisturePerSecond = 5.0f;
	PlayerWateringMaxDistance = 400.0f;
	InteractionBoxPadding = 45.0f;
	PlayerWaterPrompt = FText::FromString(TEXT("F 水やり"));
	PlayerStopWaterPrompt = FText::FromString(TEXT("F 水やりをやめる"));

	ObservedPlantActor = nullptr;
	PlantID = TEXT("Plant_01");
	DisplayName = FText::FromString(TEXT("Waterable Plant 01"));
	PlantTags = { TEXT("watering"), TEXT("waterable_plant") };
	WaterNeed = 75.0f;
	NeedsWaterThreshold = 50.0f;
	bEnableMoistureDecay = true;
	MoistureDecayPerSecond = 0.1f;
	NeedsWaterEnterMoisture = 60.0f;
	NeedsWaterClearMoisture = 95.0f;
	bRandomizeInitialMoisture = true;
	bNeedsWater = true;
	bEnabled = true;
	bReserved = false;
	Priority = 0;
	ApproachDistance = 95.0f;
	bOnlyTargetWhenNeedsWater = true;
	bSyncAutonomyWorldState = true;
	AutonomyWorldStateTag = FName(TEXT("flower_needs_water"));
}

void AWaterablePlantActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshDerivedState();

	if (PlantTags.Num() == 0)
	{
		PlantTags = { TEXT("watering"), TEXT("waterable_plant") };
	}
}

void AWaterablePlantActor::BeginPlay()
{
	Super::BeginPlay();
	if (bRandomizeInitialMoisture)
	{
		// Deterministic per-plant stagger so reruns are comparable: moisture 69..100.
		const uint32 Hash = GetTypeHash(PlantID.ToLower());
		const float InitialMoisture = 100.0f - static_cast<float>(Hash % 31u);
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[WaterablePlant] initial moisture staggered. ID=%s Placed=%.1f -> %.1f"),
			*PlantID,
			100.0f - WaterNeed,
			InitialMoisture);
		WaterNeed = 100.0f - InitialMoisture;
	}
	RefreshDerivedState();
	ReminderTier = ComputeReminderTier(GetMoisture());
	LastBroadcastMoisture = GetMoisture();
	RefreshAutonomyWorldState(false);
	RefreshTargetBounds();
	PlaceMoistureBar();
	UpdateMoistureBar();

	AActor* TargetActor = GetPlantTargetActor();
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[WaterablePlant] Registered ID=%s Marker=%s Target=%s Location=%s Moisture=%.1f NeedsWater=%s Priority=%d"),
		*PlantID,
		*GetName(),
		TargetActor ? *TargetActor->GetName() : TEXT("None"),
		*GetPlantTargetLocation().ToCompactString(),
		GetMoisture(),
		bNeedsWater ? TEXT("true") : TEXT("false"),
		Priority);
}

AActor* AWaterablePlantActor::GetPlantTargetActor() const
{
	return IsValid(ObservedPlantActor) && ObservedPlantActor != this
		? ObservedPlantActor
		: const_cast<AWaterablePlantActor*>(this);
}

FVector AWaterablePlantActor::GetPlantTargetLocation() const
{
	AActor* TargetActor = GetPlantTargetActor();
	if (!TargetActor)
	{
		return GetActorLocation();
	}

	FVector BoundsCenter = TargetActor->GetActorLocation();
	FVector BoundsExtent = FVector::ZeroVector;
	TargetActor->GetActorBounds(false, BoundsCenter, BoundsExtent);
	return BoundsCenter;
}

bool AWaterablePlantActor::HasTagString(const FString& Tag) const
{
	const FString TrimmedTag = Tag.TrimStartAndEnd();
	if (TrimmedTag.IsEmpty())
	{
		return false;
	}

	return PlantTags.ContainsByPredicate([&TrimmedTag](const FString& PlantTag)
	{
		return PlantTag.TrimStartAndEnd().Equals(TrimmedTag, ESearchCase::IgnoreCase);
	});
}

bool AWaterablePlantActor::NeedsWater() const
{
	return bEnabled && bNeedsWater;
}

bool AWaterablePlantActor::IsAvailableForTag(const FString& Tag) const
{
	if (!bEnabled || IsReserved() || !HasTagString(Tag))
	{
		return false;
	}

	return !bOnlyTargetWhenNeedsWater || NeedsWater();
}

void AWaterablePlantActor::SetWaterNeed(float NewWaterNeed, bool bMeaningfulChange)
{
	ApplyMoisture(100.0f - FMath::Clamp(NewWaterNeed, 0.0f, 100.0f), bMeaningfulChange, TEXT("set"));
}

void AWaterablePlantActor::AddWaterNeed(float Amount, bool bMeaningfulChange)
{
	SetWaterNeed(WaterNeed + Amount, bMeaningfulChange);
}

void AWaterablePlantActor::WaterPlant(float Amount, bool bMeaningfulChange)
{
	// Instant refill kept for blueprints and debug. NPC and player watering use AddMoisture over time.
	SetWaterNeed(WaterNeed - FMath::Max(Amount, 0.0f), bMeaningfulChange);
}

void AWaterablePlantActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bEnabled && bEnableMoistureDecay && GCafePlantDecayScale > 0.0f)
	{
		ApplyMoisture(
			GetMoisture() - FMath::Max(MoistureDecayPerSecond, 0.0f) * GCafePlantDecayScale * DeltaSeconds,
			false,
			TEXT("decay"));
	}
	UpdatePlayerWatering(DeltaSeconds);
	// Also runs when decay is frozen: watering and the camera distance still change what the bar shows.
	UpdateMoistureBar();
}

void AWaterablePlantActor::RefreshTargetBounds()
{
	// The interaction box must not count itself when the bounds are measured.
	if (PlantInteractionBox)
	{
		PlantInteractionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	TargetBoundsOrigin = GetActorLocation();
	TargetBoundsExtent = FVector::ZeroVector;
	if (const AActor* TargetActor = GetPlantTargetActor())
	{
		TargetActor->GetActorBounds(true, TargetBoundsOrigin, TargetBoundsExtent);
	}

	if (PlantInteractionBox)
	{
		PlantInteractionBox->SetWorldRotation(FRotator::ZeroRotator);
		PlantInteractionBox->SetWorldScale3D(FVector::OneVector);
		PlantInteractionBox->SetWorldLocation(TargetBoundsOrigin);
		PlantInteractionBox->SetBoxExtent(FVector(
			FMath::Max(TargetBoundsExtent.X + InteractionBoxPadding, 40.0),
			FMath::Max(TargetBoundsExtent.Y + InteractionBoxPadding, 40.0),
			FMath::Max(TargetBoundsExtent.Z + InteractionBoxPadding, 40.0)));
		PlantInteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
}

bool AWaterablePlantActor::IsPlayerWatering() const
{
	return bPlayerWatering;
}

double AWaterablePlantActor::GetPlayerDistanceToPlant(const AActor* Player) const
{
	if (!Player)
	{
		return TNumericLimits<double>::Max();
	}
	const double Horizontal = FVector::Dist2D(Player->GetActorLocation(), TargetBoundsOrigin);
	return FMath::Max(0.0, Horizontal - FMath::Max(TargetBoundsExtent.X, TargetBoundsExtent.Y));
}

bool AWaterablePlantActor::IsPlayerSeated(const AActor* Player) const
{
	const UPlayerSeatComponent* SeatComponent = Player ? Player->FindComponentByClass<UPlayerSeatComponent>() : nullptr;
	return SeatComponent && SeatComponent->bIsSeated;
}

bool AWaterablePlantActor::CanInteract_Implementation(AActor* Interactor) const
{
	if (!bAllowPlayerWatering || !bEnabled || !Interactor)
	{
		return false;
	}
	if (bPlayerWatering)
	{
		// While watering, the same key always stops it.
		return PlayerWaterer.Get() == Interactor;
	}
	return !IsPlayerSeated(Interactor)
		&& GetPlayerDistanceToPlant(Interactor) <= PlayerWateringMaxDistance
		&& GetMoisture() + KINDA_SMALL_NUMBER < 100.0f;
}

FText AWaterablePlantActor::GetInteractionText_Implementation(AActor* Interactor) const
{
	if (!CanInteract_Implementation(Interactor))
	{
		return FText::GetEmpty();
	}
	return bPlayerWatering ? PlayerStopWaterPrompt : PlayerWaterPrompt;
}

bool AWaterablePlantActor::Interact_Implementation(AActor* Interactor)
{
	if (!CanInteract_Implementation(Interactor))
	{
		return false;
	}
	if (bPlayerWatering)
	{
		StopPlayerWatering(TEXT("player_toggle"));
	}
	else
	{
		StartPlayerWatering(Interactor);
	}
	return true;
}

void AWaterablePlantActor::StartPlayerWatering(AActor* Waterer)
{
	bPlayerWatering = true;
	PlayerWaterer = Waterer;
	PlayerWateringStartMoisture = GetMoisture();
	PlayerWateringStartedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	// Smooth bar while watering; back to the slow tick when it stops.
	SetActorTickInterval(0.05f);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[PlayerWatering] started. Plant=%s Moisture=%.1f Rate=%.1f/s"),
		*PlantID,
		PlayerWateringStartMoisture,
		PlayerWateringMoisturePerSecond);
	UNPCResearchLoggerComponent::RecordGlobalEvent(
		TEXT("player_watering_started"),
		FString::Printf(TEXT("{\"plant_id\":\"%s\",\"moisture\":%.1f}"), *PlantID, PlayerWateringStartMoisture));
}

void AWaterablePlantActor::StopPlayerWatering(const TCHAR* Reason)
{
	if (!bPlayerWatering)
	{
		return;
	}
	bPlayerWatering = false;
	PlayerWaterer.Reset();
	SetActorTickInterval(0.25f);

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : PlayerWateringStartedAt;
	const float Added = GetMoisture() - PlayerWateringStartMoisture;
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[PlayerWatering] stopped. Plant=%s Reason=%s Moisture=%.1f Added=%.1f Duration=%.1fs"),
		*PlantID,
		Reason,
		GetMoisture(),
		Added,
		Now - PlayerWateringStartedAt);
	UNPCResearchLoggerComponent::RecordGlobalEvent(
		TEXT("player_watering_stopped"),
		FString::Printf(
			TEXT("{\"plant_id\":\"%s\",\"reason\":\"%s\",\"moisture\":%.1f,\"added\":%.1f}"),
			*PlantID,
			Reason,
			GetMoisture(),
			Added));
}

void AWaterablePlantActor::UpdatePlayerWatering(float DeltaSeconds)
{
	if (!bPlayerWatering)
	{
		return;
	}

	const AActor* Waterer = PlayerWaterer.Get();
	if (!Waterer)
	{
		StopPlayerWatering(TEXT("player_gone"));
		return;
	}
	if (!bEnabled || !bAllowPlayerWatering)
	{
		StopPlayerWatering(TEXT("disabled"));
		return;
	}
	if (IsPlayerSeated(Waterer))
	{
		StopPlayerWatering(TEXT("player_seated"));
		return;
	}
	if (GetPlayerDistanceToPlant(Waterer) > PlayerWateringMaxDistance)
	{
		StopPlayerWatering(TEXT("left_area"));
		return;
	}

	AddMoisture(FMath::Max(PlayerWateringMoisturePerSecond, 0.0f) * DeltaSeconds, TEXT("player_watering"));
	if (GetMoisture() + KINDA_SMALL_NUMBER >= 100.0f)
	{
		StopPlayerWatering(TEXT("full"));
	}
}

void AWaterablePlantActor::PlaceMoistureBar()
{
	if (!MoistureBarComponent)
	{
		return;
	}
	MoistureBarComponent->SetDrawSize(MoistureBarDrawSize);
	MoistureBarComponent->SetWorldLocation(
		FVector(TargetBoundsOrigin.X, TargetBoundsOrigin.Y, TargetBoundsOrigin.Z + TargetBoundsExtent.Z + MoistureBarHeightOffset));
}

void AWaterablePlantActor::UpdateMoistureBar()
{
	if (!MoistureBarComponent)
	{
		return;
	}

	const float Moisture = GetMoisture();
	bool bVisible = bShowMoistureBar && bEnabled;
	if (bVisible && bMoistureBarOnlyWhenNotFull && Moisture + KINDA_SMALL_NUMBER >= NeedsWaterClearMoisture)
	{
		bVisible = false;
	}
	if (bVisible && MoistureBarMaxDistance > 0.0f)
	{
		if (const UWorld* World = GetWorld())
		{
			if (const APlayerController* PlayerController = World->GetFirstPlayerController())
			{
				FVector CameraLocation = FVector::ZeroVector;
				FRotator CameraRotation = FRotator::ZeroRotator;
				PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
				bVisible = FVector::Dist(CameraLocation, MoistureBarComponent->GetComponentLocation()) <= MoistureBarMaxDistance;
			}
		}
	}
	if (MoistureBarComponent->IsVisible() != bVisible)
	{
		MoistureBarComponent->SetVisibility(bVisible);
	}
	if (!bVisible)
	{
		return;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (LastBarMoisture > -999.0f && Moisture > LastBarMoisture + 0.05f)
	{
		// Moisture is rising: someone is watering it. Blue until it stops rising for a moment.
		BarWateringHighlightUntil = Now + 1.2;
	}
	LastBarMoisture = Moisture;

	FLinearColor FillColor(0.25f, 0.8f, 0.3f, 1.0f);
	if (Now < BarWateringHighlightUntil)
	{
		FillColor = FLinearColor(0.3f, 0.6f, 1.0f, 1.0f);
	}
	else if (Moisture < NeedsWaterEnterMoisture - 40.0f)
	{
		FillColor = FLinearColor(0.9f, 0.2f, 0.2f, 1.0f);
	}
	else if (Moisture < NeedsWaterEnterMoisture - 20.0f)
	{
		FillColor = FLinearColor(0.95f, 0.5f, 0.15f, 1.0f);
	}
	else if (Moisture < NeedsWaterEnterMoisture)
	{
		FillColor = FLinearColor(0.95f, 0.8f, 0.2f, 1.0f);
	}

	if (UPlantMoistureBarWidget* BarWidget = Cast<UPlantMoistureBarWidget>(MoistureBarComponent->GetUserWidgetObject()))
	{
		BarWidget->SetBarState(GetMoisture01(), FillColor);
	}
}

float AWaterablePlantActor::GetMoisture() const
{
	return FMath::Clamp(100.0f - WaterNeed, 0.0f, 100.0f);
}

float AWaterablePlantActor::GetMoisture01() const
{
	return GetMoisture() / 100.0f;
}

FString AWaterablePlantActor::GetMoistureTierName() const
{
	const float Moisture = GetMoisture();
	if (Moisture + KINDA_SMALL_NUMBER >= NeedsWaterClearMoisture)
	{
		return TEXT("full");
	}
	if (Moisture + KINDA_SMALL_NUMBER >= NeedsWaterEnterMoisture)
	{
		return TEXT("ok");
	}
	if (Moisture + KINDA_SMALL_NUMBER >= NeedsWaterEnterMoisture - 20.0f)
	{
		return TEXT("low");
	}
	if (Moisture + KINDA_SMALL_NUMBER >= NeedsWaterEnterMoisture - 40.0f)
	{
		return TEXT("dry");
	}
	return TEXT("critical");
}

float AWaterablePlantActor::GetWaterUrgency01() const
{
	if (!bEnabled || !bNeedsWater)
	{
		return 0.0f;
	}
	const float Enter = FMath::Max(NeedsWaterEnterMoisture, 1.0f);
	return FMath::Max(FMath::Clamp((Enter - GetMoisture()) / Enter, 0.0f, 1.0f), 0.05f);
}

float AWaterablePlantActor::AddMoisture(float Amount, const TCHAR* Source)
{
	ApplyMoisture(GetMoisture() + Amount, false, Source);
	return GetMoisture();
}

void AWaterablePlantActor::ClearNeedsWater(const FString& Reason)
{
	if (!bNeedsWater)
	{
		return;
	}

	bNeedsWater = false;
	DismissedReenterMoisture = GetMoisture() - 10.0f;
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[WaterablePlant] needs-water dismissed. ID=%s Moisture=%.1f Reason=%s ReenterBelow=%.1f"),
		*PlantID,
		GetMoisture(),
		*Reason,
		FMath::Min(NeedsWaterEnterMoisture, DismissedReenterMoisture));
	OnPlantStateChanged.Broadcast(this, WaterNeed, bNeedsWater);
	RefreshAutonomyWorldState(false);
}

bool AWaterablePlantActor::IsReserved() const
{
	// A runtime reservation whose owner is gone no longer blocks the plant.
	return bReserved && (!bReservedByRuntime || ReservedBy.IsValid());
}

bool AWaterablePlantActor::TryReserve(AActor* Waterer)
{
	if (!Waterer)
	{
		return false;
	}
	if (IsReserved() && ReservedBy.Get() != Waterer)
	{
		return false;
	}

	bReserved = true;
	bReservedByRuntime = true;
	ReservedBy = Waterer;
	return true;
}

void AWaterablePlantActor::ReleaseReservation(AActor* Waterer)
{
	if (!bReserved)
	{
		return;
	}
	if (bReservedByRuntime && Waterer && ReservedBy.IsValid() && ReservedBy.Get() != Waterer)
	{
		return;
	}

	bReserved = false;
	bReservedByRuntime = false;
	ReservedBy.Reset();
}

FString AWaterablePlantActor::GetReservedByName() const
{
	if (!bReserved)
	{
		return FString();
	}
	return ReservedBy.IsValid() ? ReservedBy->GetName() : FString(TEXT("editor"));
}

void AWaterablePlantActor::ApplyMoisture(float NewMoisture, bool bMeaningfulChange, const TCHAR* Source)
{
	const float PreviousMoisture = GetMoisture();
	const bool bPreviouslyNeededWater = bNeedsWater;
	WaterNeed = FMath::Clamp(100.0f - NewMoisture, 0.0f, 100.0f);
	UpdateNeedsWaterState();

	const float CurrentMoisture = GetMoisture();
	const bool bFlipped = bPreviouslyNeededWater != bNeedsWater;
	if (bFlipped || FMath::Abs(CurrentMoisture - LastBroadcastMoisture) >= 1.0f)
	{
		LastBroadcastMoisture = CurrentMoisture;
		OnPlantStateChanged.Broadcast(this, WaterNeed, bNeedsWater);
	}

	if (bFlipped)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[WaterablePlant] needs-water %s. ID=%s Moisture=%.1f Source=%s"),
			bNeedsWater ? TEXT("ENTERED") : TEXT("CLEARED"),
			*PlantID,
			CurrentMoisture,
			Source);
		RefreshAutonomyWorldState(bMeaningfulChange);
	}

	const int32 PreviousTier = ReminderTier;
	ReminderTier = ComputeReminderTier(CurrentMoisture);
	if (bEnabled && bNeedsWater && CurrentMoisture < PreviousMoisture && ReminderTier > PreviousTier)
	{
		NotifyMoistureReminder(ReminderTier, CurrentMoisture);
	}
}

void AWaterablePlantActor::UpdateNeedsWaterState()
{
	const float Moisture = GetMoisture();
	const float Enter = FMath::Min(NeedsWaterEnterMoisture, NeedsWaterClearMoisture);
	if (Moisture + KINDA_SMALL_NUMBER >= Enter)
	{
		// Healthy again: any earlier explicit dismissal has run its course.
		DismissedReenterMoisture = TNumericLimits<float>::Max();
	}

	if (!bNeedsWater)
	{
		if (Moisture < FMath::Min(Enter, DismissedReenterMoisture))
		{
			bNeedsWater = true;
		}
	}
	else if (Moisture + KINDA_SMALL_NUMBER >= NeedsWaterClearMoisture)
	{
		bNeedsWater = false;
	}
}

int32 AWaterablePlantActor::ComputeReminderTier(float Moisture) const
{
	if (Moisture < NeedsWaterEnterMoisture - 40.0f)
	{
		return 3;
	}
	if (Moisture < NeedsWaterEnterMoisture - 20.0f)
	{
		return 2;
	}
	if (Moisture < NeedsWaterEnterMoisture)
	{
		return 1;
	}
	return 0;
}

void AWaterablePlantActor::NotifyMoistureReminder(int32 Tier, float Moisture) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const int32 Threshold = FMath::RoundToInt(NeedsWaterEnterMoisture - 20.0f * static_cast<float>(Tier - 1));
	for (TActorIterator<ACafeServiceManagerActor> It(World); It; ++It)
	{
		if (UNPCAutonomyComponent* Autonomy = (*It)->FindComponentByClass<UNPCAutonomyComponent>())
		{
			Autonomy->NotifyPlantMoistureReminder(PlantID, Moisture, Threshold);
		}
	}
}

void AWaterablePlantActor::RefreshAutonomyWorldState(bool bMeaningfulChange) const
{
	if (!bSyncAutonomyWorldState || AutonomyWorldStateTag.IsNone())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bool bAnyPlantNeedsWater = false;
	for (TActorIterator<AWaterablePlantActor> It(World); It; ++It)
	{
		const AWaterablePlantActor* Plant = *It;
		if (Plant
			&& Plant->bSyncAutonomyWorldState
			&& Plant->AutonomyWorldStateTag == AutonomyWorldStateTag
			&& Plant->NeedsWater())
		{
			bAnyPlantNeedsWater = true;
			break;
		}
	}

	for (TActorIterator<ACafeServiceManagerActor> It(World); It; ++It)
	{
		ACafeServiceManagerActor* ServiceManager = *It;
		if (ServiceManager)
		{
			if (UNPCAutonomyComponent* Autonomy = ServiceManager->FindComponentByClass<UNPCAutonomyComponent>())
			{
				Autonomy->SetWorldStateTag(AutonomyWorldStateTag, bAnyPlantNeedsWater, bMeaningfulChange);
			}
		}
	}
}

void AWaterablePlantActor::RefreshDerivedState()
{
	WaterNeed = FMath::Clamp(WaterNeed, 0.0f, 100.0f);
	NeedsWaterThreshold = FMath::Clamp(NeedsWaterThreshold, 0.0f, 100.0f);
	NeedsWaterEnterMoisture = FMath::Clamp(NeedsWaterEnterMoisture, 0.0f, 100.0f);
	NeedsWaterClearMoisture = FMath::Clamp(NeedsWaterClearMoisture, NeedsWaterEnterMoisture, 100.0f);
	// Construction / BeginPlay seed: no hysteresis memory yet, so use the enter line.
	bNeedsWater = GetMoisture() < NeedsWaterEnterMoisture;
}
