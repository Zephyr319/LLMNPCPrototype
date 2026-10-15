// Copyright Epic Games, Inc. All Rights Reserved.

#include "NPCFoodCarryComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"

UNPCFoodCarryComponent::UNPCFoodCarryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	HandSocketName = FName(TEXT("FoodCarrySocket"));
	PreferredMeshComponentName = NAME_None;
	HandAttachLocationOffset = FVector::ZeroVector;
	HandAttachRotationOffset = FRotator::ZeroRotator;
	TablePlaceOffset = FVector(0.0f, 0.0f, 85.0f);
	CounterPlaceOffset = FVector(75.0f, 0.0f, 90.0f);
	CarriedFoodActor = nullptr;
	CarriedFoodItemName = TEXT("");
	bIsCarryingFood = false;
	LastPlacedFoodActor = nullptr;
	LastPlacedTableActor = nullptr;
	LastPlacedFoodItemName = TEXT("");
}

bool UNPCFoodCarryComponent::SpawnAndAttachFoodToHand(const FString& ItemName)
{
	const FString TrimmedItemName = ItemName.TrimStartAndEnd();
	AActor* Owner = GetOwner();
	UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] SpawnAndAttachFoodToHand Item=%s"), *TrimmedItemName);
	UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Owner=%s"), Owner ? *Owner->GetName() : TEXT("None"));

	if (CarriedFoodActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Existing carried food cleared before attaching new item. Previous=%s"), *CarriedFoodItemName);
		ClearCarriedFood();
	}

	TSubclassOf<AActor> FoodActorClass = ResolveFoodActorClass(TrimmedItemName);
	if (!FoodActorClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] No food actor class configured for item=%s"), *TrimmedItemName);
		return false;
	}

	USkeletalMeshComponent* MeshComponent = ResolveOwnerMesh();
	if (!MeshComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Skeletal mesh component not found for food carry."));
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Mesh=%s"), *MeshComponent->GetName());
	UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] HandSocketName=%s"), *HandSocketName.ToString());

	const bool bSocketExists = MeshComponent->DoesSocketExist(HandSocketName);
	UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Socket exists=%s"), bSocketExists ? TEXT("true") : TEXT("false"));

	USceneComponent* AttachComponent = nullptr;
	FName AttachSocketName = NAME_None;
	FTransform AttachWorldTransform;

	if (bSocketExists)
	{
		AttachComponent = MeshComponent;
		AttachSocketName = HandSocketName;
		AttachWorldTransform = MeshComponent->GetSocketTransform(HandSocketName, RTS_World);
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Socket world location=%s"), *AttachWorldTransform.GetLocation().ToString());
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Using hand socket: %s"), *HandSocketName.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Socket world location=Unavailable"));
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Hand socket not found, trying FoodCarryPoint fallback."));
		AttachComponent = FindFoodCarryPoint();
		if (AttachComponent)
		{
			AttachWorldTransform = AttachComponent->GetComponentTransform();
			UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Attached food to FoodCarryPoint."));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Cannot attach food: no socket and no FoodCarryPoint."));
			return false;
		}
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] World not found while spawning food."));
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = Owner;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* SpawnedFoodActor = World->SpawnActor<AActor>(FoodActorClass, AttachWorldTransform, SpawnParameters);
	if (!SpawnedFoodActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Failed to spawn food actor. Item=%s"), *TrimmedItemName);
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Food actor spawned=%s"), *SpawnedFoodActor->GetName());
	UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Food initial world location=%s"), *SpawnedFoodActor->GetActorLocation().ToString());

	SetFoodCollisionEnabled(SpawnedFoodActor, ECollisionEnabled::NoCollision);
	const bool bAttachResult = SpawnedFoodActor->AttachToComponent(
		AttachComponent,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		AttachSocketName);
	UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Attach result=%s"), bAttachResult ? TEXT("true") : TEXT("false"));

	if (!bAttachResult)
	{
		SpawnedFoodActor->Destroy();
		return false;
	}

	SpawnedFoodActor->SetActorRelativeLocation(HandAttachLocationOffset);
	SpawnedFoodActor->SetActorRelativeRotation(HandAttachRotationOffset);
	const USceneComponent* FoodRootComponent = SpawnedFoodActor->GetRootComponent();
	const FVector RelativeLocation = FoodRootComponent ? FoodRootComponent->GetRelativeLocation() : FVector::ZeroVector;
	const FRotator RelativeRotation = FoodRootComponent ? FoodRootComponent->GetRelativeRotation() : FRotator::ZeroRotator;
	UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Relative location after attach=%s"), *RelativeLocation.ToString());
	UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Relative rotation after attach=%s"), *RelativeRotation.ToString());
	UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Food final world location=%s"), *SpawnedFoodActor->GetActorLocation().ToString());

	CarriedFoodActor = SpawnedFoodActor;
	CarriedFoodItemName = TrimmedItemName;
	bIsCarryingFood = true;
	BroadcastCarryStateChanged();

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[FoodCarry] Food attached to hand. Item=%s Socket=%s"),
		*TrimmedItemName,
		*HandSocketName.ToString());
	return true;
}

