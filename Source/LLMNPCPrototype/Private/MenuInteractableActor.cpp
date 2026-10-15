// Copyright Epic Games, Inc. All Rights Reserved.

#include "MenuInteractableActor.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MenuUIControllerComponent.h"
#include "PlayerSeatComponent.h"

AMenuInteractableActor::AMenuInteractableActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBox"));
	InteractionBox->SetupAttachment(SceneRoot);
	InteractionBox->SetBoxExtent(FVector(80.0f, 40.0f, 80.0f));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBox->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionBox->SetGenerateOverlapEvents(false);

	MenuUIController = CreateDefaultSubobject<UMenuUIControllerComponent>(TEXT("MenuUIController"));

	MenuID = TEXT("Menu_01");
	RelatedTableID = TEXT("Table_01");
	bRequiresSeated = false;
}

bool AMenuInteractableActor::CanInteract_Implementation(AActor* Interactor) const
{
	if (!bRequiresSeated)
	{
		return true;
	}

	const UPlayerSeatComponent* SeatComponent = Interactor ? Interactor->FindComponentByClass<UPlayerSeatComponent>() : nullptr;
	return SeatComponent && SeatComponent->bIsSeated;
}

FText AMenuInteractableActor::GetInteractionText_Implementation(AActor* Interactor) const
{
	return FText::FromString(TEXT("F メニューを見る"));
}

bool AMenuInteractableActor::Interact_Implementation(AActor* Interactor)
{
	if (!CanInteract_Implementation(Interactor))
	{
		return false;
	}

	const UPlayerSeatComponent* SeatComponent = Interactor ? Interactor->FindComponentByClass<UPlayerSeatComponent>() : nullptr;
	const FString PlayerState = SeatComponent ? SeatComponent->GetPlayerPostureState() : TEXT("standing");
	const FString SeatID = SeatComponent ? SeatComponent->CurrentSeatID : TEXT("");
	const FString CurrentTableID = SeatComponent ? SeatComponent->CurrentTableID : TEXT("");

	PrintDebugMessage(FString::Printf(TEXT("Open menu: %s"), *MenuID), FColor::Cyan);

	const FString ContextMessage = FString::Printf(
		TEXT("Menu context | state=%s seat=%s table=%s menu_table=%s menu=%s"),
		*PlayerState,
		*SeatID,
		*CurrentTableID,
		*RelatedTableID,
		*MenuID);

	PrintDebugMessage(ContextMessage, FColor::Cyan);
	UE_LOG(LogTemp, Log, TEXT("%s"), *ContextMessage);

	APlayerController* PlayerController = nullptr;
	if (const APawn* InteractorPawn = Cast<APawn>(Interactor))
	{
		PlayerController = Cast<APlayerController>(InteractorPawn->GetController());
	}

	if (!PlayerController)
	{
		PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	}

	if (MenuUIController)
	{
		MenuUIController->SetMenuText(TEXT("カレーライス\nラーメン\nサラダ\nコーヒー"));
		MenuUIController->ToggleMenuUI(PlayerController);
	}

	return true;
}

void AMenuInteractableActor::PrintDebugMessage(const FString& Message, const FColor& Color) const
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, Color, Message);
	}
}
