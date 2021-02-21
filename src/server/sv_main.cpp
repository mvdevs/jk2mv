
#include "server.h"

serverStatic_t	svs;				// persistant server info
server_t		sv;					// local server
vm_t			*gvm = NULL;				// game virtual machine // bk001212 init

cvar_t	*sv_fps;				// time rate for running non-clients
cvar_t	*sv_timeout;			// seconds without any message
cvar_t	*sv_zombietime;			// seconds to sink messages after disconnect
cvar_t	*sv_rconPassword;		// password for remote server commands
cvar_t	*sv_privatePassword;	// password for the privateClient slots
cvar_t	*sv_allowDownload;
cvar_t	*mv_httpdownloads;
cvar_t	*mv_httpserverport;
cvar_t	*sv_maxclients;
cvar_t	*sv_privateClients;		// number of clients reserved for password
cvar_t	*sv_hostname;
cvar_t	*sv_master[MAX_MASTER_SERVERS];		// master server ip address
cvar_t	*sv_reconnectlimit;		// minimum seconds between connect messages
cvar_t	*sv_showloss;			// report when usercmds are lost
cvar_t	*sv_padPackets;			// add nop bytes to messages
cvar_t	*sv_killserver;			// menu system can set to 1 to shut server down
cvar_t	*sv_mapname;
cvar_t	*sv_mapChecksum;
cvar_t	*sv_serverid;
cvar_t	*sv_minSnaps;			// minimum snapshots/sec a client can request, also limited by sv_maxSnaps
cvar_t	*sv_maxSnaps;			// maximum snapshots/sec a client can request, also limited by sv_fps
cvar_t	*sv_enforceSnaps;
cvar_t	*sv_minRate;
cvar_t	*sv_maxRate;
cvar_t	*sv_maxOOBRate;
cvar_t	*sv_minPing;
cvar_t	*sv_maxPing;
cvar_t	*sv_gametype;
cvar_t	*sv_pure;
cvar_t	*sv_floodProtect;
cvar_t	*sv_allowAnonymous;
cvar_t	*sv_needpass;
cvar_t	*mv_serverversion;
cvar_t  *sv_hibernateFps;
cvar_t	*mv_apiConnectionless;
cvar_t	*sv_pingFix;
cvar_t	*sv_autoWhitelist;
cvar_t	*sv_dynamicSnapshots;

// jk2mv's toggleable fixes
cvar_t	*mv_fixnamecrash;
cvar_t	*mv_fixforcecrash;
cvar_t	*mv_fixgalaking;
cvar_t	*mv_fixbrokenmodels;
cvar_t	*mv_fixturretcrash;
cvar_t	*mv_blockchargejump;
cvar_t	*mv_blockspeedhack;
cvar_t	*mv_fixsaberstealing;
cvar_t	*mv_fixplayerghosting;

// jk2mv engine flags
cvar_t	*mv_resetServerTime;

/*
=============================================================================

EVENT MESSAGES

=============================================================================
*/

/*
===============
SV_ExpandNewlines

Converts newlines to "\n" so a line prints nicer
===============
*/
char	*SV_ExpandNewlines( char *in ) {
	static	char	string[1024];
	unsigned		l;

	l = 0;
	while ( *in && l < sizeof(string) - 3 ) {
		if ( *in == '\n' ) {
			string[l++] = '\\';
			string[l++] = 'n';
		} else {
			string[l++] = *in;
		}
		in++;
	}
	string[l] = 0;

	return string;
}

/*
======================
SV_ReplacePendingServerCommands

  This is ugly
======================
*/
int SV_ReplacePendingServerCommands( client_t *client, const char *cmd ) {
	int i, index, csnum1, csnum2;

	for ( i = client->reliableSent+1; i <= client->reliableSequence; i++ ) {
		index = i & ( MAX_RELIABLE_COMMANDS - 1 );
		//
		if (!Q_strncmp(cmd, client->reliableCommands[index], (int)strlen("cs"))) {
			if ( sscanf(cmd, "cs %i", &csnum1) != 1 )
				return qfalse;
			if ( sscanf(client->reliableCommands[ index ], "cs %i", &csnum2) != 1 )
				return qfalse;
			if ( csnum1 == csnum2 ) {
				Q_strncpyz( client->reliableCommands[ index ], cmd, sizeof( client->reliableCommands[ index ] ) );
				/*
				if ( client->netchan.remoteAddress.type != NA_BOT ) {
					Com_Printf( "WARNING: client %i removed double pending config string %i: %s\n", client-svs.clients, csnum1, cmd );
				}
				*/
				return qtrue;
			}
		}
	}
	return qfalse;
}

