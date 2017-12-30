// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "TheiaBeacon.h"
#include "Misc/FeedbackContext.h"
#include "UObject/CoreNet.h"
#include "OnlineSubsystem.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "NboSerializer.h"

/** Sets the broadcast address for this object */
FTheiaBeacon::FTheiaBeacon(void) 
	: ListenSocket(NULL),
	  SockAddr(ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr())
{
}

/** Frees the broadcast socket */
FTheiaBeacon::~FTheiaBeacon(void)
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	SocketSubsystem->DestroySocket(ListenSocket);
}

/** Return true if there is a valid ListenSocket */
bool FTheiaBeacon::IsListenSocketValid() const
{
	return (ListenSocket ? true : false);
}

/**
 * Initializes the socket
 *
 * @param Port the port to listen on
 *
 * @return true if both socket was created successfully, false otherwise
 */
bool FTheiaBeacon::Init(int32 Port)
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	bool bSuccess = false;
	// Set our broadcast address
	BroadcastAddr = SocketSubsystem->CreateInternetAddr();
	BroadcastAddr->SetBroadcastAddress();
	BroadcastAddr->SetPort(Port);
	// Now the listen address
	ListenAddr = SocketSubsystem->GetLocalBindAddr(*GWarn);
	ListenAddr->SetPort(Port);
	// A temporary "received from" address
	SockAddr = SocketSubsystem->CreateInternetAddr();
	// Now create and set up our sockets (no VDP)
	ListenSocket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("LAN beacon"), true);
	if (ListenSocket != NULL)
	{
		ListenSocket->SetReuseAddr();
		ListenSocket->SetNonBlocking();
		ListenSocket->SetRecvErr();
		// Bind to our listen port
		if (ListenSocket->Bind(*ListenAddr))
		{
			// Set it to broadcast mode, so we can send on it
			// NOTE: You must set this to broadcast mode on Xbox 360 or the
			// secure layer will eat any packets sent
			bSuccess = ListenSocket->SetBroadcast();
		}
		else
		{
			UE_LOG(LogOnline, Error, TEXT("Failed to bind listen socket to addr (%s) for LAN beacon"),
				*ListenAddr->ToString(true));
		}
	}
	else
	{
		UE_LOG(LogOnline, Error, TEXT("Failed to create listen socket for LAN beacon"));
	}
	return bSuccess && ListenSocket;
}


bool FTheiaBeacon::InitHost(int32 Port)
{
	UE_LOG(LogOnline, VeryVerbose, TEXT("LanBeacon InitHost"))

		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	bool bSuccess = false;

	// Now the listen address
	ListenAddr = SocketSubsystem->GetLocalBindAddr(*GLog);
	ListenAddr->SetPort((int)Port);

	UE_LOG(LogOnline, VeryVerbose, TEXT("LanBeacon InitHost Listen Address is %s "), *ListenAddr->ToString(true));

	// A temporary "received from" address
	SockAddr = SocketSubsystem->CreateInternetAddr();
	UE_LOG(LogOnline, VeryVerbose, TEXT("LanBeacon InitHost SockAddr is %s"), *SockAddr->ToString(true));

	// Now create and set up our sockets (no VDP)
	ListenSocket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("LAN beacon"), true);
	if (ListenSocket != NULL)
	{
		if (ListenSocket->SetReuseAddr())
		{
			
			UE_LOG(LogOnline, VeryVerbose, TEXT("LanBeacon InitHost ReuseAddr true"));
		}

		if (ListenSocket->SetNonBlocking())
		{
			UE_LOG(LogOnline, VeryVerbose, TEXT("LanBeacon InitHost NonBlocking true"));
		}
		if (ListenSocket->SetRecvErr())
		{
			UE_LOG(LogOnline, VeryVerbose, TEXT("LanBeacon InitHost SetRecvError true"));
		}
		// Bind to our listen port
		if (ListenSocket->Bind(*ListenAddr))
		{
			UE_LOG(LogOnline, VeryVerbose, TEXT("LanBeacon InitHost Listen Address bound is %s "), *ListenAddr->ToString(true));
			// Set it to broadcast mode, so we can send on it
			// NOTE: You must set this to broadcast mode on Xbox 360 or the
			// secure layer will eat any packets sent
			bSuccess = true;// ListenSocket->SetBroadcast();
		}
		else
		{
			////Try to find another port to bind to
			if (SocketSubsystem->BindNextPort(ListenSocket, *ListenAddr, 8000, 1) != 0)
			{
				UE_LOG(LogOnline, VeryVerbose, TEXT("New ListenAddr after Port increment is %s"),
					*ListenAddr->ToString(true));
				bSuccess = true; // ListenSocket->SetBroadcast();

				TriggerOnPortChangedDelegates(ListenAddr->GetPort());



			}
			else
			{
				UE_LOG(LogOnline, Error, TEXT("Failed to bind listen socket to addr (%s) for LAN beacon"),
					*ListenAddr->ToString(true));
			}
		}
	}
	else
	{
		UE_LOG(LogOnline, Error, TEXT("Failed to create listen socket for LAN beacon"));
	}

	return bSuccess && ListenSocket;
}


