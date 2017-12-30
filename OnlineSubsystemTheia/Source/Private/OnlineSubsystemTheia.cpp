// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemTheia.h"
#include "HAL/RunnableThread.h"
#include "OnlineAsyncTaskManagerTheia.h"

#include "OnlineSessionInterfaceTheia.h"
#include "OnlineLeaderboardInterfaceTheia.h"
#include "OnlineIdentityTheia.h"
#include "VoiceInterfaceImpl.h"
#include "OnlineAchievementsInterfaceTheia.h"

FThreadSafeCounter FOnlineSubsystemTheia::TaskCounter;

IOnlineSessionPtr FOnlineSubsystemTheia::GetSessionInterface() const
{
	return SessionInterface;
}

IOnlineFriendsPtr FOnlineSubsystemTheia::GetFriendsInterface() const
{
	return nullptr;
}

IOnlinePartyPtr FOnlineSubsystemTheia::GetPartyInterface() const
{
	return nullptr;
}

IOnlineGroupsPtr FOnlineSubsystemTheia::GetGroupsInterface() const
{
	return nullptr;
}

IOnlineSharedCloudPtr FOnlineSubsystemTheia::GetSharedCloudInterface() const
{
	return nullptr;
}

IOnlineUserCloudPtr FOnlineSubsystemTheia::GetUserCloudInterface() const
{
	return nullptr;
}

IOnlineEntitlementsPtr FOnlineSubsystemTheia::GetEntitlementsInterface() const
{
	return nullptr;
};

IOnlineLeaderboardsPtr FOnlineSubsystemTheia::GetLeaderboardsInterface() const
{
	return LeaderboardsInterface;
}

IOnlineVoicePtr FOnlineSubsystemTheia::GetVoiceInterface() const
{
	if (VoiceInterface.IsValid() && !bVoiceInterfaceInitialized)
	{	
		if (!VoiceInterface->Init())
		{
			VoiceInterface = nullptr;
		}

		bVoiceInterfaceInitialized = true;
	}

	return VoiceInterface;
}

IOnlineExternalUIPtr FOnlineSubsystemTheia::GetExternalUIInterface() const
{
	return nullptr;
}

IOnlineTimePtr FOnlineSubsystemTheia::GetTimeInterface() const
{
	return nullptr;
}

IOnlineIdentityPtr FOnlineSubsystemTheia::GetIdentityInterface() const
{
	return IdentityInterface;
}

IOnlineTitleFilePtr FOnlineSubsystemTheia::GetTitleFileInterface() const
{
	return nullptr;
}

IOnlineStorePtr FOnlineSubsystemTheia::GetStoreInterface() const
{
	return nullptr;
}

IOnlineEventsPtr FOnlineSubsystemTheia::GetEventsInterface() const
{
	return nullptr;
}

IOnlineAchievementsPtr FOnlineSubsystemTheia::GetAchievementsInterface() const
{
	return AchievementsInterface;
}

IOnlineSharingPtr FOnlineSubsystemTheia::GetSharingInterface() const
{
	return nullptr;
}

IOnlineUserPtr FOnlineSubsystemTheia::GetUserInterface() const
{
	return nullptr;
}

IOnlineMessagePtr FOnlineSubsystemTheia::GetMessageInterface() const
{
	return nullptr;
}

IOnlinePresencePtr FOnlineSubsystemTheia::GetPresenceInterface() const
{
	return nullptr;
}

IOnlineChatPtr FOnlineSubsystemTheia::GetChatInterface() const
{
	return nullptr;
}

IOnlineTurnBasedPtr FOnlineSubsystemTheia::GetTurnBasedInterface() const
{
    return nullptr;
}

bool FOnlineSubsystemTheia::Tick(float DeltaTime)
{
	if (!FOnlineSubsystemImpl::Tick(DeltaTime))
	{
		return false;
	}

	if (OnlineAsyncTaskThreadRunnable)
	{
		OnlineAsyncTaskThreadRunnable->GameTick();
	}

 	if (SessionInterface.IsValid())
 	{
 		SessionInterface->Tick(DeltaTime);
 	}

	if (VoiceInterface.IsValid() && bVoiceInterfaceInitialized)
	{
		VoiceInterface->Tick(DeltaTime);
	}

	return true;
}

bool FOnlineSubsystemTheia::Init()
{
	const bool bTheiaInit = true;
	
	if (bTheiaInit)
	{
		// Create the online async task thread
		OnlineAsyncTaskThreadRunnable = new FOnlineAsyncTaskManagerTheia(this);
		check(OnlineAsyncTaskThreadRunnable);
		OnlineAsyncTaskThread = FRunnableThread::Create(OnlineAsyncTaskThreadRunnable, *FString::Printf(TEXT("OnlineAsyncTaskThreadTheia %s(%d)"), *InstanceName.ToString(), TaskCounter.Increment()), 128 * 1024, TPri_Normal);
		check(OnlineAsyncTaskThread);
		UE_LOG_ONLINE(Verbose, TEXT("Created thread (ID:%d)."), OnlineAsyncTaskThread->GetThreadID());

 		SessionInterface = MakeShareable(new FOnlineSessionTheia(this));
		LeaderboardsInterface = MakeShareable(new FOnlineLeaderboardsTheia(this));
		IdentityInterface = MakeShareable(new FOnlineIdentityTheia(this));
		AchievementsInterface = MakeShareable(new FOnlineAchievementsTheia(this));
		VoiceInterface = MakeShareable(new FOnlineVoiceImpl(this));
	}
	else
	{
		Shutdown();
	}

	return bTheiaInit;
}

bool FOnlineSubsystemTheia::Shutdown()
{
	UE_LOG_ONLINE(Display, TEXT("FOnlineSubsystemNull::Shutdown()"));

	FOnlineSubsystemImpl::Shutdown();

	if (OnlineAsyncTaskThread)
	{
		// Destroy the online async task thread
		delete OnlineAsyncTaskThread;
		OnlineAsyncTaskThread = nullptr;
	}

	if (OnlineAsyncTaskThreadRunnable)
	{
		delete OnlineAsyncTaskThreadRunnable;
		OnlineAsyncTaskThreadRunnable = nullptr;
	}

	if (VoiceInterface.IsValid() && bVoiceInterfaceInitialized)
	{
		VoiceInterface->Shutdown();
	}
	
#define DESTRUCT_INTERFACE(Interface) \
 	if (Interface.IsValid()) \
 	{ \
 		ensure(Interface.IsUnique()); \
 		Interface = nullptr; \
 	}
 
 	// Destruct the interfaces
	DESTRUCT_INTERFACE(VoiceInterface);
	DESTRUCT_INTERFACE(AchievementsInterface);
	DESTRUCT_INTERFACE(IdentityInterface);
	DESTRUCT_INTERFACE(LeaderboardsInterface);
 	DESTRUCT_INTERFACE(SessionInterface);
	
#undef DESTRUCT_INTERFACE
	
	return true;
}

FString FOnlineSubsystemTheia::GetAppId() const
{
	return TEXT("");
}

bool FOnlineSubsystemTheia::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (FOnlineSubsystemImpl::Exec(InWorld, Cmd, Ar))
	{
		return true;
	}
	return false;
}

FText FOnlineSubsystemTheia::GetOnlineServiceName() const
{
	return NSLOCTEXT("OnlineSubsystemTheia", "OnlineServiceName", "Theia");
}

bool FOnlineSubsystemTheia::IsEnabled()
{
	return true;
}
