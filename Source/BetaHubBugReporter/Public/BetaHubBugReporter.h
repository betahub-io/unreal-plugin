// Copyright (c) 2024-2026 Upsoft sp. z o. o.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FBetaHubBugReporterModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
