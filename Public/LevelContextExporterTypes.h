// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Broadcast when an export operation completes
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnExportComplete, bool /*bSuccess*/, const FString& /*OutputPath*/);

// Holds exported data for a single actor
struct FExportedActorData
{
	FString ActorName;
	FString ClassName;
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	FVector Scale = FVector::OneVector;
	TArray<TPair<FString, FString>> Properties;
	TArray<FString> ComponentDescriptions;
	TArray<FString> ReferencedAssets;
	TArray<FString> ImplementedInterfaces;
	FString ParentActorName;
	TArray<FString> ChildActorNames;
	FString Mobility;
	TArray<FName> Tags;
	FString Layer;
	bool bIsVisible = true;
	bool bSimulatePhysics = false;
	FString CollisionProfileName;
	FString ExportTimestamp;
};

// Summary statistics about the current level
struct FLevelContextStats
{
	int32 TotalActorCount = 0;
	TArray<TPair<FString, int32>> ClassBreakdown;
	int32 EstimatedTokenCount = 0;
	TArray<FString> UniqueAssetReferences;
};
