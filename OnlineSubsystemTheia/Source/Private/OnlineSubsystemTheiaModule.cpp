// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemTheiaModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "OnlineSubsystemModule.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemTheia.h"

IMPLEMENT_MODULE(FOnlineSubsystemTheiaModule, OnlineSubsystemTheia);

/**
 * Class responsible for creating instance(s) of the subsystem
 */
class FOnlineFactoryTheia : public IOnlineFactory
{
public:

	FOnlineFactoryTheia() {}
	virtual ~FOnlineFactoryTheia() {}

	virtual IOnlineSubsystemPtr CreateSubsystem(FName InstanceName)
	{
		FOnlineSubsystemTheiaPtr OnlineSub = MakeShareable(new FOnlineSubsystemTheia(InstanceName));
		if (OnlineSub->IsEnabled())
		{
			if(!OnlineSub->Init())
			{
				UE_LOG_ONLINE(Warning, TEXT("Null API failed to initialize!"));
				OnlineSub->Shutdown();
				OnlineSub = NULL;
			}
		}
		else
		{
			UE_LOG_ONLINE(Warning, TEXT("Null API disabled!"));
			OnlineSub->Shutdown();
			OnlineSub = NULL;
		}

		return OnlineSub;
	}
};

void FOnlineSubsystemTheiaModule::StartupModule()
{
	TheiaFactory = new FOnlineFactoryTheia();

	// Create and register our singleton factory with the main online subsystem for easy access
	FOnlineSubsystemModule& OSS = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
	OSS.RegisterPlatformService(THEIA_SUBSYSTEM, TheiaFactory);
}

void FOnlineSubsystemTheiaModule::ShutdownModule()
{
	FOnlineSubsystemModule& OSS = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
	OSS.UnregisterPlatformService(THEIA_SUBSYSTEM);
	
	delete TheiaFactory;
	TheiaFactory = NULL;
}