bool FTheiaBeacon::InitClient(int32 IP, int32 Port, int32 ListenPort)
{
	UE_LOG(LogOnline, VeryVerbose, TEXT("LanBeacon InitClient IP from blueprint is %u"), IP);

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	bool bSuccess = false;

	// Set IP of Host to Connect To
	BroadcastAddr = SocketSubsystem->CreateInternetAddr(IP, (int)Port);
	BroadcastAddr->SetIp(IP);

	BroadcastAddr->SetPort((int)Port);

	UE_LOG(LogOnline, VeryVerbose, TEXT("LanBeacon InitClient Broadcast Address is %s "), *BroadcastAddr->ToString(true));

	// Now the listen address peep change object *GWarn to Glog
	ListenAddr = SocketSubsystem->GetLocalBindAddr(*GLog);

	//DefaultObjectWas Port working 63
	ListenAddr->SetPort((int)ListenPort);

	UE_LOG(LogOnline, VeryVerbose, TEXT("LanBeacon InitClient Listen Address is %s "), *ListenAddr->ToString(true));

	// A temporary "received from" address
	SockAddr = SocketSubsystem->CreateInternetAddr();

	UE_LOG(LogOnline, VeryVerbose, TEXT("LanBeacon InitClient SockAddr is %s"), *SockAddr->ToString(true));

	// Now create and set up our sockets (no VDP)
	ListenSocket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("LAN beacon"), true);
	if (ListenSocket != NULL)
	{
		if (ListenSocket->SetReuseAddr())
		{
			UE_LOG(LogOnline, VeryVerbose, TEXT("LanBeacon InitClient ReuseAddr true"));
		}

		if (ListenSocket->SetNonBlocking())
		{
			UE_LOG(LogOnline, VeryVerbose, TEXT("LanBeacon InitClient NonBlocking true"));
		}
		if (ListenSocket->SetRecvErr())
		{
			UE_LOG(LogOnline, VeryVerbose, TEXT("LanBeacon InitClient SetRecvError true"));
		}
		//bSuccess = true;
		// Bind to our listen port
		if (ListenSocket->Bind(*ListenAddr))
		{

			UE_LOG(LogOnline, VeryVerbose, TEXT("LanBeacon InitClient Listen Address after Socket bound is %s "), *ListenAddr->ToString(true));

			// Set it to broadcast mode, so we can send on it
			// NOTE: You must set this to broadcast mode on Xbox 360 or the
			// secure layer will eat any packets sent

			bSuccess = ListenSocket->SetBroadcast();
			//bSuccess = true;
		}
		else
		{
			////Try to find another port to bind to
			if (SocketSubsystem->BindNextPort(ListenSocket, *ListenAddr, 8000, 1) != 0)
			{
				UE_LOG(LogOnline, VeryVerbose, TEXT("New ListenAddr after Port increment is %s"),
					*ListenAddr->ToString(true));
				bSuccess = true; // ListenSocket->SetBroadcast();

								 //ToDo: Make Output in Blueprint node so user knows which ports to open (also in else)
				TriggerOnPortChangedDelegates(ListenAddr->GetPort());



			}
			else
			{
				UE_LOG(LogOnline
, Error, TEXT("Failed to bind listen socket to addr (%s) for LAN beacon"),
					*ListenAddr->ToString(true));
			}

		}
	}
	else
	{

	}
	return bSuccess && ListenSocket;
}
/**
 * Called to poll the socket for pending data. Any data received is placed
 * in the specified packet buffer
 *
 * @param PacketData the buffer to get the socket's packet data
 * @param BufferSize the size of the packet buffer
 *
 * @return the number of bytes read (<= 0 if none or an error)
 */