bool UNPCFoodCarryComponent::PlaceCarriedFoodOnTable(AActor* TableActor)
{
	if (!CarriedFoodActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] No carried food to place."));
		return false;
	}

	if (!TableActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Table actor not found while placing food. Item=%s"), *CarriedFoodItemName);
		return false;
	}

	FTransform PlacementTransform;
	if (!ResolveTablePlacementTransform(TableActor, PlacementTransform))
	{
		PlacementTransform = FTransform(
			TableActor->GetActorRotation(),
			TableActor->GetActorLocation() + TablePlaceOffset,
			FVector::OneVector);
	}

	AActor* PlacedFoodActor = CarriedFoodActor;
	const FString PlacedItemName = CarriedFoodItemName;

	PlacedFoodActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	PlacedFoodActor->SetActorLocationAndRotation(
		PlacementTransform.GetLocation(),
		PlacementTransform.GetRotation().Rotator());
	SetFoodCollisionEnabled(PlacedFoodActor, ECollisionEnabled::NoCollision);

	CarriedFoodActor = nullptr;
	CarriedFoodItemName = TEXT("");
	bIsCarryingFood = false;
	LastPlacedFoodActor = PlacedFoodActor;
	LastPlacedTableActor = TableActor;
	LastPlacedFoodItemName = PlacedItemName;
	BroadcastCarryStateChanged();
	OnFoodPlaced.Broadcast(PlacedFoodActor, PlacedItemName);

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[FoodCarry] Food placed on table. Item=%s Table=%s"),
		*PlacedItemName,
		*TableActor->GetName());
	return true;
}

bool UNPCFoodCarryComponent::PickUpPlacedFoodFromTable(AActor* TableActor)
{
	if (IsCarryingFood())
	{
		UE_LOG(LogTemp, Log, TEXT("[FoodCarry] Pickup skipped because food is already being carried. Item=%s"), *CarriedFoodItemName);
		return true;
	}

	if (!IsValid(LastPlacedFoodActor) || !IsValid(LastPlacedTableActor))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] No tracked tableware is available to collect."));
		return false;
	}
	if (TableActor && LastPlacedTableActor != TableActor)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[FoodCarry] Tracked tableware belongs to another table. Requested=%s Tracked=%s"),
			*TableActor->GetName(),
			*LastPlacedTableActor->GetName());
		return false;
	}

	USkeletalMeshComponent* MeshComponent = ResolveOwnerMesh();
	if (!MeshComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Skeletal mesh component not found while collecting tableware."));
		return false;
	}

	USceneComponent* AttachComponent = nullptr;
	FName AttachSocketName = NAME_None;
	if (MeshComponent->DoesSocketExist(HandSocketName))
	{
		AttachComponent = MeshComponent;
		AttachSocketName = HandSocketName;
	}
	else
	{
		AttachComponent = FindFoodCarryPoint();
	}
	if (!AttachComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Cannot collect tableware: no socket and no FoodCarryPoint."));
		return false;
	}

	AActor* FoodActor = LastPlacedFoodActor;
	const FString ItemName = LastPlacedFoodItemName;
	SetFoodCollisionEnabled(FoodActor, ECollisionEnabled::NoCollision);
	const bool bAttached = FoodActor->AttachToComponent(
		AttachComponent,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		AttachSocketName);
	if (!bAttached)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Failed to attach collected tableware to the maid."));
		return false;
	}

	FoodActor->SetActorRelativeLocation(HandAttachLocationOffset);
	FoodActor->SetActorRelativeRotation(HandAttachRotationOffset);
	CarriedFoodActor = FoodActor;
	CarriedFoodItemName = ItemName;
	bIsCarryingFood = true;
	LastPlacedFoodActor = nullptr;
	LastPlacedTableActor = nullptr;
	LastPlacedFoodItemName = TEXT("");
	BroadcastCarryStateChanged();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[FoodCarry] Existing tableware collected from table. Item=%s Table=%s"),
		*CarriedFoodItemName,
		TableActor ? *TableActor->GetName() : TEXT("tracked_table"));
	return true;
}

