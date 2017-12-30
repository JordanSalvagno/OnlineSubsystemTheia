// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "OnlineSessionInterfaceTheia.h"
#include "Misc/Guid.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemTheia.h"
#include "OnlineSubsystemTheiaTypes.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineAsyncTaskManager.h"
#include "SocketSubsystem.h"
#include "NboSerializerTheia.h"
//#include "IPv4address.h"

FOnlineSessionInfoTheia::FOnlineSessionInfoTheia() :
	HostAddr(NULL),
	SessionId(TEXT("INVALID"))
{
}

void FOnlineSessionInfoTheia::Init(const FOnlineSubsystemTheia& Subsystem)
{
	// Read the IP from the system
	bool bCanBindAll;
	HostAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLocalHostAddr(*GLog, bCanBindAll);

	// The below is a workaround for systems that set hostname to a distinct address from 127.0.0.1 on a loopback interface.
	// See e.g. https://www.debian.org/doc/manuals/debian-reference/ch05.en.html#_the_hostname_resolution
	// and http://serverfault.com/questions/363095/why-does-my-hostname-appear-with-the-address-127-0-1-1-rather-than-127-0-0-1-in
	// Since we bind to 0.0.0.0, we won't answer on 127.0.1.1, so we need to advertise ourselves as 127.0.0.1 for any other loopback address we may have.
	uint32 HostIp = 0;
	HostAddr->GetIp(HostIp); // will return in host order
	// if this address is on loopback interface, advertise it as 127.0.0.1
	if ((HostIp & 0xff000000) == 0x7f000000)
	{
		HostAddr->SetIp(0x7f000001);	// 127.0.0.1
	}
	// Now set the port that was configured
	HostAddr->SetPort(GetPortFromNetDriver(Subsystem.GetInstanceName()));

	UE_LOG(LogTemp, Verbose, TEXT("OnlineSessionInterfaceDirect Init HostAddr is %s "), *HostAddr->ToString(true));

	FGuid OwnerGuid;
	FPlatformMisc::CreateGuid(OwnerGuid);
	SessionId = FUniqueNetIdString(OwnerGuid.ToString());
}

/**
 *	Async task for ending a Null online session
 */
class FOnlineAsyncTaskTheiaEndSession : public FOnlineAsyncTaskBasic<FOnlineSubsystemTheia>
{
private:
	/** Name of session ending */
	FName SessionName;

public:
	FOnlineAsyncTaskTheiaEndSession(class FOnlineSubsystemTheia* InSubsystem, FName InSessionName) :
		FOnlineAsyncTaskBasic(InSubsystem),
		SessionName(InSessionName)
	{
	}

	~FOnlineAsyncTaskTheiaEndSession()
	{
	}

	/**
	 *	Get a human readable description of task
	 */
	virtual FString ToString() const override
	{
		return FString::Printf(TEXT("FOnlineAsyncTaskTheiaEndSession bWasSuccessful: %d SessionName: %s"), bWasSuccessful, *SessionName.ToString());
	}

	/**
	 * Give the async task time to do its work
	 * Can only be called on the async task manager thread
	 */
	virtual void Tick() override
	{
		bIsComplete = true;
		bWasSuccessful = true;
	}

	/**
	 * Give the async task a chance to marshal its data back to the game thread
	 * Can only be called on the game thread by the async task manager
	 */
	virtual void Finalize() override
	{
		IOnlineSessionPtr SessionInt = Subsystem->GetSessionInterface();
		FNamedOnlineSession* Session = SessionInt->GetNamedSession(SessionName);
		if (Session)
		{
			Session->SessionState = EOnlineSessionState::Ended;
		}
	}

	/**
	 *	Async task is given a chance to trigger it's delegates
	 */
	virtual void TriggerDelegates() override
	{
		IOnlineSessionPtr SessionInt = Subsystem->GetSessionInterface();
		if (SessionInt.IsValid())
		{
			SessionInt->TriggerOnEndSessionCompleteDelegates(SessionName, bWasSuccessful);
		}
	}
};

/**
 *	Async task for destroying a Null online session
 */
class FOnlineAsyncTaskTheiaDestroySession : public FOnlineAsyncTaskBasic<FOnlineSubsystemTheia>
{
private:
	/** Name of session ending */
	FName SessionName;

public:
	FOnlineAsyncTaskTheiaDestroySession(class FOnlineSubsystemTheia* InSubsystem, FName InSessionName) :
		FOnlineAsyncTaskBasic(InSubsystem),
		SessionName(InSessionName)
	{
	}

	~FOnlineAsyncTaskTheiaDestroySession()
	{
	}

	/**
	 *	Get a human readable description of task
	 */
	virtual FString ToString() const override
	{
		return FString::Printf(TEXT("FOnlineAsyncTaskTheiaDestroySession bWasSuccessful: %d SessionName: %s"), bWasSuccessful, *SessionName.ToString());
	}

	/**
	 * Give the async task time to do its work
	 * Can only be called on the async task manager thread
	 */
	virtual void Tick() override
	{
		bIsComplete = true;
		bWasSuccessful = true;
	}

	/**
	 * Give the async task a chance to marshal its data back to the game thread
	 * Can only be called on the game thread by the async task manager
	 */
	virtual void Finalize() override
	{
		IOnlineSessionPtr SessionInt = Subsystem->GetSessionInterface();
		if (SessionInt.IsValid())
		{
			FNamedOnlineSession* Session = SessionInt->GetNamedSession(SessionName);
			if (Session)
			{
				SessionInt->RemoveNamedSession(SessionName);
			}
		}
	}

	/**
	 *	Async task is given a chance to trigger it's delegates
	 */
	virtual void TriggerDelegates() override
	{
		IOnlineSessionPtr SessionInt = Subsystem->GetSessionInterface();
		if (SessionInt.IsValid())
		{
			SessionInt->TriggerOnDestroySessionCompleteDelegates(SessionName, bWasSuccessful);
		}
	}
};