int32 FTheiaBeacon::ReceivePacket(uint8* PacketData, int32 BufferSize)
{
	check(PacketData && BufferSize);
	// Default to no data being read
	int32 BytesRead = 0;
	if (ListenSocket != NULL)
	{
		// Read from the socket
		ListenSocket->RecvFrom(PacketData, BufferSize, BytesRead, *SockAddr);
		if (BytesRead > 0)
		{
			UE_LOG(LogOnline, Verbose, TEXT("Received %d bytes from %s"), BytesRead, *SockAddr->ToString(true));
		}
	}

	return BytesRead;
}

/**
 * Uses the cached broadcast address to send packet to a subnet
 *
 * @param Packet the packet to send
 * @param Length the size of the packet to send
 */
bool FTheiaBeacon::BroadcastPacket(uint8* Packet, int32 Length)
{
	int32 BytesSent = 0;
	BroadcastAddr->SetPort(BroadcastAddr->GetPort() + 1);
	UE_LOG(LogOnline, Verbose, TEXT("BroadcastPacket: Sending %d bytes to %s"), Length, *BroadcastAddr->ToString(true));
	return ListenSocket->SendTo(Packet, Length, BytesSent, *BroadcastAddr) && (BytesSent == Length);
}

bool FTheiaBeacon::BroadcastPacketFromSocket(uint8* Packet, int32 Length)
{
	UE_LOG(LogOnline, VeryVerbose, TEXT("BroadcastPacketFromSocket: LanBeacon BroadcastPacket to Beacon"));

	int32 BytesSent = 16;
	UE_LOG(LogOnline, Verbose, TEXT("BroadcastPacketFromSocket: Sending %d bytes to %s"), Length, *SockAddr->ToString(true));

	UE_LOG(LogOnline, VeryVerbose, TEXT("BroadcastPacketFromSocket: LanBeacon Broadcast to Listen Address before sending is %s "), *ListenAddr->ToString(true));

	return ListenSocket->SendTo(Packet, Length, BytesSent, *SockAddr) && (BytesSent == Length);

}

bool FTheiaSession::Host(FOnValidQueryPacketDelegate& QueryDelegate, int32 Port)
{
	UE_LOG(LogOnline, VeryVerbose, TEXT("LanBeacon Host Incoming Port is %u "), Port);
	bool bSuccess = false;
	if (TheiaBeacon != NULL)
	{
		StopTheiaSession();
	}

	// Bind a socket for LAN beacon activity
	TheiaBeacon = new FTheiaBeacon();
	//if its LAN Connection
	if (Port == -1)
	{
		if (TheiaBeacon->Init(TheiaAnnouncePort))
		{

			AddOnValidQueryPacketDelegate_Handle(QueryDelegate);
			// We successfully created everything so mark the socket as needing polling
			TheiaBeaconState = ELanBeaconState::Hosting;
			bSuccess = true;
			UE_LOG(LogOnline, Verbose, TEXT("Listening for LAN beacon requests on %d"), TheiaAnnouncePort);
		}
		else
		{
			UE_LOG(LogOnline, VeryVerbose, TEXT("Failed to init LAN beacon %s"), ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetSocketError());
		}
	}
	else
	{
		if (TheiaBeacon->InitHost(Port))
		{

			AddOnValidQueryPacketDelegate_Handle(QueryDelegate);
			// We successfully created everything so mark the socket as needing polling
			TheiaBeaconState = ELanBeaconState::Hosting;
			bSuccess = true;
			UE_LOG(LogOnline, Verbose, TEXT("Listening for Online beacon requests on %u"), Port);
		}
		else
		{
			UE_LOG(LogOnline, VeryVerbose, TEXT("Failed to init Online beacon %s"), ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetSocketError());
		}
	}

	return bSuccess;
}
/**
* Creates the LAN beacon for queries/advertising servers
*/
bool FTheiaSession::Host(FOnValidQueryPacketDelegate& QueryDelegate)
{
	UE_LOG(LogOnline, VeryVerbose, TEXT("Host: Creating Host Lan Beacon"));
	bool bSuccess = false;
	if (TheiaBeacon != NULL)
	{
		StopTheiaSession();
	}

	// Bind a socket for LAN beacon activity
	TheiaBeacon = new FTheiaBeacon();
	if (TheiaBeacon->Init(TheiaAnnouncePort))
	{
		AddOnValidQueryPacketDelegate_Handle(QueryDelegate);
		// We successfully created everything so mark the socket as needing polling
		TheiaBeaconState = ELanBeaconState::Hosting;
		bSuccess = true;
		UE_LOG(LogOnline, Verbose, TEXT("Listening for LAN beacon requests on %d"),	TheiaAnnouncePort);
	}
	else
	{
		UE_LOG(LogOnline, Warning, TEXT("Failed to init to LAN beacon %s"),	ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetSocketError());
	}
	
	return bSuccess;
}