/*
======================
SV_AddServerCommand

The given command will be transmitted to the client, and is guaranteed to
not have future snapshot_t executed before it is executed
======================
*/
void SV_AddServerCommand( client_t *client, const char *cmd ) {
	int		index, i;

	// this is very ugly but it's also a waste to for instance send multiple config string updates
	// for the same config string index in one snapshot
//	if ( SV_ReplacePendingServerCommands( client, cmd ) ) {
//		return;
//	}

	client->reliableSequence++;
	// if we would be losing an old command that hasn't been acknowledged,
	// we must drop the connection
	// we check == instead of >= so a broadcast print added by SV_DropClient()
	// doesn't cause a recursive drop client
	if ( client->reliableSequence - client->reliableAcknowledge == MAX_RELIABLE_COMMANDS + 1 ) {
		Com_Printf( "===== pending server commands =====\n" );
		for ( i = client->reliableAcknowledge + 1 ; i <= client->reliableSequence ; i++ ) {
			Com_Printf( "cmd %5d: %s\n", i, client->reliableCommands[ i & (MAX_RELIABLE_COMMANDS-1) ] );
		}
		Com_Printf( "cmd %5d: %s\n", i, cmd );
		SV_DropClient( client, "Server command overflow" );
		return;
	}
	index = client->reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 );
	Q_strncpyz( client->reliableCommands[ index ], cmd, sizeof( client->reliableCommands[ index ] ) );
}


/*
=================
SV_SendServerCommand

Sends a reliable command string to be interpreted by
the client game module: "cp", "print", "chat", etc
A NULL client will broadcast to all clients
=================
*/
void QDECL SV_SendServerCommand(client_t *cl, const char *fmt, ...) {
	va_list		argptr;
	byte		message[MAX_MSGLEN];
	client_t	*client;
	int			j;

	va_start (argptr,fmt);
	Q_vsnprintf ((char *)message, sizeof(message), fmt,argptr);
	va_end (argptr);

	// q3msgboom exploit
	if (strlen((char *)message) > 1022) {
		return;
	}

	if ( cl != NULL ) {
		SV_AddServerCommand( cl, (char *)message );
		return;
	}

	// hack to echo broadcast prints to console
	if ( com_dedicated->integer && !strncmp( (char *)message, "print", 5) ) {
		Com_Printf ("broadcast: %s\n", SV_ExpandNewlines((char *)message) );
	}

	// send the data to all relevent clients
	for (j = 0, client = svs.clients; j < sv_maxclients->integer ; j++, client++) {
		if ( client->state < CS_PRIMED ) {
			continue;
		}
		SV_AddServerCommand( client, (char *)message );
	}
}


/*
==============================================================================

MASTER SERVER FUNCTIONS

==============================================================================
*/

/*
================
SV_MasterHeartbeat

Send a message to the masters every few minutes to
let it know we are alive, and log information.
We will also have a heartbeat sent when a server
changes from empty to non-empty, and full to non-full,
but not on every player enter or exit.
================
*/
#define	HEARTBEAT_MSEC	300*1000
#define	HEARTBEAT_GAME	"QuakeArena-1"
void SV_MasterHeartbeat( void ) {
	static netadr_t	adr[MAX_MASTER_SERVERS];
	int			i;

	// "dedicated 1" is for lan play, "dedicated 2" is for inet public play
	if ( !com_dedicated || com_dedicated->integer != 2 ) {
		return;		// only dedicated servers send heartbeats
	}

	// if not time yet, don't send anything
	if ( svs.time < svs.nextHeartbeatTime ) {
		return;
	}
	svs.nextHeartbeatTime = svs.time + HEARTBEAT_MSEC;


	// send to group masters
	for ( i = 0 ; i < MAX_MASTER_SERVERS ; i++ ) {
		if ( !sv_master[i]->string[0] ) {
			continue;
		}

		// see if we haven't already resolved the name
		// resolving usually causes hitches on win95, so only
		// do it when needed
		if ( sv_master[i]->modified ) {
			sv_master[i]->modified = qfalse;

			Com_Printf( "Resolving %s\n", sv_master[i]->string );
			if ( !NET_StringToAdr( sv_master[i]->string, &adr[i] ) ) {
				// if the address failed to resolve, clear it
				// so we don't take repeated dns hits
				Com_Printf( "Couldn't resolve address: %s\n", sv_master[i]->string );
				Cvar_Set( sv_master[i]->name, "" );
				sv_master[i]->modified = qfalse;
				continue;
			}
			if ( !strstr( ":", sv_master[i]->string ) ) {
				adr[i].port = BigShort( PORT_MASTER );
			}
			Com_Printf( "%s resolved to %i.%i.%i.%i:%i\n", sv_master[i]->string,
				adr[i].ip[0], adr[i].ip[1], adr[i].ip[2], adr[i].ip[3],
				BigShort( adr[i].port ) );

			SVC_WhitelistAdr( adr[i] );
		}

		Com_Printf ("Sending heartbeat to %s\n", sv_master[i]->string );
		// this command should be changed if the server info / status format
		// ever incompatably changes
		NET_OutOfBandPrint( NS_SERVER, adr[i], "heartbeat %s\n", HEARTBEAT_GAME );
	}
}