bool FOnlineSessionTheia::CreateSession(int32 HostingPlayerNum, FName SessionName, const FOnlineSessionSettings& NewSessionSettings)
{
	uint32 Result = E_FAIL;

	// Check for an existing session
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session == NULL)
	{
		// Create a new session and deep copy the game settings
		Session = AddNamedSession(SessionName, NewSessionSettings);
		check(Session);
		Session->SessionState = EOnlineSessionState::Creating;
		Session->NumOpenPrivateConnections = NewSessionSettings.NumPrivateConnections;
		Session->NumOpenPublicConnections = NewSessionSettings.NumPublicConnections;	// always start with full public connections, local player will register later

		Session->HostingPlayerNum = HostingPlayerNum;

		check(TheiaSubsystem);
		IOnlineIdentityPtr Identity = TheiaSubsystem->GetIdentityInterface();
		if (Identity.IsValid())
		{
			Session->OwningUserId = Identity->GetUniquePlayerId(HostingPlayerNum);
			Session->OwningUserName = Identity->GetPlayerNickname(HostingPlayerNum);
		}

		// if did not get a valid one, use just something
		if (!Session->OwningUserId.IsValid())
		{
			Session->OwningUserId = MakeShareable(new FUniqueNetIdString(FString::Printf(TEXT("%d"), HostingPlayerNum)));
			Session->OwningUserName = FString(TEXT("TheiaUser"));
		}
		
		// Unique identifier of this build for compatibility
		Session->SessionSettings.BuildUniqueId = GetBuildUniqueId();

		// Setup the host session info
		FOnlineSessionInfoTheia* NewSessionInfo = new FOnlineSessionInfoTheia();
		NewSessionInfo->Init(*TheiaSubsystem);
		Session->SessionInfo = MakeShareable(NewSessionInfo);

		Result = UpdateTheiaStatus();

		if (Result != ERROR_IO_PENDING)
		{
			// Set the game state as pending (not started)
			Session->SessionState = EOnlineSessionState::Pending;

			if (Result != ERROR_SUCCESS)
			{
				// Clean up the session info so we don't get into a confused state
				RemoveNamedSession(SessionName);
			}
			else
			{
				RegisterLocalPlayers(Session);
			}
		}
	}
	else
	{
		UE_LOG_ONLINE(Warning, TEXT("Cannot create session '%s': session already exists."), *SessionName.ToString());
	}

	if (Result != ERROR_IO_PENDING)
	{
		TriggerOnCreateSessionCompleteDelegates(SessionName, (Result == ERROR_SUCCESS) ? true : false);
	}
	
	return Result == ERROR_IO_PENDING || Result == ERROR_SUCCESS;
}

bool FOnlineSessionTheia::CreateSession(const FUniqueNetId& HostingPlayerId, FName SessionName, const FOnlineSessionSettings& NewSessionSettings)
{
	// todo: use proper	HostingPlayerId
	return CreateSession(0, SessionName, NewSessionSettings);
}

bool FOnlineSessionTheia::NeedsToAdvertise()
{
	FScopeLock ScopeLock(&SessionLock);

	bool bResult = false;
	for (int32 SessionIdx=0; SessionIdx < Sessions.Num(); SessionIdx++)
	{
		FNamedOnlineSession& Session = Sessions[SessionIdx];
		if (NeedsToAdvertise(Session))
		{
			bResult = true;
			break;
		}
	}

	return bResult;
}

bool FOnlineSessionTheia::NeedsToAdvertise( FNamedOnlineSession& Session )
{
	// In Null, we have to imitate missing online service functionality, so we advertise:
	// a) LAN match with open public connections (same as usually)
	// b) Not started public LAN session (same as usually)
	// d) Joinable presence-enabled session that would be advertised with in an online service
	// (all of that only if we're server)
	return Session.SessionSettings.bShouldAdvertise && IsHost(Session) &&
		(
			(
			  Session.SessionSettings.bIsLANMatch && 			  
			  (Session.SessionState != EOnlineSessionState::InProgress || (Session.SessionSettings.bAllowJoinInProgress && Session.NumOpenPublicConnections > 0))
			) 
			||
			(
				Session.SessionSettings.bAllowJoinViaPresence || Session.SessionSettings.bAllowJoinViaPresenceFriendsOnly
			)
		);		
}

bool FOnlineSessionTheia::IsSessionJoinable(const FNamedOnlineSession& Session) const
{
	const FOnlineSessionSettings& Settings = Session.SessionSettings;

	// LAN beacons are implicitly advertised.
	const bool bIsAdvertised = Settings.bShouldAdvertise || Settings.bIsLANMatch;
	const bool bIsMatchInProgress = Session.SessionState == EOnlineSessionState::InProgress;

	const bool bJoinableFromProgress = (!bIsMatchInProgress || Settings.bAllowJoinInProgress);

	const bool bAreSpacesAvailable = Session.NumOpenPublicConnections > 0;

	// LAN matches don't care about private connections / invites.
	// LAN matches don't care about presence information.
	return bIsAdvertised && bJoinableFromProgress && bAreSpacesAvailable;
}


uint32 FOnlineSessionTheia::UpdateTheiaStatus()
{
	uint32 Result = ERROR_SUCCESS;

	if ( NeedsToAdvertise() )
	{
		// set up LAN session
		if (TheiaSessionManager.GetBeaconState() == ELanBeaconState::NotUsingLanBeacon)
		{
			TheiaSessionManager.IsLANMatch = false;
			FURL DefaultURL;
			DefaultURL.LoadURLConfig(TEXT("DefaultPlayer"), GGameIni);

			HostSessionPort = DefaultURL.Port + 1;
		//	HostSessionPort = DefaultURL.Port;

			FOnValidQueryPacketDelegate QueryPacketDelegate = FOnValidQueryPacketDelegate::CreateRaw(this, &FOnlineSessionTheia::OnValidQueryPacketReceived);
			FOnPortChangedDelegate PortChangedDelegate = FOnPortChangedDelegate::CreateRaw(this, &FOnlineSessionTheia::OnSessionListenPortChanged);

			if (!TheiaSessionManager.Host(QueryPacketDelegate, HostSessionPort))
			{
				Result = E_FAIL;

				TheiaSessionManager.StopTheiaSession();
			}
		}
	}
	else
	{
		if (TheiaSessionManager.GetBeaconState() != ELanBeaconState::Searching)
		{
			if ((Sessions.Num() > 0) && Sessions[0].SessionSettings.bIsLANMatch)
			{
				TheiaSessionManager.IsLANMatch = true;

				FOnValidQueryPacketDelegate QueryPacketDelegate = FOnValidQueryPacketDelegate::CreateRaw(this, &FOnlineSessionTheia::OnValidQueryPacketReceived);
				FOnPortChangedDelegate PortChangedDelegate = FOnPortChangedDelegate::CreateRaw(this, &FOnlineSessionTheia::OnSessionListenPortChanged);
				//TODO: if its a LAN Connection just send port 1 for now, maybe change this...
				int32 TempPort = -1;
				if (!TheiaSessionManager.Host(QueryPacketDelegate, TempPort))
				{
					Result = E_FAIL;

					TheiaSessionManager.StopTheiaSession();
				}

			}
		}
	}

	return Result;
}

void FOnlineSessionTheia::OnSessionListenPortChanged(int32 Port)
{
	HostSessionPort = Port;
}

