// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/**
 * Online subsystem module class  (Null Implementation)
 * Code related to the loading of the Null module
 */
class FOnlineSubsystemTheiaModule : public IModuleInterface
{
private:

	/** Class responsible for creating instance(s) of the subsystem */
	class FOnlineFactoryTheia* TheiaFactory;

public:

	FOnlineSubsystemTheiaModule() : 
		TheiaFactory(NULL)
	{}

	virtual ~FOnlineSubsystemTheiaModule() {}

	// IModuleInterface

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}

	virtual bool SupportsAutomaticShutdown() override
	{
		return false;
	}
};