/*
=================
SV_MasterShutdown

Informs all masters that this server is going down
=================
*/
void SV_MasterShutdown( void ) {
	// send a hearbeat right now
	svs.nextHeartbeatTime = -9999;
	SV_MasterHeartbeat();

	// send it again to minimize chance of drops
	svs.nextHeartbeatTime = -9999;
	SV_MasterHeartbeat();

	// when the master tries to poll the server, it won't respond, so
	// it will be removed from the list
}


/*
==============================================================================

CONNECTIONLESS COMMANDS

==============================================================================
*/

/*
================
SVC_BucketForAddress

Find or allocate a bucket for an address
================
*/
#include <map>

static leakyBucket_t *SVC_BucketForAddress(netadr_t address, int burst, int period, int now) {
	static std::map<int, leakyBucket_t> bucketMap;
	static unsigned int	callCounter = 0;

	if (address.type != NA_IP) {
		return NULL;
	}

	callCounter++;

	if ((callCounter & 0xffffu) == 0) {
		auto it = bucketMap.begin();

		while (it != bucketMap.end()) {
			int interval = now - it->second.lastTime;

			if (interval > it->second.burst * period || interval < 0) {
				it = bucketMap.erase(it);
			} else {
				++it;
			}
		}
	}

	return &bucketMap[address.ipi];
}

/*
================
SVC_RateLimit

Allows one packet per period msec with bucket size of burst
================
*/
qboolean SVC_RateLimit(leakyBucket_t *bucket, int burst, int period, int now) {
	if (bucket != NULL) {
		int interval = now - bucket->lastTime;
		int expired = interval / period;
		int expiredRemainder = interval % period;

		if (expired > bucket->burst || interval < 0) {
			bucket->burst = 0;
			bucket->lastTime = now;
		} else {
			bucket->burst -= expired;
			bucket->lastTime = now - expiredRemainder;
		}

		if (bucket->burst < burst) {
			bucket->burst++;

			return qfalse;
		}

		return qtrue;
	} else {
		return qfalse;
	}
}

/*
================
SVC_RateLimitAddress

Rate limit for a particular address
================
*/
static qboolean SVC_RateLimitAddress(netadr_t from, int burst, int period, int now) {
	leakyBucket_t *bucket = SVC_BucketForAddress(from, burst, period, now);

	return SVC_RateLimit(bucket, burst, period, now);
}

/*
================
SVC_Status

Responds with all the info that qplug or qspy can see about the server
and all connected players.  Used for getting detailed information after
the simple info query.
================
*/
void SVC_Status( netadr_t from ) {
	char	player[1024];
	char	status[MAX_MSGLEN];
	int		i;
	client_t	*cl;
	playerState_t	*ps;
	size_t	statusLength;
	size_t	playerLength;
	char	infostring[MAX_INFO_STRING];

	strcpy( infostring, Cvar_InfoString( CVAR_SERVERINFO ) );

	// echo back the parameter to status. so master servers can use it as a challenge
	// to prevent timed spoofed reply packets that add ghost servers
	Info_SetValueForKey( infostring, "challenge", Cmd_Argv(1) );

	// add "demo" to the sv_keywords if restricted
	if ( Cvar_VariableValue( "fs_restrict" ) ) {
		char	keywords[MAX_INFO_STRING];

		Com_sprintf( keywords, sizeof( keywords ), "demo %s",
			Info_ValueForKey( infostring, "sv_keywords" ) );
		Info_SetValueForKey( infostring, "sv_keywords", keywords );
	}

	status[0] = 0;
	statusLength = 0;

	for (i=0 ; i < sv_maxclients->integer ; i++) {
		cl = &svs.clients[i];
		if ( cl->state >= CS_CONNECTED ) {
			ps = SV_GameClientNum( i );
			Com_sprintf (player, sizeof(player), "%i %i \"%s\"\n",
				ps->persistant[PERS_SCORE], cl->ping, cl->name);
			playerLength = strlen(player);
			if (statusLength + playerLength >= sizeof(status) ) {
				break;		// can't hold any more
			}
			strcpy (status + statusLength, player);
			statusLength += playerLength;
		}
	}

	Info_SetValueForKey(infostring, "version", com_version->string);

	NET_OutOfBandPrint( NS_SERVER, from, "statusResponse\n%s\n%s", infostring, status );
}