/**
 * Creates the LAN beacon for queries/advertising servers
 *
 * @param Nonce unique identifier for this search
 * @param ResponseDelegate delegate to fire when a server response is received
 */
bool FTheiaSession::Search(FNboSerializeToBuffer& Packet, FOnValidResponsePacketDelegate& ResponseDelegate, FOnSearchingTimeoutDelegate& TimeoutDelegate)
{

	UE_LOG(LogOnline, VeryVerbose, TEXT("Search: searching for lan session"));
	bool bSuccess = true;
	if (TheiaBeacon != NULL)
	{
		StopTheiaSession();
	}

	// Bind a socket for LAN beacon activity
	TheiaBeacon = new FTheiaBeacon();
	if (IsLANMatch)
	{
		if (TheiaBeacon->Init(TheiaAnnouncePort) == false)
		{
			UE_LOG(LogOnline, Warning, TEXT("Failed to create socket for lan announce port %s"), ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetSocketError());
			bSuccess = false;
		}
	}
	else
	{
		UE_LOG(LogOnline, VeryVerbose, TEXT("LanBeacon Search Init OnlineBeacon"));
		// Bind a socket for Online beacon activity
		if (TheiaBeacon->InitClient(HostSessionAddr, HostSessionPort, ClientSessionPort) == false)
		{
			UE_LOG(LogOnline, VeryVerbose, TEXT("Failed to create socket for lan announce port %s"), ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetSocketError());
			bSuccess = false;
		}
	}

	// If we have a socket and a nonce, broadcast a discovery packet
	if (TheiaBeacon && bSuccess)
	{
		// Now kick off our broadcast which hosts will respond to
		if (TheiaBeacon->BroadcastPacket(Packet, Packet.GetByteCount()))
		{
			UE_LOG(LogOnline, Verbose, TEXT("Sent query packet..."));
			// We need to poll for the return packets
			TheiaBeaconState = ELanBeaconState::Searching;
			// Set the timestamp for timing out a search
			TheiaQueryTimeLeft = TheiaQueryTimeout;

			AddOnValidResponsePacketDelegate_Handle(ResponseDelegate);
			AddOnSearchingTimeoutDelegate_Handle(TimeoutDelegate);
		}
		else
		{
			UE_LOG(LogOnline, Warning, TEXT("Failed to send discovery broadcast %s"), ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetSocketError());
			bSuccess = false;
		}
	}

	return bSuccess;
}

/** Stops the LAN beacon from accepting broadcasts */
void FTheiaSession::StopTheiaSession()
{
	UE_LOG(LogOnline, VeryVerbose, TEXT("StopLANSession: stopping lan session"));
	// Don't poll anymore since we are shutting it down
	TheiaBeaconState = ELanBeaconState::NotUsingLanBeacon;

	// Unbind the LAN beacon object
	if (TheiaBeacon)
	{	
		delete TheiaBeacon;
		TheiaBeacon = NULL;
	}

	// Clear delegates
	OnValidQueryPacketDelegates.Clear();
	OnValidResponsePacketDelegates.Clear();
	OnSearchingTimeoutDelegates.Clear();
}

