// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerInteractionComponent.h"

#include "Camera/CameraComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "InteractableInterface.h"
#include "MenuUIControllerComponent.h"
#include "PlayerSeatComponent.h"

UPlayerInteractionComponent::UPlayerInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	InteractionDistance = 300.0f;
	bUseCameraRay = true;
	bDrawDebugInteractionRay = false;
	CurrentFocusedActor = nullptr;
	CurrentInteractionText = FText::GetEmpty();
	bHasFocusedInteractable = false;
}

void UPlayerInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	RefreshFocusedInteractable();
}

bool UPlayerInteractionComponent::TryInteract()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	if (UMenuUIControllerComponent::IsAnyMenuOpen())
	{
		UMenuUIControllerComponent::HideActiveMenuUI();
		SetFocusedInteractable(nullptr, FText::GetEmpty(), false);
		return true;
	}

	if (IsValid(CurrentFocusedActor) && CurrentFocusedActor->GetClass()->ImplementsInterface(UInteractableInterface::StaticClass()))
	{
		if (IInteractableInterface::Execute_CanInteract(CurrentFocusedActor, Owner))
		{
			return IInteractableInterface::Execute_Interact(CurrentFocusedActor, Owner);
		}

		PrintDebugMessage(TEXT("Cannot interact now"), FColor::Yellow);
		return false;
	}

	UPlayerSeatComponent* SeatComponent = Owner->FindComponentByClass<UPlayerSeatComponent>();
	if (SeatComponent && SeatComponent->bIsSeated)
	{
		return SeatComponent->LeaveSeat();
	}

	PrintDebugMessage(TEXT("No interactable target"), FColor::Yellow);
	return false;
}

FText UPlayerInteractionComponent::GetCurrentInteractionText() const
{
	return CurrentInteractionText;
}

bool UPlayerInteractionComponent::HasFocusedInteractable() const
{
	return bHasFocusedInteractable;
}

void UPlayerInteractionComponent::RefreshFocusedInteractable()
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		SetFocusedInteractable(nullptr, FText::GetEmpty(), false);
		return;
	}

	if (UMenuUIControllerComponent::IsAnyMenuOpen())
	{
		const bool bHasPromptText = !CurrentInteractionText.EqualTo(FText::GetEmpty());
		if (bHasFocusedInteractable || CurrentFocusedActor || bHasPromptText)
		{
			UE_LOG(LogTemp, Log, TEXT("[InteractPrompt] Hidden because menu UI is open."));
		}

		SetFocusedInteractable(nullptr, FText::GetEmpty(), false);
		return;
	}

	FVector TraceStart = FVector::ZeroVector;
	FVector TraceEnd = FVector::ZeroVector;
	if (!GetInteractionTrace(TraceStart, TraceEnd))
	{
		SetFocusedInteractable(nullptr, FText::GetEmpty(), false);
		return;
	}

	FHitResult HitResult;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PlayerInteractionTrace), false, Owner);
	const bool bHit = World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams);

	AActor* HitActor = bHit ? HitResult.GetActor() : nullptr;
	const bool bHitInteractable = HitActor && HitActor->GetClass()->ImplementsInterface(UInteractableInterface::StaticClass());

	if (bDrawDebugInteractionRay)
	{
		const FColor DebugColor = !bHit
			? FColor::Red
			: (bHitInteractable ? FColor::Green : FColor::Yellow);
		DrawDebugLine(World, TraceStart, TraceEnd, DebugColor, false, 0.0f, 0, 1.5f);
	}

	if (bHitInteractable)
	{
		SetFocusedInteractable(
			HitActor,
			IInteractableInterface::Execute_GetInteractionText(HitActor, Owner),
			true);
		return;
	}

	SetFocusedInteractable(nullptr, FText::GetEmpty(), false);
}

void UPlayerInteractionComponent::SetFocusedInteractable(AActor* NewFocusedActor, const FText& NewInteractionText, bool bNewHasFocusedInteractable)
{
	const bool bFocusChanged = CurrentFocusedActor != NewFocusedActor
		|| bHasFocusedInteractable != bNewHasFocusedInteractable
		|| !CurrentInteractionText.EqualTo(NewInteractionText);

	CurrentFocusedActor = NewFocusedActor;
	CurrentInteractionText = NewInteractionText;
	bHasFocusedInteractable = bNewHasFocusedInteractable;

	if (bFocusChanged)
	{
		OnInteractionFocusChanged.Broadcast(CurrentFocusedActor, CurrentInteractionText, bHasFocusedInteractable);
	}
}

bool UPlayerInteractionComponent::GetInteractionTrace(FVector& OutTraceStart, FVector& OutTraceEnd) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || InteractionDistance <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	FRotator TraceRotation = Owner->GetActorRotation();
	OutTraceStart = Owner->GetActorLocation();

	if (bUseCameraRay)
	{
		bool bHasViewPoint = false;

		if (const APawn* OwnerPawn = Cast<APawn>(Owner))
		{
			if (AController* Controller = OwnerPawn->GetController())
			{
				Controller->GetPlayerViewPoint(OutTraceStart, TraceRotation);
				bHasViewPoint = true;
			}
		}

		if (!bHasViewPoint)
		{
			if (const UCameraComponent* CameraComponent = Owner->FindComponentByClass<UCameraComponent>())
			{
				OutTraceStart = CameraComponent->GetComponentLocation();
				TraceRotation = CameraComponent->GetComponentRotation();
			}
		}
	}

	OutTraceEnd = OutTraceStart + TraceRotation.Vector() * InteractionDistance;
	return true;
}

void UPlayerInteractionComponent::PrintDebugMessage(const FString& Message, const FColor& Color) const
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, Color, Message);
	}
}