/*
================
SVC_Info

Responds with a short info message that should be enough to determine
if a user is interested in a server to do a full status
================
*/
void SVC_Info( netadr_t from ) {
	int		i, count, wDisable;
	const char *gamedir;
	char	infostring[MAX_INFO_STRING];

	// q3infoboom exploit
	if (strlen(Cmd_Argv(1)) > 128)
		return;

	// don't count privateclients
	count = 0;
	for ( i = sv_privateClients->integer ; i < sv_maxclients->integer ; i++ ) {
		if ( svs.clients[i].state >= CS_CONNECTED ) {
			count++;
		}
	}

	infostring[0] = 0;

	// echo back the parameter to status. so servers can use it as a challenge
	// to prevent timed spoofed reply packets that add ghost servers
	Info_SetValueForKey( infostring, "challenge", Cmd_Argv(1) );

	Info_SetValueForKey( infostring, "protocol", va("%i", MV_GetCurrentProtocol()) );
	Info_SetValueForKey( infostring, "hostname", sv_hostname->string );
	Info_SetValueForKey( infostring, "mapname", sv_mapname->string );
	Info_SetValueForKey( infostring, "clients", va("%i", count) );
	Info_SetValueForKey( infostring, "sv_maxclients",
		va("%i", sv_maxclients->integer - sv_privateClients->integer ) );
	Info_SetValueForKey( infostring, "gametype", va("%i", sv_gametype->integer ) );
	Info_SetValueForKey( infostring, "needpass", va("%i", sv_needpass->integer ) );
	Info_SetValueForKey( infostring, "truejedi", va("%i", Cvar_VariableIntegerValue( "g_jediVmerc" ) ) );
	if ( sv_gametype->integer == GT_TOURNAMENT )
	{
		wDisable = Cvar_VariableIntegerValue( "g_duelWeaponDisable" );
	}
	else
	{
		wDisable = Cvar_VariableIntegerValue( "g_weaponDisable" );
	}
	Info_SetValueForKey( infostring, "wdisable", va("%i", wDisable ) );
	Info_SetValueForKey( infostring, "fdisable", va("%i", Cvar_VariableIntegerValue( "g_forcePowerDisable" ) ) );
	//Info_SetValueForKey( infostring, "pure", va("%i", sv_pure->integer ) );

	if( sv_minPing->integer ) {
		Info_SetValueForKey( infostring, "minPing", va("%i", sv_minPing->integer) );
	}
	if( sv_maxPing->integer ) {
		Info_SetValueForKey( infostring, "maxPing", va("%i", sv_maxPing->integer) );
	}
	gamedir = Cvar_VariableString( "fs_game" );
	if( *gamedir ) {
		Info_SetValueForKey( infostring, "game", gamedir );
	}
	Info_SetValueForKey( infostring, "sv_allowAnonymous", va("%i", sv_allowAnonymous->integer) );

	// webserver port
	if (mv_httpdownloads->integer) {
		if (Q_stristr(mv_httpserverport->string, "http://")) {
			Info_SetValueForKey(infostring, "mvhttpurl", mv_httpserverport->string);
		} else {
			Info_SetValueForKey(infostring, "mvhttp", va("%i", sv.http_port));
		}
	}

	NET_OutOfBandPrint( NS_SERVER, from, "infoResponse\n%s", infostring );
}

/*
================
SVC_FlushRedirect

================
*/
void SV_FlushRedirect( char *outputbuf ) {
	NET_OutOfBandPrint( NS_SERVER, svs.redirectAddress, "print\n%s", outputbuf );
}

/*
===============
SVC_RemoteCommand

An rcon packet arrived from the network.
Shift down the remaining args
Redirect all printfs
===============
*/
void SVC_RemoteCommand( netadr_t from, msg_t *msg ) {
	qboolean	valid;
// max length that MSG_ReadString in CL_ConnectionlessPacket can read
#define	SV_OUTPUTBUF_LENGTH MAX_STRING_CHARS
	char		sv_outputbuf[SV_OUTPUTBUF_LENGTH];

	if ( !strlen( sv_rconPassword->string ) ||
		strcmp (Cmd_Argv(1), sv_rconPassword->string) ) {
		valid = qfalse;
		Com_DPrintf ("Bad rcon from %s: %s\n", NET_AdrToString (from), Cmd_ArgsFrom(2) );
	} else {
		valid = qtrue;
		Com_DPrintf ("Rcon from %s: %s\n", NET_AdrToString (from), Cmd_ArgsFrom(2) );
	}

	// start redirecting all print outputs to the packet
	svs.redirectAddress = from;
	Com_BeginRedirect (sv_outputbuf, SV_OUTPUTBUF_LENGTH, SV_FlushRedirect, qfalse);

	if ( !strlen( sv_rconPassword->string ) ) {
		Com_Printf ("No rconpassword set.\n");
	} else if ( !valid ) {
		Com_Printf ("Bad rconpassword.\n");
	} else {
		SVC_WhitelistAdr( from );

		Cmd_DropArg (1);
		Cmd_DropArg (0);
		Cmd_Execute ();
	}

	Com_EndRedirect ();
}

/*
===============
MVAPI_GetConnectionlessPacket
===============
*/
mvaddr_t curraddr;
char currmessage[MAX_STRING_CHARS];
qboolean MVAPI_GetConnectionlessPacket(mvaddr_t *addr, char *buf, int bufsize) {
	if (!mv_apiConnectionless->integer) {
		return qtrue;
	}

	if (currmessage[0] == 0) {
		return qtrue;
	}

	Com_Memcpy(addr, &curraddr, sizeof(curraddr));
	Q_strncpyz(buf, currmessage, bufsize);
	return qfalse;
}

