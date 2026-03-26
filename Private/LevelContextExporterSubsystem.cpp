// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelContextExporterSubsystem.h"
#include "LevelContextExporterWindow.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Editor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Async/Async.h"
#include "UObject/UnrealType.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

namespace LevelContextExporter_SubsystemThreading
{
	static void BroadcastExportComplete_GameThreadSafe(ULevelContextExporterSubsystem* Subsystem, bool bSuccess, const FString& OutputPath)
	{
		if (!Subsystem)
		{
			return;
		}
		if (IsInGameThread())
		{
			Subsystem->OnExportComplete.Broadcast(bSuccess, OutputPath);
			return;
		}
		TWeakObjectPtr<ULevelContextExporterSubsystem> WeakSubsystem(Subsystem);
		AsyncTask(ENamedThreads::GameThread, [WeakSubsystem, bSuccess, OutputPath]()
		{
			if (ULevelContextExporterSubsystem* Pinned = WeakSubsystem.Get())
			{
				Pinned->OnExportComplete.Broadcast(bSuccess, OutputPath);
			}
		});
	}

	static void BroadcastAssetTreeExportComplete_GameThreadSafe(ULevelContextExporterSubsystem* Subsystem, bool bSuccess, const FString& OutputPath)
	{
		if (!Subsystem)
		{
			return;
		}
		if (IsInGameThread())
		{
			Subsystem->OnAssetTreeExportComplete.Broadcast(bSuccess, OutputPath);
			return;
		}
		TWeakObjectPtr<ULevelContextExporterSubsystem> WeakSubsystem(Subsystem);
		AsyncTask(ENamedThreads::GameThread, [WeakSubsystem, bSuccess, OutputPath]()
		{
			if (ULevelContextExporterSubsystem* Pinned = WeakSubsystem.Get())
			{
				Pinned->OnAssetTreeExportComplete.Broadcast(bSuccess, OutputPath);
			}
		});
	}
}

void ULevelContextExporterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void ULevelContextExporterSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void ULevelContextExporterSubsystem::SetWindowReference(TWeakPtr<SLevelContextExporterWindow> InWindow)
{
	CachedWindow = InWindow;
}

// ---------------------------------------------------------------------------
// Stats Gathering
// ---------------------------------------------------------------------------

FLevelContextStats ULevelContextExporterSubsystem::GatherLevelStats()
{
	FLevelContextStats Stats;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ULevel* Level = World->GetCurrentLevel();

	// Collect all valid actors
	TArray<AActor*> AllActors;
	for (AActor* Actor : Level->Actors)
	{
		if (Actor != nullptr)
		{
			AllActors.Add(Actor);
		}
	}

	Stats.TotalActorCount = AllActors.Num();

	// Count actors per class
	TArray<TPair<UClass*, int32>> ClassCounts;
	for (AActor* Actor : AllActors)
	{
		UClass* ActorClass = Actor->GetClass();

		// Check if we already recorded this class
		bool bFound = false;
		for (int32 i = 0; i < ClassCounts.Num(); i++)
		{
			if (ClassCounts[i].Key == ActorClass)
			{
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			// Count how many actors have this class
			int32 Count = 0;
			for (AActor* Other : AllActors)
			{
				if (Other->GetClass() == ActorClass)
				{
					Count++;
				}
			}
			ClassCounts.Add(TPair<UClass*, int32>(ActorClass, Count));
		}
	}

	// Store as displayable strings
	for (const auto& Pair : ClassCounts)
	{
		Stats.ClassBreakdown.Add(TPair<FString, int32>(Pair.Key->GetName(), Pair.Value));
	}

	// Collect unique asset references
	for (AActor* Actor : AllActors)
	{
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);

		for (UActorComponent* Component : Components)
		{
			UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(Component);
			if (MeshComp && MeshComp->GetStaticMesh())
			{
				FString AssetPath = MeshComp->GetStaticMesh()->GetPathName();
				if (!Stats.UniqueAssetReferences.Contains(AssetPath))
				{
					Stats.UniqueAssetReferences.Add(AssetPath);
				}
			}
		}
	}

	// Estimate token count — rough heuristic: total expected chars / 4
	int32 TotalChars = 0;
	for (const auto& ClassPair : Stats.ClassBreakdown)
	{
		TotalChars += ClassPair.Key.Len() + 10;
	}
	for (const auto& AssetRef : Stats.UniqueAssetReferences)
	{
		TotalChars += AssetRef.Len();
	}
	TotalChars += Stats.TotalActorCount * 500;
	Stats.EstimatedTokenCount = TotalChars / 4;
	// TODO: Fine-tune this estimate based on actual measured export sizes

	return Stats;
}

