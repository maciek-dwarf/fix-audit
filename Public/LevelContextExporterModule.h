// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FLevelContextExporterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenuExtension();
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);

	static const FName TabName;
};