/*
===============
MVAPI_SendConnectionlessPacket
===============
*/
qboolean MVAPI_SendConnectionlessPacket(const mvaddr_t *addr, const char *message) {
	netadr_t nativeAdr;

	if (!mv_apiConnectionless->integer) {
		return qtrue;
	}

	if (addr->type != MV_IPV4) {
		return qtrue;
	}

	nativeAdr.type = NA_IP;
	nativeAdr.ip[0] = addr->ip.v4[0];
	nativeAdr.ip[1] = addr->ip.v4[1];
	nativeAdr.ip[2] = addr->ip.v4[2];
	nativeAdr.ip[3] = addr->ip.v4[3];
	nativeAdr.port = addr->port;

	NET_OutOfBandPrint(NS_SERVER, nativeAdr, "%s", message);
	return qfalse;
}

qboolean mvStructConversionDisabled = qfalse;
qboolean MVAPI_DisableStructConversion(qboolean disable)
{
	mvStructConversionDisabled = disable;

	return qfalse;
}

// ddos protection whitelist

#define WHITELIST_FILE			"ipwhitelist.dat"

#include <set>

static std::set<int32_t>	svc_whitelist;

void SVC_LoadWhitelist( void ) {
	fileHandle_t f;
	int32_t *data = NULL;
	int len = FS_SV_FOpenFileRead(WHITELIST_FILE, &f);

	if (len <= 0) {
		return;
	}

	data = (int32_t *)Z_Malloc(len, TAG_TEMP_WORKSPACE);

	FS_FLock(f, FLOCK_SH, qfalse);
	FS_Read(data, len, f);
	FS_FLock(f, FLOCK_UN, qfalse);
	FS_FCloseFile(f);

	len /= sizeof(int32_t);

	for (int i = 0; i < len; i++) {
		svc_whitelist.insert(data[i]);
	}

	Z_Free(data);
	data = NULL;
}

void SVC_WhitelistAdr( netadr_t adr ) {
	fileHandle_t f;

	if (adr.type != NA_IP) {
		return;
	}

	if (!svc_whitelist.insert(adr.ipi).second) {
		return;
	}

	Com_DPrintf("Whitelisting %s\n", NET_AdrToString(adr));

	f = FS_SV_FOpenFileAppend(WHITELIST_FILE);
	if (!f) {
		Com_Printf("Couldn't open " WHITELIST_FILE ".\n");
		return;
	}

	FS_FLock(f, FLOCK_EX, qfalse);
	FS_Write(adr.ip, sizeof(adr.ip), f);
	FS_FLock(f, FLOCK_UN, qfalse);
	FS_FCloseFile(f);
}

static bool SVC_IsWhitelisted( netadr_t adr ) {
	if (adr.type == NA_IP) {
		return svc_whitelist.find(adr.ipi) != svc_whitelist.end();
	} else {
		return true;
	}
}

//

typedef enum {
	SVC_INVALID,
	SVC_CONNECT,
	SVC_GETSTATUS,
	SVC_FIRST = SVC_GETSTATUS,
	SVC_GETINFO,
	SVC_GETCHALLENGE,
	SVC_RCON,
	SVC_DISCONNECT,
	SVC_MVAPI,
	SVC_MAX
} svcType_t;