// ---------------------------------------------------------------------------
// Property Serialization
// ---------------------------------------------------------------------------

TArray<TPair<FString, FString>> ULevelContextExporterSubsystem::SerializeActorProperties(AActor* Actor)
{
	TArray<TPair<FString, FString>> Properties;

	if (!Actor)
	{
		return Properties;
	}

	// Walk every property on the actor's class hierarchy
	for (TFieldIterator<FProperty> PropIt(Actor->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		FString ValueStr;
		Property->ExportText_InContainer(0, ValueStr, Actor, nullptr, Actor, PPF_None);
		Properties.Add(TPair<FString, FString>(Property->GetName(), ValueStr));
	}

	return Properties;
}

TArray<FString> ULevelContextExporterSubsystem::SerializeComponents(AActor* Actor)
{
	TArray<FString> ComponentDescriptions;

	if (!Actor)
	{
		return ComponentDescriptions;
	}

	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);

	for (UActorComponent* Component : Components)
	{
		FString Desc;
		Desc += Component->GetName() + TEXT(" (") + Component->GetClass()->GetName() + TEXT("):\n");

		// Serialize each property on the component
		for (TFieldIterator<FProperty> PropIt(Component->GetClass()); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;
			FString ValueStr;
			Property->ExportText_InContainer(0, ValueStr, Component, nullptr, Component, PPF_None);
			Desc += TEXT("  ") + Property->GetName() + TEXT(" = ") + ValueStr + TEXT("\n");
		}

		// Include owning actor spatial context for component-relative data
		TArray<TPair<FString, FString>> OwnerProps = SerializeActorProperties(Actor);
		for (const auto& Prop : OwnerProps)
		{
			if (Prop.Key.Contains(TEXT("Location")) || Prop.Key.Contains(TEXT("Rotation")))
			{
				Desc += TEXT("  [OwnerCtx] ") + Prop.Key + TEXT(" = ") + Prop.Value + TEXT("\n");
			}
		}

		ComponentDescriptions.Add(Desc);
	}

	return ComponentDescriptions;
}

TArray<FString> ULevelContextExporterSubsystem::CollectActorAssetReferences(AActor* Actor)
{
	TArray<FString> Assets;

	if (!Actor)
	{
		return Assets;
	}

	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);

	for (UActorComponent* Component : Components)
	{
		// Static meshes
		UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(Component);
		if (MeshComp && MeshComp->GetStaticMesh())
		{
			FString AssetPath = MeshComp->GetStaticMesh()->GetPathName();
			if (!Assets.Contains(AssetPath))
			{
				Assets.Add(AssetPath);
			}
		}

		// Materials
		UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component);
		if (PrimComp)
		{
			for (int32 MatIdx = 0; MatIdx < PrimComp->GetNumMaterials(); MatIdx++)
			{
				UMaterialInterface* Material = PrimComp->GetMaterial(MatIdx);
				if (Material)
				{
					FString MatPath = Material->GetPathName();
					if (!Assets.Contains(MatPath))
					{
						Assets.Add(MatPath);
					}
				}
			}
		}
	}

	return Assets;
}

// ---------------------------------------------------------------------------
// JSON Builder
// ---------------------------------------------------------------------------

