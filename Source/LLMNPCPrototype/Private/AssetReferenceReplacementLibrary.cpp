// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetReferenceReplacementLibrary.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "NPCAutonomyActionSet.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "UObject/UObjectHash.h"

namespace
{
	TMap<UObject*, UObject*> BuildReplacementMap(
		const TArray<UObject*>& ObjectsToReplace,
		const TArray<UObject*>& ReplacementObjects)
	{
		TMap<UObject*, UObject*> ReplacementMap;
		if (ObjectsToReplace.Num() != ReplacementObjects.Num())
		{
			return ReplacementMap;
		}

		for (int32 Index = 0; Index < ObjectsToReplace.Num(); ++Index)
		{
			UObject* OldObject = ObjectsToReplace[Index];
			UObject* NewObject = ReplacementObjects[Index];
			if (OldObject && NewObject && OldObject != NewObject)
			{
				ReplacementMap.Add(OldObject, NewObject);
			}
		}
		return ReplacementMap;
	}
}

int64 UAssetReferenceReplacementLibrary::ReplaceObjectReferences(
	UObject* RootObject,
	const TArray<UObject*>& ObjectsToReplace,
	const TArray<UObject*>& ReplacementObjects)
{
#if WITH_EDITOR
	if (!RootObject || ObjectsToReplace.Num() != ReplacementObjects.Num())
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[AssetMigration] Invalid replacement request. Root=%s Old=%d New=%d"),
			*GetNameSafe(RootObject),
			ObjectsToReplace.Num(),
			ReplacementObjects.Num());
		return 0;
	}

	TMap<UObject*, UObject*> ReplacementMap = BuildReplacementMap(
		ObjectsToReplace,
		ReplacementObjects);

	if (ReplacementMap.IsEmpty())
	{
		return 0;
	}

	RootObject->Modify();
	FArchiveReplaceObjectRef<UObject> ReplaceArchive(
		RootObject,
		ReplacementMap,
		EArchiveReplaceObjectFlags::IgnoreOuterRef
			| EArchiveReplaceObjectFlags::IgnoreArchetypeRef
			| EArchiveReplaceObjectFlags::TrackReplacedReferences);
	RootObject->MarkPackageDirty();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[AssetMigration] Replaced %lld reference(s) under %s."),
		ReplaceArchive.GetCount(),
		*RootObject->GetPathName());
	return ReplaceArchive.GetCount();
#else
	return 0;
#endif
}

int64 UAssetReferenceReplacementLibrary::ReplacePackageObjectReferences(
	UObject* ObjectInPackage,
	const TArray<UObject*>& ObjectsToReplace,
	const TArray<UObject*>& ReplacementObjects)
{
#if WITH_EDITOR
	if (!ObjectInPackage || ObjectsToReplace.Num() != ReplacementObjects.Num())
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[AssetMigration] Invalid package replacement request. Root=%s Old=%d New=%d"),
			*GetNameSafe(ObjectInPackage),
			ObjectsToReplace.Num(),
			ReplacementObjects.Num());
		return 0;
	}

	TMap<UObject*, UObject*> ReplacementMap = BuildReplacementMap(
		ObjectsToReplace,
		ReplacementObjects);
	if (ReplacementMap.IsEmpty())
	{
		return 0;
	}

	UPackage* Package = ObjectInPackage->GetOutermost();
	TArray<UObject*> PackageObjects;
	GetObjectsWithPackage(Package, PackageObjects, true);
	int64 TotalReplacementCount = 0;
	for (UObject* PackageObject : PackageObjects)
	{
		if (!PackageObject)
		{
			continue;
		}

		PackageObject->Modify();
		FArchiveReplaceObjectRef<UObject> ReplaceArchive(
			PackageObject,
			ReplacementMap,
			EArchiveReplaceObjectFlags::IgnoreOuterRef
				| EArchiveReplaceObjectFlags::IgnoreArchetypeRef
				| EArchiveReplaceObjectFlags::TrackReplacedReferences);
		TotalReplacementCount += ReplaceArchive.GetCount();
	}
	Package->MarkPackageDirty();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[AssetMigration] Replaced %lld package reference(s) across %d object(s) in %s."),
		TotalReplacementCount,
		PackageObjects.Num(),
		*Package->GetName());
	return TotalReplacementCount;
#else
	return 0;
#endif
}

