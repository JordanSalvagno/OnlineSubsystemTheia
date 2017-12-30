// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OnlineAsyncTaskManager.h"

/**
 *	Null version of the async task manager to register the various Null callbacks with the engine
 */
class FOnlineAsyncTaskManagerTheia : public FOnlineAsyncTaskManager
{
protected:

	/** Cached reference to the main online subsystem */
	class FOnlineSubsystemTheia* TheiaSubsystem;

public:

	FOnlineAsyncTaskManagerTheia(class FOnlineSubsystemTheia* InOnlineSubsystem)
		: TheiaSubsystem(InOnlineSubsystem)
	{
	}

	~FOnlineAsyncTaskManagerTheia() 
	{
	}

	// FOnlineAsyncTaskManager
	virtual void OnlineTick() override;
};