FString ULevelContextExporterSubsystem::BuildJsonOutput(const TArray<FExportedActorData>& ExportedActors)
{
	// TODO: Consider using a JSON library for proper escaping
	FString Json;
	Json += TEXT("{\n");
	Json += TEXT("  \"LevelContext\": {\n");
	Json += TEXT("    \"ActorCount\": ") + FString::FromInt(ExportedActors.Num()) + TEXT(",\n");
	Json += TEXT("    \"Actors\": [\n");

	for (int32 i = 0; i < ExportedActors.Num(); i++)
	{
		const FExportedActorData& Data = ExportedActors[i];

		Json += TEXT("      {\n");
		Json += TEXT("        \"Name\": \"") + Data.ActorName + TEXT("\",\n");
		Json += TEXT("        \"Class\": \"") + Data.ClassName + TEXT("\",\n");
		Json += TEXT("        \"ExportedAt\": \"") + Data.ExportTimestamp + TEXT("\",\n");

		// Transform
		Json += TEXT("        \"Transform\": {\n");
		Json += TEXT("          \"Location\": \"") + Data.Location.ToString() + TEXT("\",\n");
		Json += TEXT("          \"Rotation\": \"") + Data.Rotation.ToString() + TEXT("\",\n");
		Json += TEXT("          \"Scale\": \"") + Data.Scale.ToString() + TEXT("\"\n");
		Json += TEXT("        },\n");

		// Metadata
		Json += TEXT("        \"Mobility\": \"") + Data.Mobility + TEXT("\",\n");
		Json += TEXT("        \"IsVisible\": ") + FString(Data.bIsVisible ? TEXT("true") : TEXT("false")) + TEXT(",\n");
		Json += TEXT("        \"SimulatePhysics\": ") + FString(Data.bSimulatePhysics ? TEXT("true") : TEXT("false")) + TEXT(",\n");
		Json += TEXT("        \"CollisionProfile\": \"") + Data.CollisionProfileName + TEXT("\",\n");
		Json += TEXT("        \"Layer\": \"") + Data.Layer + TEXT("\",\n");

		// Tags
		Json += TEXT("        \"Tags\": [");
		for (int32 t = 0; t < Data.Tags.Num(); t++)
		{
			Json += TEXT("\"") + Data.Tags[t].ToString() + TEXT("\"");
			if (t < Data.Tags.Num() - 1) Json += TEXT(", ");
		}
		Json += TEXT("],\n");

		// Hierarchy
		Json += TEXT("        \"Parent\": \"") + Data.ParentActorName + TEXT("\",\n");
		Json += TEXT("        \"Children\": [");
		for (int32 c = 0; c < Data.ChildActorNames.Num(); c++)
		{
			Json += TEXT("\"") + Data.ChildActorNames[c] + TEXT("\"");
			if (c < Data.ChildActorNames.Num() - 1) Json += TEXT(", ");
		}
		Json += TEXT("],\n");

		// Interfaces
		Json += TEXT("        \"Interfaces\": [");
		for (int32 f = 0; f < Data.ImplementedInterfaces.Num(); f++)
		{
			Json += TEXT("\"") + Data.ImplementedInterfaces[f] + TEXT("\"");
			if (f < Data.ImplementedInterfaces.Num() - 1) Json += TEXT(", ");
		}
		Json += TEXT("],\n");

		// Properties
		Json += TEXT("        \"Properties\": {\n");
		for (int32 p = 0; p < Data.Properties.Num(); p++)
		{
			FString EscapedValue = Data.Properties[p].Value.Replace(TEXT("\""), TEXT("\\\""));
			Json += TEXT("          \"") + Data.Properties[p].Key + TEXT("\": \"") + EscapedValue + TEXT("\"");
			if (p < Data.Properties.Num() - 1) Json += TEXT(",");
			Json += TEXT("\n");
		}
		Json += TEXT("        },\n");

		// Components
		Json += TEXT("        \"Components\": [\n");
		for (int32 comp = 0; comp < Data.ComponentDescriptions.Num(); comp++)
		{
			FString Escaped = Data.ComponentDescriptions[comp].Replace(TEXT("\""), TEXT("\\\"")).Replace(TEXT("\n"), TEXT("\\n"));
			Json += TEXT("          \"") + Escaped + TEXT("\"");
			if (comp < Data.ComponentDescriptions.Num() - 1) Json += TEXT(",");
			Json += TEXT("\n");
		}
		Json += TEXT("        ],\n");

		// Asset references
		Json += TEXT("        \"ReferencedAssets\": [");
		for (int32 a = 0; a < Data.ReferencedAssets.Num(); a++)
		{
			Json += TEXT("\"") + Data.ReferencedAssets[a] + TEXT("\"");
			if (a < Data.ReferencedAssets.Num() - 1) Json += TEXT(", ");
		}
		Json += TEXT("]\n");

		Json += TEXT("      }");
		if (i < ExportedActors.Num() - 1) Json += TEXT(",");
		Json += TEXT("\n");
	}

	Json += TEXT("    ]\n");
	Json += TEXT("  }\n");
	Json += TEXT("}\n");

	return Json;
}