void FTheiaSession::Tick(float DeltaTime)
{
	if (TheiaBeaconState == ELanBeaconState::NotUsingLanBeacon)
	{
		return;
	}

	uint8 PacketData[LAN_BEACON_MAX_PACKET_SIZE];
	bool bShouldRead = true;
	// Read each pending packet and pass it out for processing
	while (bShouldRead)
	{
		int32 NumRead = TheiaBeacon->ReceivePacket(PacketData, LAN_BEACON_MAX_PACKET_SIZE);
		if (NumRead > 0)
		{
			// Check our mode to determine the type of allowed packets
			if (TheiaBeaconState == ELanBeaconState::Hosting)
			{
				uint64 ClientNonce;
				// We can only accept Server Query packets
				if (IsValidTheiaQueryPacket(PacketData, NumRead, ClientNonce))
				{
					// Strip off the header
					TriggerOnValidQueryPacketDelegates(&PacketData[LAN_BEACON_PACKET_HEADER_SIZE], NumRead - LAN_BEACON_PACKET_HEADER_SIZE, ClientNonce);
				}
			}
			else if (TheiaBeaconState == ELanBeaconState::Searching)
			{
				// We can only accept Server Response packets
				if (IsValidTheiaResponsePacket(PacketData, NumRead))
				{
					// Strip off the header
					TriggerOnValidResponsePacketDelegates(&PacketData[LAN_BEACON_PACKET_HEADER_SIZE], NumRead - LAN_BEACON_PACKET_HEADER_SIZE);
				}
			}
		}
		else
		{
			if (TheiaBeaconState == ELanBeaconState::Searching)
			{
				// Decrement the amount of time remaining
				TheiaQueryTimeLeft -= DeltaTime;
				// Check for a timeout on the search packet
				if (TheiaQueryTimeLeft <= 0.f)
				{
					TriggerOnSearchingTimeoutDelegates();
				}
			}
			bShouldRead = false;
		}
	}
}

void FTheiaSession::CreateHostResponsePacket(FNboSerializeToBuffer& Packet, uint64 ClientNonce)
{
	UE_LOG(LogOnline, VeryVerbose, TEXT("CreateHostResponsePacket: creating response packet"));
	// Add the supported version
	Packet << LAN_BEACON_PACKET_VERSION
		// Platform information
		<< (uint8)FPlatformProperties::IsLittleEndian()
		// Game id to prevent cross game lan packets
		<< TheiaGameUniqueId
		// Add the packet type
		<< LAN_SERVER_RESPONSE1 << LAN_SERVER_RESPONSE2
		// Append the client nonce as a uint64
		<< ClientNonce;
}

void FTheiaSession::CreateClientQueryPacket(FNboSerializeToBuffer& Packet, uint64 ClientNonce)
{
	UE_LOG(LogOnline, VeryVerbose, TEXT("CreateClientQueryPacket: creating query packet"));
	// Build the discovery packet
	Packet << LAN_BEACON_PACKET_VERSION
		// Platform information
		<< (uint8)FPlatformProperties::IsLittleEndian()
		// Game id to prevent cross game lan packets
		<< TheiaGameUniqueId
		// Identify the packet type
		<< LAN_SERVER_QUERY1 << LAN_SERVER_QUERY2
		// Append the nonce as a uint64
		<< ClientNonce;
}

/**
 * Uses the cached broadcast address to send packet to a subnet
 *
 * @param Packet the packet to send
 * @param Length the size of the packet to send
 */
bool FTheiaSession::BroadcastPacket(uint8* Packet, int32 Length)
{
	UE_LOG(LogOnline, VeryVerbose, TEXT("BroadcastPacket: Broadcasting Packet"));
	bool bSuccess = false;
	if (TheiaBeacon)
	{
		bSuccess = TheiaBeacon->BroadcastPacket(Packet, Length);
		if (!bSuccess)
		{
			UE_LOG(LogOnline, Warning, TEXT("Failed to send broadcast packet %d"), (int32)ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode());
		}
	}

	return bSuccess;
}