bool FOnlineSessionTheia::StartSession(FName SessionName)
{
	uint32 Result = E_FAIL;
	// Grab the session information by name
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		// Can't start a match multiple times
		if (Session->SessionState == EOnlineSessionState::Pending ||
			Session->SessionState == EOnlineSessionState::Ended)
		{
			// If this lan match has join in progress disabled, shut down the beacon
			Result = UpdateTheiaStatus();
			Session->SessionState = EOnlineSessionState::InProgress;
		}
		else
		{
			UE_LOG_ONLINE(Warning,	TEXT("Can't start an online session (%s) in state %s"),
				*SessionName.ToString(),
				EOnlineSessionState::ToString(Session->SessionState));
		}
	}
	else
	{
		UE_LOG_ONLINE(Warning, TEXT("Can't start an online game for session (%s) that hasn't been created"), *SessionName.ToString());
	}

	if (Result != ERROR_IO_PENDING)
	{
		// Just trigger the delegate
		TriggerOnStartSessionCompleteDelegates(SessionName, (Result == ERROR_SUCCESS) ? true : false);
	}

	return Result == ERROR_SUCCESS || Result == ERROR_IO_PENDING;
}

int32 FOnlineSessionTheia::GetPort()
{
	return HostSessionPort;
}

bool FOnlineSessionTheia::UpdateSession(FName SessionName, FOnlineSessionSettings& UpdatedSessionSettings, bool bShouldRefreshOnlineData)
{
	bool bWasSuccessful = true;

	// Grab the session information by name
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		// @TODO ONLINE update LAN settings
		Session->SessionSettings = UpdatedSessionSettings;
		TriggerOnUpdateSessionCompleteDelegates(SessionName, bWasSuccessful);
	}

	return bWasSuccessful;
}

bool FOnlineSessionTheia::EndSession(FName SessionName)
{
	uint32 Result = E_FAIL;

	// Grab the session information by name
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		// Can't end a match that isn't in progress
		if (Session->SessionState == EOnlineSessionState::InProgress)
		{
			Session->SessionState = EOnlineSessionState::Ended;

			// If the session should be advertised and the lan beacon was destroyed, recreate
			Result = UpdateTheiaStatus();
		}
		else
		{
			UE_LOG_ONLINE(Warning, TEXT("Can't end session (%s) in state %s"),
				*SessionName.ToString(),
				EOnlineSessionState::ToString(Session->SessionState));
		}
	}
	else
	{
		UE_LOG_ONLINE(Warning, TEXT("Can't end an online game for session (%s) that hasn't been created"),
			*SessionName.ToString());
	}

	if (Result != ERROR_IO_PENDING)
	{
		if (Session)
		{
			Session->SessionState = EOnlineSessionState::Ended;
		}

		TriggerOnEndSessionCompleteDelegates(SessionName, (Result == ERROR_SUCCESS) ? true : false);
	}

	return Result == ERROR_SUCCESS || Result == ERROR_IO_PENDING;
}

bool FOnlineSessionTheia::DestroySession(FName SessionName, const FOnDestroySessionCompleteDelegate& CompletionDelegate)
{
	uint32 Result = E_FAIL;
	// Find the session in question
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		// The session info is no longer needed
		RemoveNamedSession(Session->SessionName);

		Result = UpdateTheiaStatus();
	}
	else
	{
		UE_LOG_ONLINE(Warning, TEXT("Can't destroy a null online session (%s)"), *SessionName.ToString());
	}

	if (Result != ERROR_IO_PENDING)
	{
		CompletionDelegate.ExecuteIfBound(SessionName, (Result == ERROR_SUCCESS) ? true : false);
		TriggerOnDestroySessionCompleteDelegates(SessionName, (Result == ERROR_SUCCESS) ? true : false);
	}

	return Result == ERROR_SUCCESS || Result == ERROR_IO_PENDING;
}

bool FOnlineSessionTheia::IsPlayerInSession(FName SessionName, const FUniqueNetId& UniqueId)
{
	return IsPlayerInSessionImpl(this, SessionName, UniqueId);
}

bool FOnlineSessionTheia::StartMatchmaking(const TArray< TSharedRef<const FUniqueNetId> >& LocalPlayers, FName SessionName, const FOnlineSessionSettings& NewSessionSettings, TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	UE_LOG(LogOnline, Warning, TEXT("StartMatchmaking is not supported on this platform. Use FindSessions or FindSessionById."));
	TriggerOnMatchmakingCompleteDelegates(SessionName, false);
	return false;
}

bool FOnlineSessionTheia::CancelMatchmaking(int32 SearchingPlayerNum, FName SessionName)
{
	UE_LOG(LogOnline, Warning, TEXT("CancelMatchmaking is not supported on this platform. Use CancelFindSessions."));
	TriggerOnCancelMatchmakingCompleteDelegates(SessionName, false);
	return false;
}

bool FOnlineSessionTheia::CancelMatchmaking(const FUniqueNetId& SearchingPlayerId, FName SessionName)
{
	UE_LOG(LogOnline, Warning, TEXT("CancelMatchmaking is not supported on this platform. Use CancelFindSessions."));
	TriggerOnCancelMatchmakingCompleteDelegates(SessionName, false);
	return false;
}

bool FOnlineSessionTheia::FindSessions(int32 SearchingPlayerNum, const TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	uint32 Return = E_FAIL;
	//int32 HostSessionAddr = 0x345a2aee;
	//int32 HostSessionPort = 7777;

	// Don't start another search while one is in progress
	if (!CurrentSessionSearch.IsValid() && SearchSettings->SearchState != EOnlineAsyncTaskState::InProgress)
	{
		// Free up previous results
		SearchSettings->SearchResults.Empty();

		// Copy the search pointer so we can keep it around
		CurrentSessionSearch = SearchSettings;

		// remember the time at which we started search, as this will be used for a "good enough" ping estimation
		SessionSearchStartInSeconds = FPlatformTime::Seconds();

		if (!SearchSettings->bIsLanQuery)
		{
			UE_LOG(LogOnline, Warning, TEXT("Searching for online session"))
			TheiaSessionManager.IsLANMatch = false;
			//SetHostAddr(SearchSettings);
			//SetHostPort(SearchSettings);
	//int32 HostSessionAddr = 0x345a2aee;
	//int32 HostSessionPort = 7777;
			TheiaSessionManager.HostSessionPort = 7777;
			TheiaSessionManager.HostSessionAddr = 0x345a2aee;

			FURL DefaultURL;
			DefaultURL.LoadURLConfig(TEXT("DefaultPlayer"), GGameIni);

			TheiaSessionManager.ClientSessionPort = DefaultURL.Port + 2;
		}
		else
		{
			TheiaSessionManager.IsLANMatch = true;
		}
		UE_LOG(LogOnline, Warning, TEXT("executing FindLanSession"))

		// Check if its a LAN query
		Return = FindTheiaSession();

		if (Return == ERROR_IO_PENDING)
		{
			SearchSettings->SearchState = EOnlineAsyncTaskState::InProgress;
		}
	}
	else
	{
		UE_LOG_ONLINE(Warning, TEXT("Ignoring game search request while one is pending"));
		Return = ERROR_IO_PENDING;
	}

	return Return == ERROR_SUCCESS || Return == ERROR_IO_PENDING;
}