// ---------------------------------------------------------------------------
// Async Export
// ---------------------------------------------------------------------------

void ULevelContextExporterSubsystem::ExportLevelContext(const FString& OutputPath)
{
	bIsExporting.store(true, std::memory_order_release);
	ExportProgress.store(0.0f, std::memory_order_release);
	bLastExportWasAssetTree.store(false, std::memory_order_release);
	LastExportedAssetCount.store(0, std::memory_order_release);
	{
		FScopeLock Lock(&ExportResultLock);
		LastExportResult.Empty();
		LastAssetTreeExportResult.Empty();
	}

	// All UObject / editor access must run on the game thread. JSON build + file write run on a worker thread.
	AsyncTask(ENamedThreads::GameThread, [this, OutputPath]()
	{
		if (!GEditor)
		{
			{
				FScopeLock Lock(&ExportResultLock);
				LastExportResult = TEXT("Export failed: editor not available.");
			}
			bIsExporting.store(false, std::memory_order_release);
			ExportProgress.store(0.0f, std::memory_order_release);
			LevelContextExporter_SubsystemThreading::BroadcastExportComplete_GameThreadSafe(this, false, OutputPath);
			return;
		}

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			{
				FScopeLock Lock(&ExportResultLock);
				LastExportResult = TEXT("Export failed: no editor world.");
			}
			bIsExporting.store(false, std::memory_order_release);
			ExportProgress.store(0.0f, std::memory_order_release);
			LevelContextExporter_SubsystemThreading::BroadcastExportComplete_GameThreadSafe(this, false, OutputPath);
			return;
		}

		ULevel* Level = World->GetCurrentLevel();
		if (!Level)
		{
			{
				FScopeLock Lock(&ExportResultLock);
				LastExportResult = TEXT("Export failed: no current level.");
			}
			bIsExporting.store(false, std::memory_order_release);
			ExportProgress.store(0.0f, std::memory_order_release);
			LevelContextExporter_SubsystemThreading::BroadcastExportComplete_GameThreadSafe(this, false, OutputPath);
			return;
		}

		// Gather all valid actors
		TArray<AActor*> AllActors;
		for (AActor* Actor : Level->Actors)
		{
			if (Actor != nullptr)
			{
				AllActors.Add(Actor);
			}
		}

		TArray<FExportedActorData> ExportedActors;
		const int32 TotalActors = AllActors.Num();
		if (TotalActors == 0)
		{
			ExportProgress.store(1.0f, std::memory_order_release);
		}

		for (int32 i = 0; i < AllActors.Num(); i++)
		{
			AActor* Actor = AllActors[i];

			UE_LOG(LogTemp, Verbose, TEXT("Exporting actor %d/%d: %s"), i + 1, TotalActors, *Actor->GetName());

			FExportedActorData ActorData;
			ActorData.ActorName = Actor->GetName();
			ActorData.ClassName = Actor->GetClass()->GetName();
			ActorData.Location = Actor->GetActorLocation();
			ActorData.Rotation = Actor->GetActorRotation();
			ActorData.Scale = Actor->GetActorScale3D();
			ActorData.ExportTimestamp = FDateTime::Now().ToString();

			// Mobility
			USceneComponent* RootComp = Actor->GetRootComponent();
			if (RootComp)
			{
				switch (RootComp->Mobility)
				{
				case EComponentMobility::Static:     ActorData.Mobility = TEXT("Static"); break;
				case EComponentMobility::Stationary:  ActorData.Mobility = TEXT("Stationary"); break;
				case EComponentMobility::Movable:     ActorData.Mobility = TEXT("Movable"); break;
				}
			}

			// Tags
			ActorData.Tags = Actor->Tags;

			// Layers
			for (const FName& LayerName : Actor->Layers)
			{
				ActorData.Layer += LayerName.ToString() + TEXT(", ");
			}

			// Visibility
			ActorData.bIsVisible = !Actor->IsHidden();

			// Physics and collision settings
			if (RootComp)
			{
				UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(RootComp);
				if (PrimComp)
				{
					ActorData.bSimulatePhysics = PrimComp->IsSimulatingPhysics();
					ActorData.CollisionProfileName = PrimComp->GetCollisionProfileName().ToString();
				}
			}

			// Parent/child hierarchy
			if (Actor->GetAttachParentActor())
			{
				ActorData.ParentActorName = Actor->GetAttachParentActor()->GetName();
			}

			TArray<AActor*> ChildActors;
			Actor->GetAttachedActors(ChildActors);
			for (AActor* Child : ChildActors)
			{
				ActorData.ChildActorNames.Add(Child->GetName());
			}

			// Interfaces implemented by this actor's class
			for (const FImplementedInterface& Interface : Actor->GetClass()->Interfaces)
			{
				ActorData.ImplementedInterfaces.Add(Interface.Class->GetName());
			}

			// Serialize all properties on the actor
			ActorData.Properties = SerializeActorProperties(Actor);

			// Gather component info (includes per-component property dump)
			ActorData.ComponentDescriptions = SerializeComponents(Actor);

			// Collect referenced assets (meshes, materials, textures)
			ActorData.ReferencedAssets = CollectActorAssetReferences(Actor);

			ExportedActors.Add(MoveTemp(ActorData));

			// Report progress (game thread — safe for Slate)
			if (TotalActors > 0)
			{
				ExportProgress.store(
					static_cast<float>(i + 1) / static_cast<float>(TotalActors),
					std::memory_order_release);
			}

			if (CachedWindow)
			{
				if (TSharedPtr<SLevelContextExporterWindow> PinnedWindow = CachedWindow.Pin())
				{
					PinnedWindow->UpdateProgressText(
						FString::Printf(TEXT("Exporting: %d / %d actors"), i + 1, TotalActors));
				}
			}
		}

		Async(EAsyncExecution::Thread, [this, OutputPath, ExportedActors = MoveTemp(ExportedActors)]() mutable
		{
			const FString JsonOutput = BuildJsonOutput(ExportedActors);
			const bool bSaved = FFileHelper::SaveStringToFile(JsonOutput, *OutputPath);

			AsyncTask(ENamedThreads::GameThread, [this, bSaved, OutputPath, ExportedActors = MoveTemp(ExportedActors)]() mutable
			{
				{
					FScopeLock Lock(&ExportResultLock);
					LastExportedActors = MoveTemp(ExportedActors);
					const int32 NumActors = LastExportedActors.Num();
					LastExportResult = bSaved
						? FString::Printf(TEXT("Exported %d actors to %s"), NumActors, *OutputPath)
						: FString::Printf(TEXT("Failed to write file: %s"), *OutputPath);
				}

				bIsExporting.store(false, std::memory_order_release);
				ExportProgress.store(1.0f, std::memory_order_release);

				LevelContextExporter_SubsystemThreading::BroadcastExportComplete_GameThreadSafe(this, bSaved, OutputPath);
			});
		});
	});
}