int32 UAssetReferenceReplacementLibrary::SetAutonomyActionAnimations(
	UNPCAutonomyActionSet* ActionSet,
	const TArray<FString>& ActivityIDs,
	const TArray<UAnimSequenceBase*>& Animations)
{
#if WITH_EDITOR
	if (!ActionSet || ActivityIDs.Num() != Animations.Num())
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[AssetMigration] Invalid action animation request. ActionSet=%s IDs=%d Animations=%d"),
			*GetNameSafe(ActionSet),
			ActivityIDs.Num(),
			Animations.Num());
		return 0;
	}

	ActionSet->Modify();
	int32 UpdatedCount = 0;
	for (int32 Index = 0; Index < ActivityIDs.Num(); ++Index)
	{
		FNPCAutonomyActivityDefinition* Activity = ActionSet->Activities.FindByPredicate(
			[&ActivityIDs, Index](const FNPCAutonomyActivityDefinition& Candidate)
			{
				return Candidate.ActivityID == ActivityIDs[Index];
			});
		if (!Activity || !Animations[Index])
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[AssetMigration] Action animation was not updated. ID=%s Animation=%s"),
				*ActivityIDs[Index],
				*GetNameSafe(Animations[Index]));
			continue;
		}

		Activity->Animation = Animations[Index];
		++UpdatedCount;
	}

	ActionSet->MarkPackageDirty();
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[AssetMigration] Updated %d autonomy action animation reference(s) in %s."),
		UpdatedCount,
		*ActionSet->GetPathName());
	return UpdatedCount;
#else
	return 0;
#endif
}

bool UAssetReferenceReplacementLibrary::ApplyBoneRotationOffset(
	UAnimSequence* Animation,
	FName BoneName,
	FRotator RotationOffset,
	bool bPreMultiply)
{
#if WITH_EDITOR
	if (!Animation || BoneName.IsNone())
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[AssetMigration] Invalid animation bone rotation request. Animation=%s Bone=%s"),
			*GetNameSafe(Animation),
			*BoneName.ToString());
		return false;
	}

	const IAnimationDataModel* DataModel = Animation->GetDataModel();
	FName MatchedBoneName = NAME_None;
	if (DataModel)
	{
		TArray<FName> TrackNames;
		DataModel->GetBoneTrackNames(TrackNames);
		for (const FName CandidateName : TrackNames)
		{
			if (CandidateName.ToString().Equals(BoneName.ToString(), ESearchCase::IgnoreCase))
			{
				MatchedBoneName = CandidateName;
				break;
			}
		}
	}
	if (MatchedBoneName.IsNone())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[AssetMigration] Bone track not found. Animation=%s Bone=%s"),
			*Animation->GetPathName(),
			*BoneName.ToString());
		return false;
	}

	TArray<FTransform> SourceTransforms;
	DataModel->GetBoneTrackTransforms(MatchedBoneName, SourceTransforms);
	if (SourceTransforms.IsEmpty())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[AssetMigration] Bone track has no transform keys. Animation=%s Bone=%s"),
			*Animation->GetPathName(),
			*MatchedBoneName.ToString());
		return false;
	}

	TArray<FVector3f> PositionKeys;
	TArray<FQuat4f> RotationKeys;
	TArray<FVector3f> ScaleKeys;
	PositionKeys.Reserve(SourceTransforms.Num());
	RotationKeys.Reserve(SourceTransforms.Num());
	ScaleKeys.Reserve(SourceTransforms.Num());
	const FQuat OffsetQuaternion = RotationOffset.Quaternion();
	for (const FTransform& SourceTransform : SourceTransforms)
	{
		PositionKeys.Add(FVector3f(SourceTransform.GetTranslation()));
		const FQuat SourceRotation = SourceTransform.GetRotation();
		FQuat Result = bPreMultiply
			? OffsetQuaternion * SourceRotation
			: SourceRotation * OffsetQuaternion;
		Result.Normalize();
		RotationKeys.Add(FQuat4f(Result));
		ScaleKeys.Add(FVector3f(SourceTransform.GetScale3D()));
	}

	Animation->Modify();
	IAnimationDataController& Controller = Animation->GetController();
	Controller.OpenBracket(
		FText::FromString(TEXT("Apply NPC animation bone rotation offset")),
		false);
	const bool bUpdated = Controller.SetBoneTrackKeys(
		MatchedBoneName,
		PositionKeys,
		RotationKeys,
		ScaleKeys,
		false);
	Controller.CloseBracket(false);

	if (bUpdated)
	{
		Animation->MarkPackageDirty();
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[AssetMigration] Applied rotation offset to %s:%s Offset=%s PreMultiply=%d"),
			*Animation->GetPathName(),
			*BoneName.ToString(),
			*RotationOffset.ToCompactString(),
			bPreMultiply ? 1 : 0);
	}
	return bUpdated;
