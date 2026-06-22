// Copyright Epic Games, Inc. All Rights Reserved.

#include "InteractableInterface.h"

bool IInteractableInterface::CanInteract_Implementation(AActor* Interactor) const
{
	return true;
}

FText IInteractableInterface::GetInteractionText_Implementation(AActor* Interactor) const
{
	return FText::GetEmpty();
}

bool IInteractableInterface::Interact_Implementation(AActor* Interactor)
{
	return false;
}