namespace LevelContextExporter_AssetTree
{
	struct FFolderNode
	{
		FString Name;
		FString Path;
		TMap<FString, TSharedPtr<FFolderNode>> Children;
		TArray<FAssetData> Assets;
	};

	static void AddAssetToTree(FFolderNode& Root, const FString& RootPath, const FAssetData& Asset)
	{
		const FString PackagePath = Asset.PackagePath.ToString();
		FString Relative = PackagePath;
		if (Relative.StartsWith(RootPath))
		{
			Relative.RightChopInline(RootPath.Len());
		}
		Relative.RemoveFromStart(TEXT("/"));

		FFolderNode* Current = &Root;

		if (!Relative.IsEmpty())
		{
			TArray<FString> Segments;
			Relative.ParseIntoArray(Segments, TEXT("/"), true);
			for (const FString& Segment : Segments)
			{
				if (Segment.IsEmpty())
				{
					continue;
				}

				TSharedPtr<FFolderNode>& Child = Current->Children.FindOrAdd(Segment);
				if (!Child.IsValid())
				{
					Child = MakeShared<FFolderNode>();
					Child->Name = Segment;

					const FString ParentPath = Current->Path;
					Child->Path = ParentPath.EndsWith(TEXT("/")) ? (ParentPath + Segment) : (ParentPath + TEXT("/") + Segment);
				}

				Current = Child.Get();
			}
		}

		Current->Assets.Add(Asset);
	}

