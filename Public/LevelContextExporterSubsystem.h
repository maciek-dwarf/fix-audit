// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "LevelContextExporterTypes.h"
#include "LevelContextExporterSubsystem.generated.h"

class SLevelContextExporterWindow;

/**
 * Core subsystem for gathering level data and exporting it as JSON.
 * Handles actor crawling, property serialization, and async file export.
 */
UCLASS()
class ULevelContextExporterSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Gather summary statistics about the current level */
	FLevelContextStats GatherLevelStats();

	/** Export full level context to a JSON file (runs asynchronously) */
	void ExportLevelContext(const FString& OutputPath);

	/** Returns current export progress from 0.0 to 1.0 */
	float GetExportProgress() const { return ExportProgress; }

	/** Whether an export is currently in progress */
	bool IsExporting() const { return bIsExporting; }

	/** Register the editor window for live progress updates */
	void SetWindowReference(SLevelContextExporterWindow* InWindow);

	/** Broadcast when export finishes */
	FOnExportComplete OnExportComplete;

	/** Results from the last completed export */
	TArray<FExportedActorData> LastExportedActors;

private:
	/** Serialize all UProperties on an actor into key-value string pairs */
	TArray<TPair<FString, FString>> SerializeActorProperties(AActor* Actor);

	/** Gather component info for an actor, including property data */
	TArray<FString> SerializeComponents(AActor* Actor);

	/** Collect asset paths referenced by an actor's components */
	TArray<FString> CollectActorAssetReferences(AActor* Actor);

	/** Build the full JSON string from exported actor data */
	FString BuildJsonOutput(const TArray<FExportedActorData>& ExportedActors);

	// Export state
	float ExportProgress = 0.0f;
	bool bIsExporting = false;
	FString LastExportResult;

	// Cached pointer to the editor window for progress callbacks.
	// Stored as raw pointer to avoid shared pointer overhead in tight loops.
	SLevelContextExporterWindow* CachedWindow = nullptr;
};