/*
=================
SV_ConnectionlessPacket

A connectionless packet has four leading 0xff
characters to distinguish it from a game channel.
Clients that are in the game can still send
connectionless packets.
=================
*/
void SV_ConnectionlessPacket( netadr_t from, msg_t *msg ) {
	static unsigned droppedAdr;
	static unsigned dropped[SVC_MAX];
	static int lastMsgAdr;
	static int lastMsg[SVC_MAX];
	static leakyBucket_t bucket[SVC_MAX];
	static const char * const commands[SVC_MAX] = {
		"invalid",
		"connect",
		"getstatus",
		"getinfo",
		"getchallenge",
		"rcon",
		"disconnect",
		"mvapi"
	};

	char	*s;
	char	*c;

	svcType_t cmd = SVC_INVALID;
	int now = Sys_Milliseconds();

	if (SVC_RateLimitAddress(from, 10, 1000, now)) {
		if (com_developer && com_developer->integer) {
			Com_DPrintf("SV_ConnectionlessPacket: rate limit from %s exceeded, dropping request\n", NET_AdrToString(from));
		}
		droppedAdr++;
		return;
	}

	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg );		// skip the -1 marker

	if (!Q_strncmp("connect", (const char *)&msg->data[4], 7)) {
		Huff_Decompress(msg, 12);
		cmd = SVC_CONNECT;
	}

	s = MSG_ReadStringLine( msg );
	Cmd_TokenizeString( s );
	c = Q_strlwr(Cmd_Argv(0));

	for (int i = SVC_FIRST; i < SVC_MAX && cmd == SVC_INVALID; i++) {
		if (!strcmp(c, commands[i])) {
			cmd = (svcType_t)i;
		}
	}

	int rate = Com_Clampi(1, 1000, sv_maxOOBRate->integer);
	int period = 1000 / rate;
	int burst = rate;			// one second worth of packets

	// Whitelisted IPs can still go through when not-whitelisted rate
	// limit is expleted. If server is DDOSed from whitelisted ips,
	// OOB packets from not-whitelisted IPs never go through.
	if (SVC_IsWhitelisted(from)) {
		burst *= 2;
	}

	if (SVC_RateLimit(&bucket[cmd], burst, period, now)) {
		dropped[cmd]++;
		return;
	}

	if (dropped[cmd] > 0 && lastMsg[cmd] + 1000 < now) {
		Com_Printf("SV_ConnectionlessPacket: \"%s\" rate limit exceeded, dropped %d requests\n", commands[cmd], dropped[cmd]);
		dropped[cmd] = 0;
		lastMsg[cmd] = now;
	}

	if (droppedAdr > 0 && lastMsgAdr + 1000 < now) {
		Com_Printf("SV_ConnectionlessPacket: IP rate limit exceeded, dropped %d requests\n", droppedAdr);
		droppedAdr = 0;
		lastMsgAdr = now;
	}

	Com_DPrintf ("SV packet %s : %s\n", NET_AdrToString(from), c);

	switch (cmd) {
	case SVC_GETSTATUS:
		SVC_Status( from );
		break;
	case SVC_GETINFO:
		SVC_Info( from );
		break;
	case SVC_GETCHALLENGE:
		SV_GetChallenge( from );
		break;
	case SVC_CONNECT:
		SV_DirectConnect( from );
		break;
	case SVC_RCON:
		SVC_RemoteCommand( from, msg );
		break;
	case SVC_DISCONNECT:
		// if a client starts up a local server, we may see some spurious
		// server disconnect messages when their new server sees our final
		// sequenced messages to the old client
		break;
	case SVC_MVAPI:
		if (VM_MVAPILevel(gvm) >= 1 && from.type == NA_IP) {
			Q_strncpyz(currmessage, s, sizeof(currmessage));
			curraddr.type = MV_IPV4;
			curraddr.ip.v4[0] = from.ip[0];
			curraddr.ip.v4[1] = from.ip[1];
			curraddr.ip.v4[2] = from.ip[2];
			curraddr.ip.v4[3] = from.ip[3];
			curraddr.port = from.port;

			VM_Call(gvm, GAME_MVAPI_RECV_CONNECTIONLESSPACKET);

			currmessage[0] = 0;
		}
		break;
	default:
		Com_DPrintf("bad connectionless packet from %s:\n%s\n", NET_AdrToString(from), s);
		break;
	}
}


//============================================================================

/*
=================
SV_ReadPackets
=================
*/
void SV_PacketEvent( netadr_t from, msg_t *msg ) {
	int			i;
	client_t	*cl;
	int			qport;

	// check for connectionless packet (0xffffffff) first
	if ( msg->cursize >= 4 && *(int *)msg->data == -1) {
		SV_ConnectionlessPacket( from, msg );
		return;
	}

	// read the qport out of the message so we can fix up
	// stupid address translating routers
	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg );				// sequence number
	qport = MSG_ReadShort( msg ) & 0xffff;

	// find which client the message is from
	for (i=0, cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++) {
		if (cl->state == CS_FREE) {
			continue;
		}
		if ( !NET_CompareBaseAdr( from, cl->netchan.remoteAddress ) ) {
			continue;
		}
		// it is possible to have multiple clients from a single IP
		// address, so they are differentiated by the qport variable
		if (cl->netchan.qport != qport) {
			continue;
		}

		// the IP port can't be used to differentiate them, because
		// some address translating routers periodically change UDP
		// port assignments
		if (cl->netchan.remoteAddress.port != from.port) {
			Com_Printf( "SV_ReadPackets: fixing up a translated port\n" );
			cl->netchan.remoteAddress.port = from.port;
		}

		// make sure it is a valid, in sequence packet
		if (SV_Netchan_Process(cl, msg)) {
			// zombie clients still need to do the Netchan_Process
			// to make sure they don't need to retransmit the final
			// reliable message, but they don't do any other processing
			if (cl->state != CS_ZOMBIE) {
				cl->lastPacketTime = svs.time;	// don't timeout
				SV_ExecuteClientMessage( cl, msg );
			}
		}
		return;
	}

	// if we received a sequenced packet from an address we don't reckognize,
	// send an out of band disconnect packet to it
	NET_OutOfBandPrint( NS_SERVER, from, "disconnect" );
}


