// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelContextExporterModule.h"
#include "LevelContextExporterWindow.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "LevelContextExporter"

DEFINE_LOG_CATEGORY_STATIC(LogLevelContextExporter, Log, All);

const FName FLevelContextExporterModule::TabName(TEXT("LevelContextExporter"));

void FLevelContextExporterModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TabName,
		FOnSpawnTab::CreateRaw(this, &FLevelContextExporterModule::SpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Level Context Exporter"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	// TODO: Add keyboard shortcut for quick access
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FLevelContextExporterModule::RegisterMenuExtension));

	UE_LOG(LogLevelContextExporter, Log, TEXT("LevelContextExporter module initialized"));
}

void FLevelContextExporterModule::ShutdownModule()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
	UE_LOG(LogLevelContextExporter, Log, TEXT("LevelContextExporter module shut down"));
}

void FLevelContextExporterModule::RegisterMenuExtension()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& Section = Menu->FindOrAddSection("LevelContextExporter");

	Section.AddMenuEntry(
		"OpenLevelContextExporter",
		LOCTEXT("MenuEntry", "Level Context Exporter"),
		LOCTEXT("MenuEntryTooltip", "Export level actor data as JSON for LLM context"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(TabName);
		}))
	);
}

TSharedRef<SDockTab> FLevelContextExporterModule::SpawnTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SLevelContextExporterWindow)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FLevelContextExporterModule, LevelContextExporter)