bool FTheiaSession::BroadcastPacketFromSocket(uint8* Packet, int32 Length)
{
	UE_LOG(LogOnline, VeryVerbose, TEXT("BroadcastPacketFromSocket: LanBeacon BroadcastPacket to Session"));

	bool bSuccess = false;

	if (TheiaBeacon != NULL)
	{
		bSuccess = TheiaBeacon->BroadcastPacketFromSocket(Packet, Length);
		if (!bSuccess)
		{
			UE_LOG(LogOnline, VeryVerbose, TEXT("Failed to send broadcast packet %d"), (int32)ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode());
		}
	}

	return bSuccess;
}

/**
 * Determines if the packet header is valid or not
 *
 * @param Packet the packet data to check
 * @param Length the size of the packet buffer
 * @param ClientNonce the client nonce contained within the packet
 *
 * @return true if the header is valid, false otherwise
 */
bool FTheiaSession::IsValidTheiaQueryPacket(const uint8* Packet, uint32 Length, uint64& ClientNonce)
{
	UE_LOG(LogOnline, VeryVerbose, TEXT("IsValidLanQueryPacket: Checking is packet is valid"));
	ClientNonce = 0;
	bool bIsValid = false;
	// Serialize out the data if the packet is the right size
	if (Length == LAN_BEACON_PACKET_HEADER_SIZE)
	{
		FNboSerializeFromBuffer PacketReader(Packet,Length);
		uint8 Version = 0;
		PacketReader >> Version;
		// Do the versions match?
		if (Version == LAN_BEACON_PACKET_VERSION)
		{
			uint8 Platform = 255;
			PacketReader >> Platform;
			// Can we communicate with this platform?
			if (Platform & TheiaPacketPlatformMask)
			{
				int32 GameId = -1;
				PacketReader >> GameId;
				// Is this our game?
				if (GameId == TheiaGameUniqueId)
				{
					uint8 SQ1 = 0;
					PacketReader >> SQ1;
					uint8 SQ2 = 0;
					PacketReader >> SQ2;
					// Is this a server query?
					bIsValid = (SQ1 == LAN_SERVER_QUERY1 && SQ2 == LAN_SERVER_QUERY2);
					// Read the client nonce as the outvalue
					PacketReader >> ClientNonce;
				}
			}
		}
	}
	UE_LOG(LogOnline, VeryVerbose, TEXT("IsValidLanQueryPacket: packet was %s"), (bIsValid ? TEXT("Valid") : TEXT("Not Valid")));
	return bIsValid;
}

/**
 * Determines if the packet header is valid or not
 *
 * @param Packet the packet data to check
 * @param Length the size of the packet buffer
 *
 * @return true if the header is valid, false otherwise
 */
bool FTheiaSession::IsValidTheiaResponsePacket(const uint8* Packet, uint32 Length)
{
	UE_LOG(LogOnline, VeryVerbose, TEXT("IsValidLanResponsePacket: Checing is packet is valid"));
	bool bIsValid = false;
	// Serialize out the data if the packet is the right size
	if (Length > LAN_BEACON_PACKET_HEADER_SIZE)
	{
		FNboSerializeFromBuffer PacketReader(Packet,Length);
		uint8 Version = 0;
		PacketReader >> Version;
		// Do the versions match?
		if (Version == LAN_BEACON_PACKET_VERSION)
		{
			uint8 Platform = 255;
			PacketReader >> Platform;
			// Can we communicate with this platform?
			if (Platform & TheiaPacketPlatformMask)
			{
				int32 GameId = -1;
				PacketReader >> GameId;
				// Is this our game?
				if (GameId == TheiaGameUniqueId)
				{
					uint8 SQ1 = 0;
					PacketReader >> SQ1;
					uint8 SQ2 = 0;
					PacketReader >> SQ2;
					// Is this a server response?
					if (SQ1 == LAN_SERVER_RESPONSE1 && SQ2 == LAN_SERVER_RESPONSE2)
					{
						uint64 Nonce = 0;
						PacketReader >> Nonce;
						bIsValid = (Nonce == TheiaNonce);
					}
				}
			}
		}
	}
	UE_LOG(LogOnline, VeryVerbose, TEXT("IsValidLanResponsePacket: packet was %s"), (bIsValid ? TEXT("Valid") : TEXT("Not Valid")));
	return bIsValid;
}