bool FOnlineSessionTheia::FindSessions(const FUniqueNetId& SearchingPlayerId, const TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	// This function doesn't use the SearchingPlayerNum parameter, so passing in anything is fine.
	return FindSessions(0, SearchSettings);
}

bool FOnlineSessionTheia::FindSessionById(const FUniqueNetId& SearchingUserId, const FUniqueNetId& SessionId, const FUniqueNetId& FriendId, const FOnSingleSessionResultCompleteDelegate& CompletionDelegates)
{
	FOnlineSessionSearchResult EmptyResult;
	CompletionDelegates.ExecuteIfBound(0, false, EmptyResult);
	return true;
}

uint32 FOnlineSessionTheia::FindTheiaSession()
{
	uint32 Return = ERROR_IO_PENDING;

	UE_LOG(LogOnline, Warning, TEXT("Inside FindLanSession"))

	// Recreate the unique identifier for this client
	GenerateNonce((uint8*)&TheiaSessionManager.TheiaNonce, 8);

	FOnValidResponsePacketDelegate ResponseDelegate = FOnValidResponsePacketDelegate::CreateRaw(this, &FOnlineSessionTheia::OnValidResponsePacketReceived);
	FOnSearchingTimeoutDelegate TimeoutDelegate = FOnSearchingTimeoutDelegate::CreateRaw(this, &FOnlineSessionTheia::OnTheiaSearchTimeout);

	FNboSerializeToBufferTheia Packet(LAN_BEACON_MAX_PACKET_SIZE);
	TheiaSessionManager.CreateClientQueryPacket(Packet, TheiaSessionManager.TheiaNonce);
	if (TheiaSessionManager.Search(Packet, ResponseDelegate, TimeoutDelegate) == false)
	{
		Return = E_FAIL;

		FinalizeTheiaSearch();

		CurrentSessionSearch->SearchState = EOnlineAsyncTaskState::Failed;
		
		// Just trigger the delegate as having failed
		TriggerOnFindSessionsCompleteDelegates(false);
	}
	return Return;
}

bool FOnlineSessionTheia::CancelFindSessions()
{
	uint32 Return = E_FAIL;
	if (CurrentSessionSearch.IsValid() && CurrentSessionSearch->SearchState == EOnlineAsyncTaskState::InProgress)
	{
		// Make sure it's the right type
		Return = ERROR_SUCCESS;

		FinalizeTheiaSearch();

		CurrentSessionSearch->SearchState = EOnlineAsyncTaskState::Failed;
		CurrentSessionSearch = NULL;
	}
	else
	{
		UE_LOG_ONLINE(Warning, TEXT("Can't cancel a search that isn't in progress"));
	}

	if (Return != ERROR_IO_PENDING)
	{
		TriggerOnCancelFindSessionsCompleteDelegates(true);
	}

	return Return == ERROR_SUCCESS || Return == ERROR_IO_PENDING;
}

bool FOnlineSessionTheia::JoinSession(int32 PlayerNum, FName SessionName, const FOnlineSessionSearchResult& DesiredSession)
{
	uint32 Return = E_FAIL;
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	UE_LOG(LogOnline, Warning, TEXT("inside JoinSession"))
	// Don't join a session if already in one or hosting one
	if (Session == NULL)
	{
		// Create a named session from the search result data
		Session = AddNamedSession(SessionName, DesiredSession.Session);
		Session->HostingPlayerNum = PlayerNum;
		UE_LOG(LogOnline, Warning, TEXT("Joining session"))

		// Create Internet or LAN match
		FOnlineSessionInfoTheia* NewSessionInfo = new FOnlineSessionInfoTheia();
		Session->SessionInfo = MakeShareable(NewSessionInfo);

		Return = JoinTheiaSession(PlayerNum, Session, &DesiredSession.Session);

		// turn off advertising on Join, to avoid clients advertising it over LAN
		Session->SessionSettings.bShouldAdvertise = false;

		if (Return != ERROR_IO_PENDING)
		{
			if (Return != ERROR_SUCCESS)
			{
				// Clean up the session info so we don't get into a confused state
				RemoveNamedSession(SessionName);
			}
			else
			{
				RegisterLocalPlayers(Session);
			}
		}
	}
	else
	{
		UE_LOG_ONLINE(Warning, TEXT("Session (%s) already exists, can't join twice"), *SessionName.ToString());
	}

	if (Return != ERROR_IO_PENDING)
	{
		// Just trigger the delegate as having failed
		TriggerOnJoinSessionCompleteDelegates(SessionName, Return == ERROR_SUCCESS ? EOnJoinSessionCompleteResult::Success : EOnJoinSessionCompleteResult::UnknownError);
	}

	return Return == ERROR_SUCCESS || Return == ERROR_IO_PENDING;
}

bool FOnlineSessionTheia::JoinSession(const FUniqueNetId& PlayerId, FName SessionName, const FOnlineSessionSearchResult& DesiredSession)
{
	// Assuming player 0 should be OK here
	return JoinSession(0, SessionName, DesiredSession);
}

bool FOnlineSessionTheia::FindFriendSession(int32 LocalUserNum, const FUniqueNetId& Friend)
{
	// this function has to exist due to interface definition, but it does not have a meaningful implementation in Null subsystem
	TArray<FOnlineSessionSearchResult> EmptySearchResult;
	TriggerOnFindFriendSessionCompleteDelegates(LocalUserNum, false, EmptySearchResult);
	return false;
};

bool FOnlineSessionTheia::FindFriendSession(const FUniqueNetId& LocalUserId, const FUniqueNetId& Friend)
{
	// this function has to exist due to interface definition, but it does not have a meaningful implementation in Null subsystem
	TArray<FOnlineSessionSearchResult> EmptySearchResult;
	TriggerOnFindFriendSessionCompleteDelegates(0, false, EmptySearchResult);
	return false;
}

bool FOnlineSessionTheia::FindFriendSession(const FUniqueNetId& LocalUserId, const TArray<TSharedRef<const FUniqueNetId>>& FriendList)
{
	// this function has to exist due to interface definition, but it does not have a meaningful implementation in Null subsystem
	TArray<FOnlineSessionSearchResult> EmptySearchResult;
	TriggerOnFindFriendSessionCompleteDelegates(0, false, EmptySearchResult);
	return false;
}