/*
===================
SV_CalcPings

Updates the cl->ping variables
===================
*/
void SV_CalcPings( void ) {
	int			i, j;
	client_t	*cl;
	int			total, count;
	int			delta;
	playerState_t	*ps;

	for (i=0 ; i < sv_maxclients->integer ; i++) {
		cl = &svs.clients[i];
		if ( cl->state != CS_ACTIVE ) {
			cl->ping = 999;
			continue;
		}
		if ( !cl->gentity ) {
			cl->ping = 999;
			continue;
		}
		if ( cl->gentity->r.svFlags & SVF_BOT ) {
			cl->ping = 0;
			continue;
		}

		total = 0;
		count = 0;
		for ( j = 0 ; j < PACKET_BACKUP ; j++ ) {
			if ( cl->frames[j].messageAcked == -1 ) {
				continue;
			}
			delta = cl->frames[j].messageAcked - cl->frames[j].messageSent;
			count++;
			total += delta;
		}
		if (!count) {
			cl->ping = 999;
		} else {
			cl->ping = total/count;
			if ( cl->ping > 999 ) {
				cl->ping = 999;
			}
			if ( sv_pingFix->integer && cl->ping < 1 )
			{ // Botfilters assume that players with 0 ping are bots. So put the minimum ping for humans at 1. At least with the new ping calculation enabled.
				cl->ping = 1;
			}
		}

		// let the game dll know about the ping
		ps = SV_GameClientNum( i );
		ps->ping = cl->ping;
	}
}

/*
==================
SV_CheckTimeouts

If a packet has not been received from a client for timeout->integer
seconds, drop the conneciton.  Server time is used instead of
realtime to avoid dropping the local client while debugging.

When a client is normally dropped, the client_t goes into a zombie state
for a few seconds to make sure any final reliable message gets resent
if necessary
==================
*/
void SV_CheckTimeouts( void ) {
	int		i;
	client_t	*cl;
	int			droppoint;
	int			zombiepoint;

	droppoint = svs.time - 1000 * sv_timeout->integer;
	zombiepoint = svs.time - 1000 * sv_zombietime->integer;

	for (i=0,cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++) {
		// message times may be wrong across a changelevel
		if (cl->lastPacketTime > svs.time) {
			cl->lastPacketTime = svs.time;
		}

		if (cl->state == CS_ZOMBIE
		&& cl->lastPacketTime < zombiepoint) {
			Com_DPrintf( "Going from CS_ZOMBIE to CS_FREE for %s\n", cl->name );
			cl->state = CS_FREE;	// can now be reused
			continue;
		}
		if ( cl->state >= CS_CONNECTED && cl->lastPacketTime < droppoint) {
			// wait several frames so a debugger session doesn't
			// cause a timeout
			if ( ++cl->timeoutCount > 5 ) {
				SV_DropClient (cl, "timed out");
				cl->state = CS_FREE;	// don't bother with zombie state
			}
		} else {
			cl->timeoutCount = 0;
		}
	}
}


/*
==================
SV_CheckPaused
==================
*/
qboolean SV_CheckPaused( void ) {
	int		count;
	client_t	*cl;
	int		i;

	if ( !cl_paused->integer ) {
		return qfalse;
	}

	// only pause if there is just a single client connected
	count = 0;
	for (i=0,cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++) {
		if ( cl->state >= CS_CONNECTED && cl->netchan.remoteAddress.type != NA_BOT ) {
			count++;
		}
	}

	if ( count > 1 ) {
		// don't pause
		sv_paused->integer = 0;
		return qfalse;
	}

	sv_paused->integer = 1;
	return qtrue;
}

void SV_CheckCvars(void) {
	static int lastModHostname = -1, lastModFramerate = -1, lastModSnapsMin = -1, lastModSnapsMax = -1;
	static int lastModEnforceSnaps = -1;
	qboolean changed = qfalse;

	if (sv_hostname->modificationCount != lastModHostname) {
		char hostname[MAX_INFO_STRING];
		char *c = hostname;
		lastModHostname = sv_hostname->modificationCount;

		strcpy(hostname, sv_hostname->string);
		while (*c)
		{
			if ((*c == '\\') || (*c == ';') || (*c == '"'))
			{
				*c = '.';
				changed = qtrue;
			}
			c++;
		}
		if (changed)
		{
			Cvar_Set("sv_hostname", hostname);
		}
	}

	// check limits on client "snaps" value based on server framerate and snapshot rate
	if (sv_fps->modificationCount != lastModFramerate ||
		sv_minSnaps->modificationCount != lastModSnapsMin ||
		sv_maxSnaps->modificationCount != lastModSnapsMax ||
		sv_enforceSnaps->modificationCount != lastModEnforceSnaps)
	{
		client_t *cl;
		int i;

		lastModFramerate = sv_fps->modificationCount;
		lastModSnapsMin = sv_minSnaps->modificationCount;
		lastModSnapsMax = sv_maxSnaps->modificationCount;
		lastModEnforceSnaps = sv_enforceSnaps->modificationCount;

		for (i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++) {
			if ( cl->state >= CS_CONNECTED ) {
				SV_ClientUpdateSnaps( cl );
			}
		}
	}
}

/*
==================
SV_FrameMsec
Return time in millseconds until processing of the next server frame.
==================
*/
int SV_FrameMsec() {
	if (sv_fps) {
		int frameMsec;

		if ( svs.hibernation.enabled && svs.hibernation.disableUntil <= svs.time ) { // Hibernation mode
			frameMsec = 1000.0f / (sv_hibernateFps->integer > 0 ? sv_hibernateFps->integer : 5);
		} else {
			frameMsec = 1000.0f / sv_fps->value;
		}

		if (frameMsec < sv.timeResidual)
			return 0;
		else
			return frameMsec - sv.timeResidual;
	} else
		return 1;
}

