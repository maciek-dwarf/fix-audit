// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "EditorSubsystem.h"
#include "LevelContextExporterTypes.h"

#include <atomic>

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

	/** Export the project's asset tree (Content Browser hierarchy) to a JSON file (runs asynchronously) */
	void ExportAssetTreeContext(const FString& OutputPath);

	/** Returns current export progress from 0.0 to 1.0 */
	float GetExportProgress() const { return ExportProgress.load(std::memory_order_acquire); }

	/** Whether an export is currently in progress */
	bool IsExporting() const { return bIsExporting.load(std::memory_order_acquire); }

	/** Register the editor window for live progress updates */
	void SetWindowReference(const TSharedRef<SLevelContextExporterWindow>& InWindow);

	/** Broadcast when export finishes */
	FOnExportComplete OnExportComplete;

	/** Broadcast when asset tree export finishes */
	FOnExportComplete OnAssetTreeExportComplete;

	/** Actor count from the last completed level export (thread-safe). */
	int32 GetLastExportedActorCount() const
	{
		FScopeLock Lock(&ExportResultLock);
		return LastExportedActors.Num();
	}

	/** Whether the last completed export was the asset tree exporter */
	bool WasLastExportAssetTree() const { return bLastExportWasAssetTree.load(std::memory_order_acquire); }

	/** Number of assets exported in the last asset tree export */
	int32 GetLastExportedAssetCount() const { return LastExportedAssetCount.load(std::memory_order_acquire); }

	/** Human-readable result from the last asset tree export */
	FString GetLastAssetTreeExportResult() const
	{
		FScopeLock Lock(&ExportResultLock);
		return LastAssetTreeExportResult;
	}

private:
	/** Serialize all UProperties on an actor into key-value string pairs */
	TArray<TPair<FString, FString>> SerializeActorProperties(AActor* Actor);

	/** Gather component info for an actor, including property data */
	TArray<FString> SerializeComponents(AActor* Actor);

	/** Collect asset paths referenced by an actor's components */
	TArray<FString> CollectActorAssetReferences(AActor* Actor);

	/** Build the full JSON string from exported actor data */
	FString BuildJsonOutput(const TArray<FExportedActorData>& ExportedActors);

	// Export state (atomics: safe to read from UI thread while workers run)
	std::atomic<float> ExportProgress{ 0.0f };
	std::atomic<bool> bIsExporting{ false };

	// Asset tree flags (atomics)
	std::atomic<bool> bLastExportWasAssetTree{ false };
	std::atomic<int32> LastExportedAssetCount{ 0 };

	// Heavier / aggregate state: protect with a lock
	mutable FCriticalSection ExportResultLock;
	FString LastExportResult;
	FString LastAssetTreeExportResult;
	TArray<FExportedActorData> LastExportedActors;

	// Cached pointer to the editor window for progress callbacks.
	// Weak pointer so it safely expires if the tab closes.
	TWeakPtr<SLevelContextExporterWindow> CachedWindow;
};