bool FOnlineSessionTheia::SendSessionInviteToFriend(int32 LocalUserNum, FName SessionName, const FUniqueNetId& Friend)
{
	// this function has to exist due to interface definition, but it does not have a meaningful implementation in Null subsystem
	return false;
};

bool FOnlineSessionTheia::SendSessionInviteToFriend(const FUniqueNetId& LocalUserId, FName SessionName, const FUniqueNetId& Friend)
{
	// this function has to exist due to interface definition, but it does not have a meaningful implementation in Null subsystem
	return false;
}

bool FOnlineSessionTheia::SendSessionInviteToFriends(int32 LocalUserNum, FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Friends)
{
	// this function has to exist due to interface definition, but it does not have a meaningful implementation in Null subsystem
	return false;
};

bool FOnlineSessionTheia::SendSessionInviteToFriends(const FUniqueNetId& LocalUserId, FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Friends)
{
	// this function has to exist due to interface definition, but it does not have a meaningful implementation in Null subsystem
	return false;
}

uint32 FOnlineSessionTheia::JoinTheiaSession(int32 PlayerNum, FNamedOnlineSession* Session, const FOnlineSession* SearchSession)
{
	check(Session != nullptr);
	UE_LOG(LogOnline, Warning, TEXT("In JoinLanSession"))

	uint32 Result = E_FAIL;
	Session->SessionState = EOnlineSessionState::Pending;

	if (Session->SessionInfo.IsValid() && SearchSession != nullptr && SearchSession->SessionInfo.IsValid())
	{
		UE_LOG(LogOnline, Warning, TEXT("Session valid"))
		// Copy the session info over
		const FOnlineSessionInfoTheia* SearchSessionInfo = (const FOnlineSessionInfoTheia*)SearchSession->SessionInfo.Get();
		FOnlineSessionInfoTheia* SessionInfo = (FOnlineSessionInfoTheia*)Session->SessionInfo.Get();
		SessionInfo->SessionId = SearchSessionInfo->SessionId;

		if (!TheiaSessionManager.IsLANMatch)
		{
			if (TheiaSessionManager.HostSessionAddr)
			{
				uint32 IpAddr = TheiaSessionManager.HostSessionAddr;

				SessionInfo->HostAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr(IpAddr, SearchSessionInfo->HostAddr->GetPort());

				UE_LOG(LogOnline, Verbose, TEXT("OnlineSessionInterfaceDirect JoinLANSession Online Session to join HostAdrr is %s "), *SearchSessionInfo->HostAddr->ToString(true));

				Result = ERROR_SUCCESS;

			}
		}
		else
		{
			uint32 IpAddr;
			SearchSessionInfo->HostAddr->GetIp(IpAddr);
			SessionInfo->HostAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr(IpAddr, SearchSessionInfo->HostAddr->GetPort());

			UE_LOG(LogOnline, Verbose, TEXT("OnlineSessionInterfaceDirect JoinLANSession Session to join HostAdrr is %s "), *SearchSessionInfo->HostAddr->ToString(true));

			Result = ERROR_SUCCESS;
		}
	}

	return Result;
}

bool FOnlineSessionTheia::PingSearchResults(const FOnlineSessionSearchResult& SearchResult)
{
	return false;
}

/** Get a resolved connection string from a session info */
static bool GetConnectStringFromSessionInfo(TSharedPtr<FOnlineSessionInfoTheia>& SessionInfo, FString& ConnectInfo, int32 PortOverride=0)
{
	bool bSuccess = false;
	if (SessionInfo.IsValid())
	{
		if (SessionInfo->HostAddr.IsValid() && SessionInfo->HostAddr->IsValid())
		{
			if (PortOverride != 0)
			{
				ConnectInfo = FString::Printf(TEXT("%s:%d"), *SessionInfo->HostAddr->ToString(false), PortOverride);
			}
			else
			{
				ConnectInfo = FString::Printf(TEXT("%s"), *SessionInfo->HostAddr->ToString(true));
			}

			bSuccess = true;
		}
	}

	return bSuccess;
}

bool FOnlineSessionTheia::GetResolvedConnectString(FName SessionName, FString& ConnectInfo, FName PortType)
{
	bool bSuccess = false;
	// Find the session
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session != NULL)
	{
		TSharedPtr<FOnlineSessionInfoTheia> SessionInfo = StaticCastSharedPtr<FOnlineSessionInfoTheia>(Session->SessionInfo);
		if (PortType == NAME_BeaconPort)
		{
			int32 BeaconListenPort = GetBeaconPortFromSessionSettings(Session->SessionSettings);
			bSuccess = GetConnectStringFromSessionInfo(SessionInfo, ConnectInfo, BeaconListenPort);
		}
		else if (PortType == NAME_GamePort)
		{
			bSuccess = GetConnectStringFromSessionInfo(SessionInfo, ConnectInfo);
		}

		if (!bSuccess)
		{
			UE_LOG_ONLINE(Warning, TEXT("Invalid session info for session %s in GetResolvedConnectString()"), *SessionName.ToString());
		}
	}
	else
	{
		UE_LOG_ONLINE(Warning,
			TEXT("Unknown session name (%s) specified to GetResolvedConnectString()"),
			*SessionName.ToString());
	}

	return bSuccess;
}

bool FOnlineSessionTheia::GetResolvedConnectString(const FOnlineSessionSearchResult& SearchResult, FName PortType, FString& ConnectInfo)
{
	bool bSuccess = false;
	if (SearchResult.Session.SessionInfo.IsValid())
	{
		TSharedPtr<FOnlineSessionInfoTheia> SessionInfo = StaticCastSharedPtr<FOnlineSessionInfoTheia>(SearchResult.Session.SessionInfo);

		if (PortType == NAME_BeaconPort)
		{
			int32 BeaconListenPort = GetBeaconPortFromSessionSettings(SearchResult.Session.SessionSettings);
			bSuccess = GetConnectStringFromSessionInfo(SessionInfo, ConnectInfo, BeaconListenPort);

		}
		else if (PortType == NAME_GamePort)
		{
			bSuccess = GetConnectStringFromSessionInfo(SessionInfo, ConnectInfo);
		}
	}
	
	if (!bSuccess || ConnectInfo.IsEmpty())
	{
		UE_LOG_ONLINE(Warning, TEXT("Invalid session info in search result to GetResolvedConnectString()"));
	}

	return bSuccess;
}

FOnlineSessionSettings* FOnlineSessionTheia::GetSessionSettings(FName SessionName) 
{
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		return &Session->SessionSettings;
	}
	return NULL;
}