#else
	return false;
#endif
}

bool UAssetReferenceReplacementLibrary::CopyAnimationSequenceData(
	UAnimSequence* DestinationAnimation,
	UAnimSequence* SourceAnimation)
{
#if WITH_EDITOR
	if (!DestinationAnimation || !SourceAnimation || DestinationAnimation == SourceAnimation)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[AssetMigration] Invalid animation data copy. Destination=%s Source=%s"),
			*GetNameSafe(DestinationAnimation),
			*GetNameSafe(SourceAnimation));
		return false;
	}

	if (DestinationAnimation->GetSkeleton() != SourceAnimation->GetSkeleton())
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[AssetMigration] Animation skeleton mismatch. Destination=%s Source=%s"),
			*DestinationAnimation->GetPathName(),
			*SourceAnimation->GetPathName());
		return false;
	}

	const IAnimationDataModel* SourceModel = SourceAnimation->GetDataModel();
	if (!SourceModel)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[AssetMigration] Source animation has no data model: %s"),
			*SourceAnimation->GetPathName());
		return false;
	}

	TArray<FName> SourceTrackNames;
	SourceModel->GetBoneTrackNames(SourceTrackNames);
	if (SourceTrackNames.IsEmpty())
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[AssetMigration] Source animation has no bone tracks: %s"),
			*SourceAnimation->GetPathName());
		return false;
	}

	DestinationAnimation->Modify();
	IAnimationDataController& DestinationController = DestinationAnimation->GetController();
	DestinationController.OpenBracket(
		FText::FromString(TEXT("Refresh retargeted NPC animation data")),
		false);
	DestinationController.RemoveAllBoneTracks(false);
	DestinationController.SetFrameRate(SourceModel->GetFrameRate(), false);
	DestinationController.SetNumberOfFrames(
		FFrameNumber(SourceModel->GetNumberOfFrames()),
		false);

	bool bAllTracksCopied = true;
	for (const FName TrackName : SourceTrackNames)
	{
		TArray<FTransform> SourceTransforms;
		SourceModel->GetBoneTrackTransforms(TrackName, SourceTransforms);
		if (SourceTransforms.IsEmpty())
		{
			bAllTracksCopied = false;
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[AssetMigration] Skipping empty animation track. Animation=%s Track=%s"),
				*SourceAnimation->GetPathName(),
				*TrackName.ToString());
			continue;
		}

		TArray<FVector3f> PositionKeys;
		TArray<FQuat4f> RotationKeys;
		TArray<FVector3f> ScaleKeys;
		PositionKeys.Reserve(SourceTransforms.Num());
		RotationKeys.Reserve(SourceTransforms.Num());
		ScaleKeys.Reserve(SourceTransforms.Num());
		for (const FTransform& SourceTransform : SourceTransforms)
		{
			PositionKeys.Add(FVector3f(SourceTransform.GetTranslation()));
			RotationKeys.Add(FQuat4f(SourceTransform.GetRotation()));
			ScaleKeys.Add(FVector3f(SourceTransform.GetScale3D()));
		}

		const bool bAdded = DestinationController.AddBoneCurve(TrackName, false);
		const bool bKeysSet = DestinationController.SetBoneTrackKeys(
			TrackName,
			PositionKeys,
			RotationKeys,
			ScaleKeys,
			false);
		bAllTracksCopied = bAllTracksCopied && bAdded && bKeysSet;
	}
	DestinationController.CloseBracket(false);
	DestinationAnimation->MarkPackageDirty();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[AssetMigration] Copied animation data. Destination=%s Source=%s Tracks=%d Complete=%d"),
		*DestinationAnimation->GetPathName(),
		*SourceAnimation->GetPathName(),
		SourceTrackNames.Num(),
		bAllTracksCopied ? 1 : 0);
	return bAllTracksCopied;
#else
	return false;
#endif
}