static void MV_FixSaberStealing( void )
{
	if ( mv_fixsaberstealing->integer && !(sv.fixes & MVFIX_SABERSTEALING) ) {
		playerState_t	*ps;
		int				i;

		for ( i = 0; i < sv_maxclients->integer; i++ ) {
			ps = SV_GameClientNum( i );

			if ( ps->persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
				ps->saberEntityNum = ENTITYNUM_NONE;
			}
		}
	}
}

/*
==================
SV_Frame

Player movement occurs as a result of packet events, which
happen before SV_Frame is called
==================
*/
void SV_Frame( int msec ) {
	int		frameMsec;
	int		startTime;

	// the menu kills the server with this cvar
	if ( sv_killserver->integer ) {
		SV_Shutdown ("Server was killed");
		Cvar_Set( "sv_killserver", "0" );
		return;
	}

	if ( !com_sv_running->integer ) {
		return;
	}

	// allow pause if only the local client is connected
	if ( SV_CheckPaused() ) {
		return;
	}

	// if it isn't time for the next frame, do nothing
	if ( sv_fps->integer < 1 ) {
		Cvar_Set( "sv_fps", "10" );
	}
	frameMsec = 1000 / sv_fps->integer ;

	sv.timeResidual += msec;

	// Check for hibernation mode
	int players = 0;
	for (int i = 0; i < sv_maxclients->integer; i++) {
		if (svs.clients[i].state >= CS_CONNECTED && svs.clients[i].netchan.remoteAddress.type != NA_BOT) {
			players++;
		}
	}

	if ( sv_hibernateFps->integer && !svs.hibernation.enabled && !players ) {
		svs.hibernation.enabled = true;
		Com_Printf("Server switched to hibernation mode\n");
	} else if  (!sv_hibernateFps->integer && svs.hibernation.enabled ) {
		svs.hibernation.enabled = false;
		Com_Printf("Server restored from hibernation\n");
	}

	if (!com_dedicated->integer) SV_BotFrame( sv.time + sv.timeResidual );

	// if time is about to hit the 32nd bit, kick all clients
	// and clear sv.time, rather
	// than checking for negative time wraparound everywhere.
	// 2giga-milliseconds = 23 days, so it won't be too often
	if ( svs.time > 0x70000000 ) {
		SV_Shutdown( "Restarting server due to time wrapping" );
		Cbuf_AddText( va( "map %s\n", Cvar_VariableString( "mapname" ) ) );
		return;
	}
	// this can happen considerably earlier when lots of clients play and the map doesn't change
	if ( svs.nextSnapshotEntities >= 0x7FFFFFFE - svs.numSnapshotEntities ) {
		SV_Shutdown( "Restarting server due to numSnapshotEntities wrapping" );
		Cbuf_AddText( va( "map %s\n", Cvar_VariableString( "mapname" ) ) );
		return;
	}

	if( sv.restartTime && sv.time >= sv.restartTime ) {
		sv.restartTime = 0;
		Cbuf_AddText( "map_restart 0\n" );
		return;
	}

	// update infostrings if anything has been changed
	if ( cvar_modifiedFlags & CVAR_SERVERINFO ) {
		SV_SetConfigstring( CS_SERVERINFO, Cvar_InfoString( CVAR_SERVERINFO ) );
		cvar_modifiedFlags &= ~CVAR_SERVERINFO;
	}
	if ( cvar_modifiedFlags & CVAR_SYSTEMINFO ) {
		SV_SetConfigstring( CS_SYSTEMINFO, Cvar_InfoString_Big( CVAR_SYSTEMINFO ) );
		cvar_modifiedFlags &= ~CVAR_SYSTEMINFO;
	}

	if ( com_speeds->integer ) {
		startTime = Sys_Milliseconds ();
	} else {
		startTime = 0;	// quite a compiler warning
	}

	// update ping based on the all received frames
	SV_CalcPings();

	if (com_dedicated->integer) SV_BotFrame( sv.time );

	if (sv.saberBlockTime < sv.time) {
		sv.saberBlockCounter = 0;
		sv.saberBlockTime = sv.time + 1000;
	}

	// run the game simulation in chunks
	while ( sv.timeResidual >= frameMsec ) {
		sv.timeResidual -= frameMsec;
		sv.time += frameMsec;
		svs.time += frameMsec;

		// let everything in the world think and move
		VM_Call( gvm, GAME_RUN_FRAME, sv.time );
		MV_FixSaberStealing();
	}

	if ( com_speeds->integer ) {
		time_game = Sys_Milliseconds () - startTime;
	}

	// check timeouts
	SV_CheckTimeouts();

	// send messages back to the clients
	SV_SendClientMessages();

	SV_CheckCvars();

	// send a heartbeat to the master if needed
	SV_MasterHeartbeat();
}

//============================================================================