void FOnlineSessionTheia::RegisterLocalPlayers(FNamedOnlineSession* Session)
{
	if (!TheiaSubsystem->IsDedicated())
	{
		IOnlineVoicePtr VoiceInt = TheiaSubsystem->GetVoiceInterface();
		if (VoiceInt.IsValid())
		{
			for (int32 Index = 0; Index < MAX_LOCAL_PLAYERS; Index++)
			{
				// Register the local player as a local talker
				VoiceInt->RegisterLocalTalker(Index);
			}
		}
	}
}

void FOnlineSessionTheia::RegisterVoice(const FUniqueNetId& PlayerId)
{
	IOnlineVoicePtr VoiceInt = TheiaSubsystem->GetVoiceInterface();
	if (VoiceInt.IsValid())
	{
		if (!TheiaSubsystem->IsLocalPlayer(PlayerId))
		{
			VoiceInt->RegisterRemoteTalker(PlayerId);
		}
		else
		{
			// This is a local player. In case their PlayerState came last during replication, reprocess muting
			VoiceInt->ProcessMuteChangeNotification();
		}
	}
}

void FOnlineSessionTheia::UnregisterVoice(const FUniqueNetId& PlayerId)
{
	IOnlineVoicePtr VoiceInt = TheiaSubsystem->GetVoiceInterface();
	if (VoiceInt.IsValid())
	{
		if (!TheiaSubsystem->IsLocalPlayer(PlayerId))
		{
			if (VoiceInt.IsValid())
			{
				VoiceInt->UnregisterRemoteTalker(PlayerId);
			}
		}
	}
}

bool FOnlineSessionTheia::RegisterPlayer(FName SessionName, const FUniqueNetId& PlayerId, bool bWasInvited)
{
	TArray< TSharedRef<const FUniqueNetId> > Players;
	Players.Add(MakeShareable(new FUniqueNetIdString(PlayerId)));
	return RegisterPlayers(SessionName, Players, bWasInvited);
}

bool FOnlineSessionTheia::RegisterPlayers(FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Players, bool bWasInvited)
{
	bool bSuccess = false;
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		bSuccess = true;

		for (int32 PlayerIdx=0; PlayerIdx<Players.Num(); PlayerIdx++)
		{
			const TSharedRef<const FUniqueNetId>& PlayerId = Players[PlayerIdx];

			FUniqueNetIdMatcher PlayerMatch(*PlayerId);
			if (Session->RegisteredPlayers.IndexOfByPredicate(PlayerMatch) == INDEX_NONE)
			{
				Session->RegisteredPlayers.Add(PlayerId);
				RegisterVoice(*PlayerId);

				// update number of open connections
				if (Session->NumOpenPublicConnections > 0)
				{
					Session->NumOpenPublicConnections--;
				}
				else if (Session->NumOpenPrivateConnections > 0)
				{
					Session->NumOpenPrivateConnections--;
				}
			}
			else
			{
				RegisterVoice(*PlayerId);
				UE_LOG_ONLINE(Log, TEXT("Player %s already registered in session %s"), *PlayerId->ToDebugString(), *SessionName.ToString());
			}			
		}
	}
	else
	{
		UE_LOG_ONLINE(Warning, TEXT("No game present to join for session (%s)"), *SessionName.ToString());
	}

	TriggerOnRegisterPlayersCompleteDelegates(SessionName, Players, bSuccess);
	return bSuccess;
}

bool FOnlineSessionTheia::UnregisterPlayer(FName SessionName, const FUniqueNetId& PlayerId)
{
	TArray< TSharedRef<const FUniqueNetId> > Players;
	Players.Add(MakeShareable(new FUniqueNetIdString(PlayerId)));
	return UnregisterPlayers(SessionName, Players);
}

bool FOnlineSessionTheia::UnregisterPlayers(FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Players)
{
	bool bSuccess = true;

	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		for (int32 PlayerIdx=0; PlayerIdx < Players.Num(); PlayerIdx++)
		{
			const TSharedRef<const FUniqueNetId>& PlayerId = Players[PlayerIdx];

			FUniqueNetIdMatcher PlayerMatch(*PlayerId);
			int32 RegistrantIndex = Session->RegisteredPlayers.IndexOfByPredicate(PlayerMatch);
			if (RegistrantIndex != INDEX_NONE)
			{
				Session->RegisteredPlayers.RemoveAtSwap(RegistrantIndex);
				UnregisterVoice(*PlayerId);

				// update number of open connections
				if (Session->NumOpenPublicConnections < Session->SessionSettings.NumPublicConnections)
				{
					Session->NumOpenPublicConnections++;
				}
				else if (Session->NumOpenPrivateConnections < Session->SessionSettings.NumPrivateConnections)
				{
					Session->NumOpenPrivateConnections++;
				}
			}
			else
			{
				UE_LOG_ONLINE(Warning, TEXT("Player %s is not part of session (%s)"), *PlayerId->ToDebugString(), *SessionName.ToString());
			}
		}
	}
	else
	{
		UE_LOG_ONLINE(Warning, TEXT("No game present to leave for session (%s)"), *SessionName.ToString());
		bSuccess = false;
	}

	TriggerOnUnregisterPlayersCompleteDelegates(SessionName, Players, bSuccess);
	return bSuccess;
}

void FOnlineSessionTheia::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_Session_Interface);
	TickLanTasks(DeltaTime);
}

void FOnlineSessionTheia::TickLanTasks(float DeltaTime)
{
	TheiaSessionManager.Tick(DeltaTime);
}

void FOnlineSessionTheia::AppendSessionToPacket(FNboSerializeToBufferTheia& Packet, FOnlineSession* Session)
{
	/** Owner of the session */
	Packet << *StaticCastSharedPtr<const FUniqueNetIdString>(Session->OwningUserId)
		<< Session->OwningUserName
		<< Session->NumOpenPrivateConnections
		<< Session->NumOpenPublicConnections;

	// Try to get the actual port the netdriver is using
	SetPortFromNetDriver(*TheiaSubsystem, Session->SessionInfo);

	// Write host info (host addr, session id, and key)
	Packet << *StaticCastSharedPtr<FOnlineSessionInfoTheia>(Session->SessionInfo);

	// Now append per game settings
	AppendSessionSettingsToPacket(Packet, &Session->SessionSettings);
}