	static TSharedPtr<FJsonObject> FolderNodeToJson(const FFolderNode& Node)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Node.Name);
		Obj->SetStringField(TEXT("path"), Node.Path);

		TArray<TSharedPtr<FJsonValue>> AssetValues;
		AssetValues.Reserve(Node.Assets.Num());
		for (const FAssetData& Asset : Node.Assets)
		{
			TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
			AssetObj->SetStringField(TEXT("assetName"), Asset.AssetName.ToString());
			AssetObj->SetStringField(TEXT("assetClass"), Asset.AssetClassPath.ToString());
			AssetObj->SetStringField(TEXT("packageName"), Asset.PackageName.ToString());
			AssetObj->SetStringField(TEXT("packagePath"), Asset.PackagePath.ToString());
			AssetObj->SetStringField(TEXT("objectPath"), Asset.GetObjectPathString());
			AssetValues.Add(MakeShared<FJsonValueObject>(AssetObj));
		}
		Obj->SetArrayField(TEXT("assets"), AssetValues);

		TArray<TSharedPtr<FJsonValue>> FolderValues;
		FolderValues.Reserve(Node.Children.Num());
		for (const auto& Pair : Node.Children)
		{
			if (Pair.Value.IsValid())
			{
				FolderValues.Add(MakeShared<FJsonValueObject>(FolderNodeToJson(*Pair.Value)));
			}
		}
		Obj->SetArrayField(TEXT("folders"), FolderValues);

		return Obj;
	}
}

void ULevelContextExporterSubsystem::ExportAssetTreeContext(const FString& OutputPath)
{
	bIsExporting.store(true, std::memory_order_release);
	ExportProgress.store(0.0f, std::memory_order_release);
	bLastExportWasAssetTree.store(false, std::memory_order_release);
	LastExportedAssetCount.store(0, std::memory_order_release);
	{
		FScopeLock Lock(&ExportResultLock);
		LastExportResult.Empty();
		LastAssetTreeExportResult.Empty();
	}

	const FString RootPath = TEXT("/Game");
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(*RootPath);
	Filter.bRecursivePaths = true;
	Filter.bIncludeOnlyOnDiskAssets = false;

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);

	const int32 AssetCount = Assets.Num();

	Async(EAsyncExecution::Thread, [this, OutputPath, RootPath, Assets = MoveTemp(Assets), AssetCount]() mutable
	{
		using namespace LevelContextExporter_AssetTree;

		FFolderNode Root;
		Root.Name = RootPath;
		Root.Path = RootPath;

		for (const FAssetData& Asset : Assets)
		{
			AddAssetToTree(Root, RootPath, Asset);
		}

		TSharedPtr<FJsonObject> RootObj = MakeShared<FJsonObject>();
		RootObj->SetStringField(TEXT("exportedAt"), FDateTime::Now().ToString());
		RootObj->SetStringField(TEXT("rootPath"), RootPath);
		RootObj->SetNumberField(TEXT("assetCount"), AssetCount);
		RootObj->SetObjectField(TEXT("tree"), FolderNodeToJson(Root));

		TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
		Document->SetObjectField(TEXT("AssetTreeContext"), RootObj);

		FString JsonOutput;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonOutput);
		FJsonSerializer::Serialize(Document.ToSharedRef(), Writer);

		const bool bSaved = FFileHelper::SaveStringToFile(JsonOutput, *OutputPath);

		AsyncTask(ENamedThreads::GameThread, [this, bSaved, OutputPath, AssetCount]()
		{
			{
				FScopeLock Lock(&ExportResultLock);
				LastAssetTreeExportResult = bSaved
					? FString::Printf(TEXT("Exported %d assets to %s"), AssetCount, *OutputPath)
					: FString::Printf(TEXT("Failed to export asset tree to %s"), *OutputPath);
			}

			LastExportedAssetCount.store(AssetCount, std::memory_order_release);
			bLastExportWasAssetTree.store(true, std::memory_order_release);
			bIsExporting.store(false, std::memory_order_release);
			ExportProgress.store(1.0f, std::memory_order_release);

			LevelContextExporter_SubsystemThreading::BroadcastAssetTreeExportComplete_GameThreadSafe(this, bSaved, OutputPath);
		});
	});
}