bool UNPCFoodCarryComponent::PlaceCarriedFoodAtCounter()
{
	if (!CarriedFoodActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] No carried tableware to remove at the counter."));
		return false;
	}

	AActor* FoodActor = CarriedFoodActor;
	const FString ItemName = CarriedFoodItemName;
	FoodActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	CarriedFoodActor = nullptr;
	CarriedFoodItemName = TEXT("");
	bIsCarryingFood = false;
	BroadcastCarryStateChanged();

	const bool bDestroyRequested = FoodActor->Destroy();
	if (bDestroyRequested)
	{
		UE_LOG(LogTemp, Log, TEXT("[FoodCarry] Tableware removed at counter. Item=%s DestroyRequested=true"), *ItemName);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Failed to remove tableware at counter. Item=%s"), *ItemName);
	}
	return bDestroyRequested;
}

void UNPCFoodCarryComponent::ClearCarriedFood()
{
	AActor* FoodActorToClear = CarriedFoodActor;
	const bool bHadCarryState = bIsCarryingFood || CarriedFoodActor != nullptr || !CarriedFoodItemName.IsEmpty();

	CarriedFoodActor = nullptr;
	CarriedFoodItemName = TEXT("");
	bIsCarryingFood = false;
	if (bHadCarryState)
	{
		BroadcastCarryStateChanged();
	}

	if (FoodActorToClear)
	{
		FoodActorToClear->Destroy();
	}
}

bool UNPCFoodCarryComponent::IsCarryingFood() const
{
	return bIsCarryingFood && CarriedFoodActor != nullptr;
}

bool UNPCFoodCarryComponent::ShouldUseCarryWalk() const
{
	return bIsCarryingFood
		&& CarriedFoodItemName.TrimStartAndEnd().Equals(TEXT("\u30E9\u30FC\u30E1\u30F3"), ESearchCase::CaseSensitive);
}

TSubclassOf<AActor> UNPCFoodCarryComponent::ResolveFoodActorClass(const FString& ItemName) const
{
	const FString TrimmedItemName = ItemName.TrimStartAndEnd();
	if (const TSubclassOf<AActor>* ExactMatchClass = FoodActorClasses.Find(TrimmedItemName))
	{
		return *ExactMatchClass;
	}

	for (const TPair<FString, TSubclassOf<AActor>>& Pair : FoodActorClasses)
	{
		if (Pair.Key.TrimStartAndEnd().Equals(TrimmedItemName, ESearchCase::IgnoreCase))
		{
			return Pair.Value;
		}
	}

	return DefaultFoodActorClass;
}