void FOnlineSessionTheia::AppendSessionSettingsToPacket(FNboSerializeToBufferTheia& Packet, FOnlineSessionSettings* SessionSettings)
{
#if DEBUG_LAN_BEACON
	UE_LOG_ONLINE(Verbose, TEXT("Sending session settings to client"));
#endif 

	// Members of the session settings class
	Packet << SessionSettings->NumPublicConnections
		<< SessionSettings->NumPrivateConnections
		<< (uint8)SessionSettings->bShouldAdvertise
		<< (uint8)SessionSettings->bIsLANMatch
		<< (uint8)SessionSettings->bIsDedicated
		<< (uint8)SessionSettings->bUsesStats
		<< (uint8)SessionSettings->bAllowJoinInProgress
		<< (uint8)SessionSettings->bAllowInvites
		<< (uint8)SessionSettings->bUsesPresence
		<< (uint8)SessionSettings->bAllowJoinViaPresence
		<< (uint8)SessionSettings->bAllowJoinViaPresenceFriendsOnly
		<< (uint8)SessionSettings->bAntiCheatProtected
	    << SessionSettings->BuildUniqueId;

	// First count number of advertised keys
	int32 NumAdvertisedProperties = 0;
	for (FSessionSettings::TConstIterator It(SessionSettings->Settings); It; ++It)
	{	
		const FOnlineSessionSetting& Setting = It.Value();
		if (Setting.AdvertisementType >= EOnlineDataAdvertisementType::ViaOnlineService)
		{
			NumAdvertisedProperties++;
		}
	}

	// Add count of advertised keys and the data
	Packet << (int32)NumAdvertisedProperties;
	for (FSessionSettings::TConstIterator It(SessionSettings->Settings); It; ++It)
	{
		const FOnlineSessionSetting& Setting = It.Value();
		if (Setting.AdvertisementType >= EOnlineDataAdvertisementType::ViaOnlineService)
		{
			Packet << It.Key();
			Packet << Setting;
#if DEBUG_LAN_BEACON
			UE_LOG_ONLINE(Verbose, TEXT("%s"), *Setting.ToString());
#endif
		}
	}
}

void FOnlineSessionTheia::OnValidQueryPacketReceived(uint8* PacketData, int32 PacketLength, uint64 ClientNonce)
{
	// Iterate through all registered sessions and respond for each one that can be joinable
	FScopeLock ScopeLock(&SessionLock);
	for (int32 SessionIndex = 0; SessionIndex < Sessions.Num(); SessionIndex++)
	{
		FNamedOnlineSession* Session = &Sessions[SessionIndex];

		// Don't respond to query if the session is not a joinable LAN match.
		//if (Session && IsSessionJoinable(*Session))
		if (Session)
		{
			const FOnlineSessionSettings& Settings = Session->SessionSettings;

			const bool bIsMatchInProgress = Session->SessionState == EOnlineSessionState::InProgress;

			const bool bIsMatchJoinable = (!bIsMatchInProgress || Settings.bAllowJoinInProgress) &&
				Settings.NumPublicConnections > 0;

			if (bIsMatchJoinable)
			{
				UE_LOG(LogOnline, Verbose, TEXT("OSID OnValidQueryPacketReceived Match is joinabale"));

				FNboSerializeToBufferTheia Packet(LAN_BEACON_MAX_PACKET_SIZE);

					// Create the basic header before appending additional information
				TheiaSessionManager.CreateHostResponsePacket(Packet, ClientNonce);

				// Add all the session details
				AppendSessionToPacket(Packet, Session);

				// Broadcast this response so the client can see us
				if (!Packet.HasOverflow())
				{
					if (!TheiaSessionManager.IsLANMatch)
					{
						TheiaSessionManager.BroadcastPacketFromSocket(Packet, Packet.GetByteCount());
					}
					else
					{
					TheiaSessionManager.BroadcastPacket(Packet, Packet.GetByteCount());
					}
				}
				else
				{
					UE_LOG_ONLINE(Warning, TEXT("LAN broadcast packet overflow, cannot broadcast on LAN"));
				}
			}
		}
	}
}

void FOnlineSessionTheia::ReadSessionFromPacket(FNboSerializeFromBufferTheia& Packet, FOnlineSession* Session)
{
	UE_LOG(LogOnline, Verbose, TEXT("OnlineSessionInterfaceDirect ReadSessionFromPacket"));

#if DEBUG_LAN_BEACON
	UE_LOG_ONLINE(Verbose, TEXT("Reading session information from server"));
#endif

	/** Owner of the session */
	FUniqueNetIdString* UniqueId = new FUniqueNetIdString;
	Packet >> *UniqueId
		>> Session->OwningUserName
		>> Session->NumOpenPrivateConnections
		>> Session->NumOpenPublicConnections;

	Session->OwningUserId = MakeShareable(UniqueId);

	// Allocate and read the connection data
	FOnlineSessionInfoTheia* NullSessionInfo = new FOnlineSessionInfoTheia();
	NullSessionInfo->HostAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	Packet >> *NullSessionInfo;
	Session->SessionInfo = MakeShareable(NullSessionInfo); 

	// Read any per object data using the server object
	ReadSettingsFromPacket(Packet, Session->SessionSettings);
}

void FOnlineSessionTheia::ReadSettingsFromPacket(FNboSerializeFromBufferTheia& Packet, FOnlineSessionSettings& SessionSettings)
{
#if DEBUG_LAN_BEACON
	UE_LOG_ONLINE(Verbose, TEXT("Reading game settings from server"));
#endif

	// Clear out any old settings
	SessionSettings.Settings.Empty();

	// Members of the session settings class
	Packet >> SessionSettings.NumPublicConnections
		>> SessionSettings.NumPrivateConnections;
	uint8 Read = 0;
	// Read all the bools as bytes
	Packet >> Read;
	SessionSettings.bShouldAdvertise = !!Read;
	Packet >> Read;
	SessionSettings.bIsLANMatch = !!Read;
	Packet >> Read;
	SessionSettings.bIsDedicated = !!Read;
	Packet >> Read;
	SessionSettings.bUsesStats = !!Read;
	Packet >> Read;
	SessionSettings.bAllowJoinInProgress = !!Read;
	Packet >> Read;
	SessionSettings.bAllowInvites = !!Read;
	Packet >> Read;
	SessionSettings.bUsesPresence = !!Read;
	Packet >> Read;
	SessionSettings.bAllowJoinViaPresence = !!Read;
	Packet >> Read;
	SessionSettings.bAllowJoinViaPresenceFriendsOnly = !!Read;
	Packet >> Read;
	SessionSettings.bAntiCheatProtected = !!Read;

	// BuildId
	Packet >> SessionSettings.BuildUniqueId;

	// Now read the contexts and properties from the settings class
	int32 NumAdvertisedProperties = 0;
	// First, read the number of advertised properties involved, so we can presize the array
	Packet >> NumAdvertisedProperties;
	if (Packet.HasOverflow() == false)
	{
		FName Key;
		// Now read each context individually
		for (int32 Index = 0;
			Index < NumAdvertisedProperties && Packet.HasOverflow() == false;
			Index++)
		{
			FOnlineSessionSetting Setting;
			Packet >> Key;
			Packet >> Setting;
			SessionSettings.Set(Key, Setting);

#if DEBUG_LAN_BEACON
			UE_LOG_ONLINE(Verbose, TEXT("%s"), *Setting->ToString());
#endif
		}
	}
	
	// If there was an overflow, treat the string settings/properties as broken
	if (Packet.HasOverflow())
	{
		SessionSettings.Settings.Empty();
		UE_LOG_ONLINE(Verbose, TEXT("Packet overflow detected in ReadGameSettingsFromPacket()"));
	}
}

