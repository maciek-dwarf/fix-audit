// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class ULevelContextExporterSubsystem;
class STextBlock;
class SEditableTextBox;
class SProgressBar;

/**
 * Editor window for the Level Context Exporter.
 * Shows level statistics, export controls, and progress feedback.
 */
class SLevelContextExporterWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLevelContextExporterWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Update the progress display text (called by the subsystem during export) */
	void UpdateProgressText(const FString& InText);

private:
	void OnExportClicked();
	ULevelContextExporterSubsystem* GetSubsystem() const;

	// UI elements
	TSharedPtr<STextBlock> StatsTextBlock;
	TSharedPtr<STextBlock> ProgressTextBlock;
	TSharedPtr<STextBlock> StatusTextBlock;
	TSharedPtr<SEditableTextBox> OutputPathTextBox;
	TSharedPtr<SProgressBar> ExportProgressBar;

	// Cached display strings
	FString CurrentProgressText;
};