USkeletalMeshComponent* UNPCFoodCarryComponent::ResolveOwnerMesh() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	Owner->GetComponents<USkeletalMeshComponent>(MeshComponents);
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (!MeshComponent)
		{
			continue;
		}

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[FoodCarry] Mesh candidate=%s Registered=%s Visible=%s"),
			*MeshComponent->GetName(),
			MeshComponent->IsRegistered() ? TEXT("true") : TEXT("false"),
			MeshComponent->IsVisible() ? TEXT("true") : TEXT("false"));
	}

	auto FindMeshByName = [&MeshComponents](const FName& MeshName) -> USkeletalMeshComponent*
	{
		if (MeshName.IsNone())
		{
			return nullptr;
		}

		for (USkeletalMeshComponent* MeshComponent : MeshComponents)
		{
			if (MeshComponent && MeshComponent->GetFName() == MeshName)
			{
				return MeshComponent;
			}
		}

		return nullptr;
	};

	if (USkeletalMeshComponent* PreferredMesh = FindMeshByName(PreferredMeshComponentName))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Selected preferred mesh: %s"), *PreferredMesh->GetName());
		return PreferredMesh;
	}

	const FName PreferredNames[] = {
		FName(TEXT("VRMVisual")),
		FName(TEXT("Mesh")),
		FName(TEXT("CharacterMesh"))
	};
	for (const FName& PreferredName : PreferredNames)
	{
		if (USkeletalMeshComponent* NamedMesh = FindMeshByName(PreferredName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Selected named mesh: %s"), *NamedMesh->GetName());
			return NamedMesh;
		}
	}

	if (const ACharacter* CharacterOwner = Cast<ACharacter>(Owner))
	{
		if (USkeletalMeshComponent* CharacterMesh = CharacterOwner->GetMesh())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Selected character mesh: %s"), *CharacterMesh->GetName());
			return CharacterMesh;
		}
	}

	USkeletalMeshComponent* FirstMesh = MeshComponents.Num() > 0 ? MeshComponents[0] : nullptr;
	if (FirstMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FoodCarry] Selected first mesh candidate: %s"), *FirstMesh->GetName());
	}
	return FirstMesh;
}

USceneComponent* UNPCFoodCarryComponent::FindFoodCarryPoint() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	TArray<USceneComponent*> SceneComponents;
	Owner->GetComponents<USceneComponent>(SceneComponents);
	for (USceneComponent* SceneComponent : SceneComponents)
	{
		if (!SceneComponent)
		{
			continue;
		}

		if (SceneComponent->GetName().Equals(TEXT("FoodCarryPoint"), ESearchCase::IgnoreCase)
			|| SceneComponent->GetFName() == FName(TEXT("FoodCarryPoint")))
		{
			return SceneComponent;
		}
	}

	return nullptr;
}

bool UNPCFoodCarryComponent::ResolveTablePlacementTransform(AActor* TableActor, FTransform& OutTransform) const
{
	if (!TableActor)
	{
		return false;
	}

	TArray<USceneComponent*> SceneComponents;
	TableActor->GetComponents<USceneComponent>(SceneComponents);
	for (USceneComponent* SceneComponent : SceneComponents)
	{
		if (!SceneComponent)
		{
			continue;
		}

		const FString ComponentName = SceneComponent->GetName();
		if (ComponentName.Equals(TEXT("FoodPlacementPoint"), ESearchCase::IgnoreCase)
			|| ComponentName.Equals(TEXT("FoodPlacePoint"), ESearchCase::IgnoreCase)
			|| ComponentName.Equals(TEXT("FoodPlacement"), ESearchCase::IgnoreCase)
			|| ComponentName.Equals(TEXT("TableTop"), ESearchCase::IgnoreCase))
		{
			OutTransform = SceneComponent->GetComponentTransform();
			return true;
		}
	}

	return false;
}

void UNPCFoodCarryComponent::SetFoodCollisionEnabled(AActor* FoodActor, ECollisionEnabled::Type CollisionType) const
{
	if (!FoodActor)
	{
		return;
	}

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	FoodActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!PrimitiveComponent)
		{
			continue;
		}

		PrimitiveComponent->SetSimulatePhysics(false);
		PrimitiveComponent->SetCollisionEnabled(CollisionType);
	}
}

void UNPCFoodCarryComponent::BroadcastCarryStateChanged()
{
	OnFoodCarryStateChanged.Broadcast(bIsCarryingFood, CarriedFoodItemName);
}
