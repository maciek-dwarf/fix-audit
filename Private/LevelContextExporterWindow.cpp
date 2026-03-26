// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelContextExporterWindow.h"
#include "LevelContextExporterSubsystem.h"
#include "Editor.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "LevelContextExporter"

void SLevelContextExporterWindow::Construct(const FArguments& InArgs)
{
	// Register with the subsystem so it can push progress updates to us
	ULevelContextExporterSubsystem* Subsystem = GetSubsystem();
	if (Subsystem)
	{
		Subsystem->SetWindowReference(this);
	}

	FString DefaultOutputPath = FPaths::ProjectSavedDir() / TEXT("LevelContext.json");
	FString DefaultAssetTreeOutputPath = FPaths::ProjectSavedDir() / TEXT("AssetTreeContext.json");

	ChildSlot
	[
		SNew(SVerticalBox)

		// --- Level Stats Header ---
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("StatsHeader", "Level Statistics"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
		]

		// --- Stats Display ---
		+ SVerticalBox::Slot()
		.FillHeight(0.35f)
		.Padding(8.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(StatsTextBlock, STextBlock)
				.Text(LOCTEXT("StatsLoading", "Gathering level stats..."))
			]
		]

		// --- Output Path ---
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("OutputPathLabel", "Output Path:"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				// TODO: Add a file browser button for output path selection
				SAssignNew(OutputPathTextBox, SEditableTextBox)
				.Text(FText::FromString(DefaultOutputPath))
			]
		]

		// --- Asset Tree Output Path ---
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AssetTreeOutputPathLabel", "Asset Tree Output Path:"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(AssetTreeOutputPathTextBox, SEditableTextBox)
				.Text(FText::FromString(DefaultAssetTreeOutputPath))
			]
		]

		// --- Export Button ---
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("ExportBtn", "Export Level Context"))
			.OnClicked(FOnClicked::CreateLambda([this]() -> FReply
			{
				OnExportClicked();
				return FReply::Handled();
			}))
		]

		// --- Export Asset Tree Button ---
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("ExportAssetTreeBtn", "Export Asset Tree Context"))
			.OnClicked(FOnClicked::CreateLambda([this]() -> FReply
			{
				OnExportAssetTreeClicked();
				return FReply::Handled();
			}))
		]

		// --- Progress Bar ---
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SAssignNew(ExportProgressBar, SProgressBar)
		]

		// --- Progress Text ---
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SAssignNew(ProgressTextBlock, STextBlock)
			.Text(LOCTEXT("ProgressIdle", "Idle"))
		]

		// --- Status / Log Area ---
		+ SVerticalBox::Slot()
		.FillHeight(0.25f)
		.Padding(8.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(StatusTextBlock, STextBlock)
				.Text(LOCTEXT("StatusReady", "Ready to export."))
			]
		]
	];
}

void SLevelContextExporterWindow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	ULevelContextExporterSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return;
	}

	// Refresh level statistics so the display stays current
	FLevelContextStats Stats = Subsystem->GatherLevelStats();

	// Format the stats for display
	FString StatsText;
	StatsText += FString::Printf(TEXT("Total Actors: %d\n"), Stats.TotalActorCount);
	StatsText += FString::Printf(TEXT("Estimated Tokens: ~%d\n\n"), Stats.EstimatedTokenCount);

	StatsText += TEXT("Class Breakdown:\n");
	for (const auto& Pair : Stats.ClassBreakdown)
	{
		StatsText += FString::Printf(TEXT("  %s: %d\n"), *Pair.Key, Pair.Value);
	}

	StatsText += FString::Printf(TEXT("\nUnique Asset References: %d\n"), Stats.UniqueAssetReferences.Num());
	for (const auto& AssetRef : Stats.UniqueAssetReferences)
	{
		StatsText += FString::Printf(TEXT("  %s\n"), *AssetRef);
	}

	StatsTextBlock->SetText(FText::FromString(StatsText));

	// Update progress bar and text during export
	ExportProgressBar->SetPercent(Subsystem->GetExportProgress());
	ProgressTextBlock->SetText(FText::FromString(CurrentProgressText));

	// Show completion info if export has finished
	if (!Subsystem->IsExporting() && Subsystem->GetExportProgress() >= 1.0f)
	{
		if (Subsystem->WasLastExportAssetTree())
		{
			StatusTextBlock->SetText(FText::FromString(Subsystem->GetLastAssetTreeExportResult()));
		}
		else
		{
			StatusTextBlock->SetText(FText::FromString(
				FString::Printf(TEXT("Export complete. %d actors exported."), Subsystem->LastExportedActors.Num())));
		}
	}
}

void SLevelContextExporterWindow::UpdateProgressText(const FString& InText)
{
	CurrentProgressText = InText;
}

void SLevelContextExporterWindow::OnExportClicked()
{
	ULevelContextExporterSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return;
	}

	FString OutputPath = OutputPathTextBox->GetText().ToString();
	StatusTextBlock->SetText(FText::FromString(TEXT("Starting export...")));
	CurrentProgressText = TEXT("Preparing...");

	Subsystem->ExportLevelContext(OutputPath);
}

void SLevelContextExporterWindow::OnExportAssetTreeClicked()
{
	ULevelContextExporterSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return;
	}

	const FString OutputPath = AssetTreeOutputPathTextBox->GetText().ToString();
	StatusTextBlock->SetText(FText::FromString(TEXT("Starting asset tree export...")));
	CurrentProgressText = TEXT("Preparing asset registry...");

	Subsystem->ExportAssetTreeContext(OutputPath);
}

ULevelContextExporterSubsystem* SLevelContextExporterWindow::GetSubsystem() const
{
	return GEditor->GetEditorSubsystem<ULevelContextExporterSubsystem>();
}

#undef LOCTEXT_NAMESPACE