void FOnlineSessionTheia::OnValidResponsePacketReceived(uint8* PacketData, int32 PacketLength)
{
	// Create an object that we'll copy the data to
	FOnlineSessionSettings NewServer;
	if (CurrentSessionSearch.IsValid())
	{
		// Add space in the search results array
		FOnlineSessionSearchResult* NewResult = new (CurrentSessionSearch->SearchResults) FOnlineSessionSearchResult();
		// this is not a correct ping, but better than nothing
		NewResult->PingInMs = static_cast<int32>((FPlatformTime::Seconds() - SessionSearchStartInSeconds) * 1000);

		FOnlineSession* NewSession = &NewResult->Session;

		// Prepare to read data from the packet
		FNboSerializeFromBufferTheia Packet(PacketData, PacketLength);
		
		ReadSessionFromPacket(Packet, NewSession);

		// NOTE: we don't notify until the timeout happens
	}
	else
	{
		UE_LOG_ONLINE(Warning, TEXT("Failed to create new online game settings object"));
	}
}

uint32 FOnlineSessionTheia::FinalizeTheiaSearch()
{
	if (TheiaSessionManager.GetBeaconState() == ELanBeaconState::Searching)
	{
		TheiaSessionManager.StopTheiaSession();
	}

	return UpdateTheiaStatus();
}

void FOnlineSessionTheia::OnTheiaSearchTimeout()
{
	FinalizeTheiaSearch();

	if (CurrentSessionSearch.IsValid())
	{
		if (CurrentSessionSearch->SearchResults.Num() > 0)
		{
			// Allow game code to sort the servers
			CurrentSessionSearch->SortSearchResults();
		}
		CurrentSessionSearch->SearchState = EOnlineAsyncTaskState::Done;

		CurrentSessionSearch = NULL;
	}

	// Trigger the delegate as complete
	TriggerOnFindSessionsCompleteDelegates(true);
}

int32 FOnlineSessionTheia::GetNumSessions()
{
	FScopeLock ScopeLock(&SessionLock);
	return Sessions.Num();
}

void FOnlineSessionTheia::DumpSessionState()
{
	FScopeLock ScopeLock(&SessionLock);

	for (int32 SessionIdx=0; SessionIdx < Sessions.Num(); SessionIdx++)
	{
		DumpNamedSession(&Sessions[SessionIdx]);
	}
}

void FOnlineSessionTheia::RegisterLocalPlayer(const FUniqueNetId& PlayerId, FName SessionName, const FOnRegisterLocalPlayerCompleteDelegate& Delegate)
{
	Delegate.ExecuteIfBound(PlayerId, EOnJoinSessionCompleteResult::Success);
}

void FOnlineSessionTheia::UnregisterLocalPlayer(const FUniqueNetId& PlayerId, FName SessionName, const FOnUnregisterLocalPlayerCompleteDelegate& Delegate)
{
	Delegate.ExecuteIfBound(PlayerId, true);
}

void FOnlineSessionTheia::SetPortFromNetDriver(const FOnlineSubsystemTheia& Subsystem, const TSharedPtr<FOnlineSessionInfo>& SessionInfo)
{
	auto NetDriverPort = GetPortFromNetDriver(Subsystem.GetInstanceName());
	auto SessionInfoNull = StaticCastSharedPtr<FOnlineSessionInfoTheia>(SessionInfo);
	if (SessionInfoNull.IsValid() && SessionInfoNull->HostAddr.IsValid())
	{
		SessionInfoNull->HostAddr->SetPort(NetDriverPort);
	}
}

//Tells whether current player is host
bool FOnlineSessionTheia::IsHost(const FNamedOnlineSession& Session) const
{
	if (TheiaSubsystem->IsDedicated())
	{
		return true;
	}

	IOnlineIdentityPtr IdentityInt = TheiaSubsystem->GetIdentityInterface(); 
	if (!IdentityInt.IsValid())
	{
		return false;
	}

	TSharedPtr<const FUniqueNetId> UserId = IdentityInt->GetUniquePlayerId(Session.HostingPlayerNum);
	return (UserId.IsValid() && (*UserId == *Session.OwningUserId));
}

//*SetHostPort Fetches Host Port from QuerrySettings and sets it to be connected to
void FOnlineSessionTheia::SetHostPort( const TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	int32 HostPort = 0;
	SearchSettings->QuerySettings.Get("HostSessionPortPort", HostPort);
	UE_LOG(LogOnline, VeryVerbose, TEXT("SetHostPort: Port Passed in from blueprint %u "), HostPort);
	TheiaSessionManager.HostSessionPort = HostPort;
}

//SetHostAddr fetches the Host address passed in from blueprint and converts it from string into Hex to be used
//Not Currently Working
void FOnlineSessionTheia::SetHostAddr(const TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
/*
	FString HostAddr;
	TArray<FString> IpArray = {};

	SearchSettings->QuerySettings.Get("HostSessionAddr", HostAddr);
	UE_LOG(LogOnline, VeryVerbose , TEXT("SetHostAddr: Addr Passed in from blueprint %s "), *HostAddr);
	const TCHAR Delim = '.';
	HostAddr.ParseIntoArray(IpArray, TEXT("."), false);
	uint32 ConvertedHostAddr;
	for (int i = 0; i < IpArray.Num(); i++)
	{
	}

	if (IpArray.Num() == 4)
	{
		FIPv4Address IPv4Address(FCString::Atoi(*IpArray[0]), FCString::Atoi(*IpArray[1]), FCString::Atoi(*IpArray[2]), FCString::Atoi(*IpArray[3]));
		ConvertedHostAddr = IPv4Address.Value;
	}
	else
	{
		UE_LOG(LogOnline, VeryVerbose, TEXT("SetHostAddr: Ipaddress not long enough"));
	}
	UE_LOG(LogOnline, VeryVerbose, TEXT("SetHostAddr: Host Address is now %u"), ConvertedHostAddr);
	TheiaSessionManager.HostSessionAddr = ConvertedHostAddr;
	*/
}