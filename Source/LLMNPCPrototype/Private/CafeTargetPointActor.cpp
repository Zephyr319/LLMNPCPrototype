// Copyright Epic Games, Inc. All Rights Reserved.

#include "CafeTargetPointActor.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"

ACafeTargetPointActor::ACafeTargetPointActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(SceneRoot);

	TargetID = TEXT("Target_01");
	TargetType = TEXT("generic");
	DisplayName = FText::FromString(TEXT("Target 01"));
	TargetTags.Reset();
	Priority = 0;
	bIsAvailable = true;
}

FTransform ACafeTargetPointActor::GetTargetTransform() const
{
	return Arrow ? Arrow->GetComponentTransform() : GetActorTransform();
}

bool ACafeTargetPointActor::HasTagString(const FString& Tag) const
{
	const FString TrimmedTag = Tag.TrimStartAndEnd();
	if (TrimmedTag.IsEmpty())
	{
		return false;
	}

	for (const FString& TargetTag : TargetTags)
	{
		if (TargetTag.TrimStartAndEnd().Equals(TrimmedTag, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}
