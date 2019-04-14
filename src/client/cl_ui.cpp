
#include "client.h"
#include "snd_public.h"

#include "../game/botlib.h"
#include "../qcommon/strip.h"

/*
Ghoul2 Insert Start
*/

#if !defined(G2_H_INC)
	#include "../ghoul2/G2_local.h"
#endif

/*
Ghoul2 Insert End
*/

extern	botlib_export_t	*botlib_export;
void SP_Register(const char *Package);

int UI_ConcatDLList(dlfile_t *files, const int maxfiles);
qboolean UI_DeleteDLFile(const dlfile_t *file);

vm_t *uivm;

/*
====================
GetClientState
====================
*/
static void GetClientState( uiClientState_t *state ) {
	state->connectPacketCount = clc.connectPacketCount;
	state->connState = cls.state;
	Q_strncpyz( state->servername, cls.servername, sizeof( state->servername ) );
	Q_strncpyz( state->updateInfoString, cls.updateInfoString, sizeof( state->updateInfoString ) );
	Q_strncpyz( state->messageString, clc.serverMessage, sizeof( state->messageString ) );
	state->clientNum = cl.snap.ps.clientNum;
}

/*
====================
LAN_LoadCachedServers
====================
*/
void LAN_LoadCachedServers( ) {
	int size;
	fileHandle_t fileIn;
	cls.numglobalservers = cls.nummplayerservers = cls.numfavoriteservers = 0;
	cls.numGlobalServerAddresses = 0;
	if (FS_SV_FOpenFileRead("servercache.dat", &fileIn)) {
		FS_Read(&cls.numglobalservers, sizeof(int), fileIn);
		FS_Read(&cls.nummplayerservers, sizeof(int), fileIn);
		FS_Read(&cls.numfavoriteservers, sizeof(int), fileIn);
		FS_Read(&size, sizeof(int), fileIn);
		if (size == sizeof(cls.globalServers) + sizeof(cls.favoriteServers) + sizeof(cls.mplayerServers)) {
			FS_Read(&cls.globalServers, sizeof(cls.globalServers), fileIn);
			FS_Read(&cls.mplayerServers, sizeof(cls.mplayerServers), fileIn);
			FS_Read(&cls.favoriteServers, sizeof(cls.favoriteServers), fileIn);
		} else {
			cls.numglobalservers = cls.nummplayerservers = cls.numfavoriteservers = 0;
			cls.numGlobalServerAddresses = 0;
		}
		FS_FCloseFile(fileIn);
	}
}

/*
====================
LAN_SaveServersToCache
====================
*/
void LAN_SaveServersToCache( ) {
	int size;
	fileHandle_t fileOut = FS_SV_FOpenFileWrite("servercache.dat");
	FS_Write(&cls.numglobalservers, sizeof(int), fileOut);
	FS_Write(&cls.nummplayerservers, sizeof(int), fileOut);
	FS_Write(&cls.numfavoriteservers, sizeof(int), fileOut);
	size = sizeof(cls.globalServers) + sizeof(cls.favoriteServers) + sizeof(cls.mplayerServers);
	FS_Write(&size, sizeof(int), fileOut);
	FS_Write(&cls.globalServers, sizeof(cls.globalServers), fileOut);
	FS_Write(&cls.mplayerServers, sizeof(cls.mplayerServers), fileOut);
	FS_Write(&cls.favoriteServers, sizeof(cls.favoriteServers), fileOut);
	FS_FCloseFile(fileOut);
}


/*
====================
LAN_ResetPings
====================
*/
static void LAN_ResetPings(int source) {
	int count,i;
	serverInfo_t *servers = NULL;
	count = 0;

	switch (source) {
		case AS_LOCAL :
			servers = &cls.localServers[0];
			count = MAX_OTHER_SERVERS;
			break;
		case AS_MPLAYER :
			servers = &cls.mplayerServers[0];
			count = MAX_OTHER_SERVERS;
			break;
		case AS_GLOBAL :
			servers = &cls.globalServers[0];
			count = MAX_GLOBAL_SERVERS;
			break;
		case AS_FAVORITES :
			servers = &cls.favoriteServers[0];
			count = MAX_OTHER_SERVERS;
			break;
	}
	if (servers) {
		for (i = 0; i < count; i++) {
			servers[i].ping = -1;
		}
	}
}

/*
====================
LAN_AddServer
====================
*/
static int LAN_AddServer(int source, const char *name, const char *address) {
	int max, *count, i;
	netadr_t adr;
	serverInfo_t *servers = NULL;
	max = MAX_OTHER_SERVERS;
	count = 0;

	switch (source) {
		case AS_LOCAL :
			count = &cls.numlocalservers;
			servers = &cls.localServers[0];
			break;
		case AS_MPLAYER :
			count = &cls.nummplayerservers;
			servers = &cls.mplayerServers[0];
			break;
		case AS_GLOBAL :
			max = MAX_GLOBAL_SERVERS;
			count = &cls.numglobalservers;
			servers = &cls.globalServers[0];
			break;
		case AS_FAVORITES :
			count = &cls.numfavoriteservers;
			servers = &cls.favoriteServers[0];
/*			if (!name || !*name)
			{
				name = "?";
			}
*/
			break;
	}
	if (servers && *count < max) {
		NET_StringToAdr( address, &adr );
		if (adr.type == NA_BAD)
		{
			return -1;
		}
		for ( i = 0; i < *count; i++ ) {
			if (NET_CompareAdr(servers[i].adr, adr)) {
				break;
			}
		}
		if (i >= *count) {
			servers[*count].adr = adr;
			Q_strncpyz(servers[*count].hostName, name, sizeof(servers[*count].hostName));
			servers[*count].visible = qtrue;
			(*count)++;
			return 1;
		}
		return 0;
	}
	return -1;
}

/*
====================
LAN_RemoveServer
====================
*/
static void LAN_RemoveServer(int source, const char *addr) {
	int *count, i;
	serverInfo_t *servers = NULL;
	count = 0;
	switch (source) {
		case AS_LOCAL :
			count = &cls.numlocalservers;
			servers = &cls.localServers[0];
			break;
		case AS_MPLAYER :
			count = &cls.nummplayerservers;
			servers = &cls.mplayerServers[0];
			break;
		case AS_GLOBAL :
			count = &cls.numglobalservers;
			servers = &cls.globalServers[0];
			break;
		case AS_FAVORITES :
			count = &cls.numfavoriteservers;
			servers = &cls.favoriteServers[0];
			break;
	}
	if (servers) {
		netadr_t comp;
		NET_StringToAdr( addr, &comp );
		for (i = 0; i < *count; i++) {
			if (servers[i].adr.type==NA_BAD || NET_CompareAdr( comp, servers[i].adr)) {
				int j = i;
				while (j < *count - 1) {
					Com_Memcpy(&servers[j], &servers[j+1], sizeof(servers[j]));
					j++;
				}
				(*count)--;
				break;
			}
		}
	}
}


/*
====================
LAN_GetServerCount
====================
*/
static int LAN_GetServerCount( int source ) {
	switch (source) {
		case AS_LOCAL :
			return cls.numlocalservers;
			break;
		case AS_MPLAYER :
			return cls.nummplayerservers;
			break;
		case AS_GLOBAL :
			return cls.numglobalservers;
			break;
		case AS_FAVORITES :
			return cls.numfavoriteservers;
			break;
	}
	return 0;
}

/*
====================
LAN_GetLocalServerAddressString
====================
*/
static void LAN_GetServerAddressString( int source, int n, char *buf, int buflen ) {
	switch (source) {
		case AS_LOCAL :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				Q_strncpyz(buf, NET_AdrToString( cls.localServers[n].adr) , buflen );
				return;
			}
			break;
		case AS_MPLAYER :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				Q_strncpyz(buf, NET_AdrToString( cls.mplayerServers[n].adr) , buflen );
				return;
			}
			break;
		case AS_GLOBAL :
			if (n >= 0 && n < MAX_GLOBAL_SERVERS) {
				Q_strncpyz(buf, NET_AdrToString( cls.globalServers[n].adr) , buflen );
				return;
			}
			break;
		case AS_FAVORITES :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				Q_strncpyz(buf, NET_AdrToString( cls.favoriteServers[n].adr) , buflen );
				return;
			}
			break;
	}
	buf[0] = '\0';
}

/*
====================
LAN_GetServerInfo
====================
*/
static void LAN_GetServerInfo( int source, int n, char *buf, int buflen ) {
	char info[MAX_STRING_CHARS];
	serverInfo_t *server = NULL;
	info[0] = '\0';
	switch (source) {
		case AS_LOCAL :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				server = &cls.localServers[n];
			}
			break;
		case AS_MPLAYER :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				server = &cls.mplayerServers[n];
			}
			break;
		case AS_GLOBAL :
			if (n >= 0 && n < MAX_GLOBAL_SERVERS) {
				server = &cls.globalServers[n];
			}
			break;
		case AS_FAVORITES :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				server = &cls.favoriteServers[n];
			}
			break;
	}
	if (server && buf) {
		buf[0] = '\0';
		Info_SetValueForKey( info, "hostname", server->hostName);
		Info_SetValueForKey( info, "mapname", server->mapName);
		Info_SetValueForKey( info, "clients", va("%i",server->clients));

		if (VM_MVAPILevel(uivm) >= 1) {
			Info_SetValueForKey(info, "bots", va("%i", server->bots));
		}

		Info_SetValueForKey( info, "sv_maxclients", va("%i",server->maxClients));
		Info_SetValueForKey( info, "ping", va("%i",server->ping));
		Info_SetValueForKey( info, "minping", va("%i",server->minPing));
		Info_SetValueForKey( info, "maxping", va("%i",server->maxPing));
		Info_SetValueForKey( info, "game", server->game);
		Info_SetValueForKey( info, "gametype", va("%i",server->gameType));
		Info_SetValueForKey( info, "nettype", va("%i",server->netType));
		Info_SetValueForKey( info, "addr", NET_AdrToString(server->adr));
//		Info_SetValueForKey( info, "sv_allowAnonymous", va("%i", server->allowAnonymous));
		Info_SetValueForKey( info, "needpass", va("%i", server->needPassword ) );
		Info_SetValueForKey( info, "truejedi", va("%i", server->trueJedi ) );
		Info_SetValueForKey( info, "wdisable", va("%i", server->weaponDisable ) );
		Info_SetValueForKey( info, "fdisable", va("%i", server->forceDisable ) );
		Info_SetValueForKey(info, "protocol", va("%i", server->protocol));
		Info_SetValueForKey( info, "gameVersion", va("%i", server->gameVersion) ); // For 1.03 in the menu...
//		Info_SetValueForKey( info, "pure", va("%i", server->pure ) );
		Q_strncpyz(buf, info, buflen);
	} else {
		if (buf) {
			buf[0] = '\0';
		}
	}
}

/*
====================
LAN_GetServerPing
====================
*/
static int LAN_GetServerPing( int source, int n ) {
	serverInfo_t *server = NULL;
	switch (source) {
		case AS_LOCAL :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				server = &cls.localServers[n];
			}
			break;
		case AS_MPLAYER :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				server = &cls.mplayerServers[n];
			}
			break;
		case AS_GLOBAL :
			if (n >= 0 && n < MAX_GLOBAL_SERVERS) {
				server = &cls.globalServers[n];
			}
			break;
		case AS_FAVORITES :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				server = &cls.favoriteServers[n];
			}
			break;
	}
	if (server) {
		return server->ping;
	}
	return -1;
}

/*
====================
LAN_GetServerPtr
====================
*/
static serverInfo_t *LAN_GetServerPtr( int source, int n ) {
	switch (source) {
		case AS_LOCAL :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				return &cls.localServers[n];
			}
			break;
		case AS_MPLAYER :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				return &cls.mplayerServers[n];
			}
			break;
		case AS_GLOBAL :
			if (n >= 0 && n < MAX_GLOBAL_SERVERS) {
				return &cls.globalServers[n];
			}
			break;
		case AS_FAVORITES :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				return &cls.favoriteServers[n];
			}
			break;
	}
	return NULL;
}

/*
====================
LAN_CompareServers
====================
*/
static int LAN_CompareServers( int source, int sortKey, int sortDir, int s1, int s2 ) {
	int res;
	serverInfo_t *server1, *server2;

	server1 = LAN_GetServerPtr(source, s1);
	server2 = LAN_GetServerPtr(source, s2);
	if (!server1 || !server2) {
		return 0;
	}

	res = 0;
	switch( sortKey ) {
		case SORT_HOST:
			res = Q_stricmp( server1->hostName, server2->hostName );
			break;

		case SORT_MAP:
			res = Q_stricmp( server1->mapName, server2->mapName );
			break;
		case SORT_CLIENTS: // still there for backwards compatibility with "old" VM's
			if (server1->clients < server2->clients) {
				res = -1;
			}
			else if (server1->clients > server2->clients) {
				res = 1;
			}
			else {
				res = 0;
			}
			break;
		case MVSORT_CLIENTS_NOBOTS:
		{
			int s1realClients = server1->clients - server1->bots;
			int s2realClients = server2->clients - server2->bots;

			if (s1realClients < s2realClients) {
				res = -1;
			} else if (s1realClients > s2realClients) {
				res = 1;
			} else {
				res = 0;
			}
			break;
		}
		case SORT_GAME:
			if (server1->gameType < server2->gameType) {
				res = -1;
			}
			else if (server1->gameType > server2->gameType) {
				res = 1;
			}
			else {
				res = 0;
			}
			break;
		case SORT_PING:
			if (server1->ping < server2->ping) {
				res = -1;
			}
			else if (server1->ping > server2->ping) {
				res = 1;
			}
			else {
				res = 0;
			}
			break;
	}

	if (sortDir) {
		if (res < 0)
			return 1;
		if (res > 0)
			return -1;
		return 0;
	}
	return res;
}

/*
====================
LAN_GetPingQueueCount
====================
*/
static int LAN_GetPingQueueCount( void ) {
	return (CL_GetPingQueueCount());
}

/*
====================
LAN_ClearPing
====================
*/
static void LAN_ClearPing( int n ) {
	CL_ClearPing( n );
}

/*
====================
LAN_GetPing
====================
*/
static void LAN_GetPing( int n, char *buf, int buflen, int *pingtime ) {
	CL_GetPing( n, buf, buflen, pingtime );
}

/*
====================
LAN_GetPingInfo
====================
*/
static void LAN_GetPingInfo( int n, char *buf, int buflen ) {
	CL_GetPingInfo( n, buf, buflen );
}

/*
====================
LAN_MarkServerVisible
====================
*/
static void LAN_MarkServerVisible(int source, int n, qboolean visible ) {
	if (n == -1) {
		int count = MAX_OTHER_SERVERS;
		serverInfo_t *server = NULL;
		switch (source) {
			case AS_LOCAL :
				server = &cls.localServers[0];
				break;
			case AS_MPLAYER :
				server = &cls.mplayerServers[0];
				break;
			case AS_GLOBAL :
				server = &cls.globalServers[0];
				count = MAX_GLOBAL_SERVERS;
				break;
			case AS_FAVORITES :
				server = &cls.favoriteServers[0];
				break;
		}
		if (server) {
			for (n = 0; n < count; n++) {
				server[n].visible = visible;
			}
		}

	} else {
		switch (source) {
			case AS_LOCAL :
				if (n >= 0 && n < MAX_OTHER_SERVERS) {
					cls.localServers[n].visible = visible;
				}
				break;
			case AS_MPLAYER :
				if (n >= 0 && n < MAX_OTHER_SERVERS) {
					cls.mplayerServers[n].visible = visible;
				}
				break;
			case AS_GLOBAL :
				if (n >= 0 && n < MAX_GLOBAL_SERVERS) {
					cls.globalServers[n].visible = visible;
				}
				break;
			case AS_FAVORITES :
				if (n >= 0 && n < MAX_OTHER_SERVERS) {
					cls.favoriteServers[n].visible = visible;
				}
				break;
		}
	}
}


/*
=======================
LAN_ServerIsVisible
=======================
*/
static int LAN_ServerIsVisible(int source, int n ) {
	switch (source) {
		case AS_LOCAL :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				return cls.localServers[n].visible;
			}
			break;
		case AS_MPLAYER :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				return cls.mplayerServers[n].visible;
			}
			break;
		case AS_GLOBAL :
			if (n >= 0 && n < MAX_GLOBAL_SERVERS) {
				return cls.globalServers[n].visible;
			}
			break;
		case AS_FAVORITES :
			if (n >= 0 && n < MAX_OTHER_SERVERS) {
				return cls.favoriteServers[n].visible;
			}
			break;
	}
	return qfalse;
}

/*
=======================
LAN_UpdateVisiblePings
=======================
*/
qboolean LAN_UpdateVisiblePings(int source ) {
	return CL_UpdateVisiblePings_f(source);
}

/*
====================
LAN_GetServerStatus
====================
*/
static int LAN_GetServerStatus( const char *serverAddress, char *serverStatus, int maxLen ) {
	return CL_ServerStatus( serverAddress, serverStatus, maxLen );
}

/*
====================
GetClipboardData
====================
*/
static void GetClipboardData( char *buf, int buflen ) {
	char	*cbd;

	cbd = Sys_GetClipboardData();

	if ( !cbd ) {
		*buf = 0;
		return;
	}

	Q_strncpyz( buf, cbd, buflen );

	Z_Free( cbd );
}

/*
====================
Key_KeynumToStringBuf
====================
*/
// only ever called by binding-display code, therefore returns non-technical "friendly" names
//	in any language that don't necessarily match those in the config file...
//
void Key_KeynumToStringBuf( int keynum, char *buf, int buflen )
{
	const char *psKeyName = Key_KeynumToString( keynum/*, qtrue */);

	// see if there's a more friendly (or localised) name...
	//
	const char *psKeyNameFriendly = SP_GetStringTextString( va("KEYNAMES_KEYNAME_%s",psKeyName) );

	Q_strncpyz( buf, (psKeyNameFriendly && psKeyNameFriendly[0]) ? psKeyNameFriendly : psKeyName, buflen );
}


/*
====================
Key_GetBindingBuf
====================
*/
static void Key_GetBindingBuf( int keynum, char *buf, int buflen ) {
	const char	*value;

	value = Key_GetBinding( keynum );
	if ( value ) {
		Q_strncpyz( buf, value, buflen );
	}
	else {
		*buf = 0;
	}
}

/*
====================
Key_GetCatcher
====================
*/
int Key_GetCatcher( void ) {
	return cls.keyCatchers;
}

/*
====================
Ket_SetCatcher
====================
*/
void Key_SetCatcher( int catcher ) {
	cls.keyCatchers = catcher;
}

/*
====================
GetConfigString
====================
*/
static int GetConfigString(int index, char *buf, int size)
{
	int		offset;

	if (index < 0 || index >= MAX_CONFIGSTRINGS)
		return qfalse;

	offset = cl.gameState.stringOffsets[index];
	if (!offset) {
		if( size ) {
			buf[0] = 0;
		}
		return qfalse;
	}

	Q_strncpyz( buf, cl.gameState.stringData+offset, size);

	return qtrue;
}

/*
====================
CL_UISetVirtualScreen
====================
*/
void CL_UISetVirtualScreen(float w, float h) {
	cls.uixadj = SCREEN_WIDTH / w;
	cls.uiyadj = SCREEN_HEIGHT / h;
}

/*
====================
CL_UISystemCalls

The ui module is making a system call
====================
*/

intptr_t CL_UISystemCalls(intptr_t *args) {
	// fix syscalls from 1.02 to match 1.04
	// this is a mess... can it be done better?
	if (VM_GetGameversion(uivm) == VERSION_1_02) {
		if (args[0] == 61) {
			args[0] = UI_ANYLANGUAGE_READCHARFROMSTRING;
		} else if (args[0] == 62) {
			args[0] = UI_R_MODELBOUNDS;
		} else if (args[0] >= 63 && args[0] <= 92) {
			args[0] += 2;
		}
	}

	switch( args[0] ) {
	case UI_ERROR:
		Com_Error( ERR_DROP, "%s", VMAS(1) );
		return 0;

	case UI_PRINT:
		Com_Printf( "%s", VMAS(1) );
		return 0;

	case UI_MILLISECONDS:
		return Sys_Milliseconds();

	case UI_CVAR_REGISTER:
		Cvar_Register( VMAV(1, vmCvar_t), VMAS(2), VMAS(3), args[4] );
		return 0;

	case UI_CVAR_UPDATE:
		Cvar_Update( VMAV(1, vmCvar_t) );
		return 0;

	case UI_CVAR_SET:
		Cvar_Set2( VMAS(1), VMAS(2), qtrue, qtrue );
		return 0;

	case UI_CVAR_VARIABLEVALUE:
		return FloatAsInt( Cvar_VariableValue( VMAS(1), qtrue ) );

	case UI_CVAR_VARIABLESTRINGBUFFER:
		Cvar_VariableStringBuffer( VMAS(1), VMAP(2, char, args[3]), args[3], qtrue );
		return 0;

	case UI_CVAR_SETVALUE:
		Cvar_SetValue( VMAS(1), VMF(2), qtrue );
		return 0;

	case UI_CVAR_RESET:
		Cvar_Reset( VMAS(1), qtrue );
		return 0;

	case UI_CVAR_CREATE:
		Cvar_Get( VMAS(1), VMAS(2), args[3], qtrue );
		return 0;

	case UI_CVAR_INFOSTRINGBUFFER:
		Cvar_InfoStringBuffer( args[1], VMAP(2, char, args[3]), args[3], qtrue );
		return 0;

	case UI_ARGC:
		return Cmd_Argc();

	case UI_ARGV:
		Cmd_ArgvBuffer( args[1], VMAP(2, char, args[3]), args[3] );
		return 0;

	case UI_CMD_EXECUTETEXT:
		Cbuf_ExecuteText( (cbufExec_t)args[1], VMAS(2) );
		return 0;

	case UI_FS_FOPENFILE:
		return FS_FOpenFileByMode( VMAS(1), VMAV(2, int), (fsMode_t)args[3], MODULE_UI );

	case UI_FS_READ:
		FS_Read2( VMAP(1, char, args[2]), args[2], args[3], MODULE_UI );
		return 0;

	case UI_FS_WRITE:
		FS_Write( VMAP(1, char, args[2]), args[2], args[3], MODULE_UI );
		return 0;

	case UI_FS_FCLOSEFILE:
		FS_FCloseFile( args[1], MODULE_UI );
		return 0;

	case UI_FS_GETFILELIST:
		return FS_GetFileList( VMAS(1), VMAS(2), VMAP(3, char, args[4]), args[4] );

	case UI_R_REGISTERMODEL:
		return re.RegisterModel( VMAS(1) );

	case UI_R_REGISTERSKIN:
		return re.RegisterSkin( VMAS(1) );

	case UI_R_REGISTERSHADERNOMIP:
		return re.RegisterShaderNoMip( VMAS(1) );

	case UI_R_CLEARSCENE:
		re.ClearScene();
		return 0;

	case UI_R_ADDREFENTITYTOSCENE:
		re.AddRefEntityToScene( VMAV(1, const refEntity_t), qfalse );
		return 0;

	case UI_R_ADDPOLYTOSCENE:
		re.AddPolyToScene( args[1], args[2], VMAA(3, const polyVert_t, args[2]), 1 );
		return 0;

	case UI_R_ADDLIGHTTOSCENE:
		re.AddLightToScene( VMAP(1, const vec_t, 3), VMF(2), VMF(3), VMF(4), VMF(5) );
		return 0;

	case UI_R_RENDERSCENE:
		re.RenderScene( VMAV(1, const refdef_t) );
		return 0;

	case UI_R_SETCOLOR:
		re.SetColor( VMAP(1, const vec_t, 4) );
		return 0;

	case UI_R_DRAWSTRETCHPIC:
		re.DrawStretchPic( VMF(1), VMF(2), VMF(3), VMF(4), VMF(5), VMF(6), VMF(7), VMF(8), args[9], cls.uixadj, cls.uiyadj );
		return 0;

	case UI_R_MODELBOUNDS:
		re.ModelBounds( args[1], VMAP(2, vec_t, 3), VMAP(3, vec_t, 3) );
		return 0;

	case UI_UPDATESCREEN:
		SCR_UpdateScreen();
		return 0;

	case UI_CM_LERPTAG:
		re.LerpTag( VMAV(1, orientation_t), args[2], args[3], args[4], VMF(5), VMAS(6) );
		return 0;

	case UI_S_REGISTERSOUND:
		return S_RegisterSound( VMAS(1) );

	case UI_S_STARTLOCALSOUND:
		S_StartLocalSound( args[1], args[2] );
		return 0;

	case UI_KEY_KEYNUMTOSTRINGBUF:
		Key_KeynumToStringBuf(Key_GetProtocolKey15(VM_GetGameversion(uivm), args[1]), VMAP(2, char, args[3]), args[3]); // 1.02 keynums -> 1.04 keynums
		return 0;

	case UI_KEY_GETBINDINGBUF:
		Key_GetBindingBuf(Key_GetProtocolKey15(VM_GetGameversion(uivm), args[1]), VMAP(2, char, args[3]), args[3]); // 1.02 keynums -> 1.04 keynums
		return 0;

	case UI_KEY_SETBINDING:
		Key_SetBinding(Key_GetProtocolKey15(VM_GetGameversion(uivm), args[1]), VMAS(2)); // 1.02 keynums -> 1.04 keynums
		return 0;

	case UI_KEY_ISDOWN:
		return Key_IsDown(Key_GetProtocolKey15(VM_GetGameversion(uivm), args[1])); // 1.02 keynums -> 1.04 keynums

	case UI_KEY_GETOVERSTRIKEMODE:
		return Key_GetOverstrikeMode();

	case UI_KEY_SETOVERSTRIKEMODE:
		Key_SetOverstrikeMode( (qboolean)!!args[1] );
		return 0;

	case UI_KEY_CLEARSTATES:
		Key_ClearStates();
		return 0;

	case UI_KEY_GETCATCHER:
		return Key_GetCatcher();

	case UI_KEY_SETCATCHER:
		Key_SetCatcher( args[1] );
		return 0;

	case UI_GETCLIPBOARDDATA:
		GetClipboardData( VMAP(1, char, args[2]), args[2] );
		return 0;

	case UI_GETCLIENTSTATE:
		GetClientState( VMAV(1, uiClientState_t) );
		return 0;

	case UI_GETGLCONFIG:
		CL_GetVMGLConfig( VMAV(1, vmglconfig_t) );
		return 0;

	case UI_GETCONFIGSTRING:
		return GetConfigString( args[1], VMAP(2, char, args[3]), args[3] );

	case UI_LAN_LOADCACHEDSERVERS:
		LAN_LoadCachedServers();
		return 0;

	case UI_LAN_SAVECACHEDSERVERS:
		LAN_SaveServersToCache();
		return 0;

	case UI_LAN_ADDSERVER:
		return LAN_AddServer( args[1], VMAS(2), VMAS(3) );

	case UI_LAN_REMOVESERVER:
		LAN_RemoveServer( args[1], VMAS(2) );
		return 0;

	case UI_LAN_GETPINGQUEUECOUNT:
		return LAN_GetPingQueueCount();

	case UI_LAN_CLEARPING:
		LAN_ClearPing( args[1] );
		return 0;

	case UI_LAN_GETPING:
		LAN_GetPing( args[1], VMAP(2, char, args[3]), args[3], VMAV(4, int) );
		return 0;

	case UI_LAN_GETPINGINFO:
		LAN_GetPingInfo( args[1], VMAP(2, char, args[3]), args[3] );
		return 0;

	case UI_LAN_GETSERVERCOUNT:
		return LAN_GetServerCount(args[1]);

	case UI_LAN_GETSERVERADDRESSSTRING:
		LAN_GetServerAddressString( args[1], args[2], VMAP(3, char, args[4]), args[4] );
		return 0;

	case UI_LAN_GETSERVERINFO:
		LAN_GetServerInfo( args[1], args[2], VMAP(3, char, args[4]), args[4] );
		return 0;

	case UI_LAN_GETSERVERPING:
		return LAN_GetServerPing( args[1], args[2] );

	case UI_LAN_MARKSERVERVISIBLE:
		LAN_MarkServerVisible( args[1], args[2], (qboolean)!!args[3] );
		return 0;

	case UI_LAN_SERVERISVISIBLE:
		return LAN_ServerIsVisible( args[1], args[2] );

	case UI_LAN_UPDATEVISIBLEPINGS:
		return LAN_UpdateVisiblePings( args[1] );

	case UI_LAN_RESETPINGS:
		LAN_ResetPings( args[1] );
		return 0;

	case UI_LAN_SERVERSTATUS:
		return LAN_GetServerStatus( VMAS(1), VMAP(2, char, args[3]), args[3] );

	case UI_LAN_COMPARESERVERS:
		return LAN_CompareServers( args[1], args[2], args[3], args[4], args[5] );

	case UI_MEMORY_REMAINING:
		return Hunk_MemoryRemaining();

	case UI_R_REGISTERFONT:
		return re.RegisterFont( VMAS(1) );

	case UI_R_FONT_STRLENPIXELS:
		return re.Font_StrLenPixels( VMAS(1), args[2], VMF(3), cls.uixadj, cls.uiyadj );

	case UI_R_FONT_STRLENCHARS:
		return re.Font_StrLenChars( VMAS(1) );

	case UI_R_FONT_STRHEIGHTPIXELS:
		return re.Font_HeightPixels( args[1], VMF(2), cls.uixadj, cls.uiyadj );

	case UI_R_FONT_DRAWSTRING:
		re.Font_DrawString( args[1], args[2], VMAS(3), VMAP(4, const vec_t, 4), args[5], args[6], VMF(7), cls.uixadj, cls.uiyadj );
		return 0;

	case UI_LANGUAGE_ISASIAN:
		return re.Language_IsAsian();

	case UI_LANGUAGE_USESSPACES:
		return re.Language_UsesSpaces();

	case UI_ANYLANGUAGE_READCHARFROMSTRING:
		return re.AnyLanguage_ReadCharFromString( VMAS(1), VMAV(2, int), VMAV(3, qboolean) );

	case UI_MEMSET:
		Com_Memset( VMAP(1, char, args[3]), args[2], args[3] );
		return 0;

	case UI_MEMCPY:
		Com_Memcpy( VMAP(1, char, args[3]), VMAP(2, char, args[3]), args[3] );
		return 0;

	case UI_STRNCPY:
		return VM_strncpy( args[1], args[2], args[3] );

	case UI_SIN:
		return FloatAsInt( sinf( VMF(1) ) );

	case UI_COS:
		return FloatAsInt( cosf( VMF(1) ) );

	case UI_ATAN2:
		return FloatAsInt( atan2f( VMF(1), VMF(2) ) );

	case UI_SQRT:
		return FloatAsInt( VMF(1) < 0 ? 0 : sqrtf( VMF(1) ) );

	case UI_FLOOR:
		return FloatAsInt( floorf( VMF(1) ) );

	case UI_CEIL:
		return FloatAsInt( ceilf( VMF(1) ) );

	case UI_PC_ADD_GLOBAL_DEFINE:
		return botlib_export->PC_AddGlobalDefine( VMAS(1) );
	case UI_PC_LOAD_SOURCE:
		return botlib_export->PC_LoadSourceHandle( VMAS(1) );
	case UI_PC_FREE_SOURCE:
		return botlib_export->PC_FreeSourceHandle( args[1] );
	case UI_PC_READ_TOKEN:
		return botlib_export->PC_ReadTokenHandle( args[1], VMAV(2, pc_token_t) );
	case UI_PC_SOURCE_FILE_AND_LINE:
		return botlib_export->PC_SourceFileAndLine( args[1], VMAP(2, char, MAX_QPATH), VMAV(3, int) );
	case UI_PC_LOAD_GLOBAL_DEFINES:
		return botlib_export->PC_LoadGlobalDefines ( VMAS(1) );
	case UI_PC_REMOVE_ALL_GLOBAL_DEFINES:
		botlib_export->PC_RemoveAllGlobalDefines ( );
		return 0;

	case UI_S_STOPBACKGROUNDTRACK:
		S_StopBackgroundTrack();
		return 0;
	case UI_S_STARTBACKGROUNDTRACK:
		S_StartBackgroundTrack( VMAS(1), VMAS(2), qfalse);
		return 0;

	case UI_REAL_TIME:
		return Com_RealTime( VMAV(1, qtime_t) );

	case UI_CIN_PLAYCINEMATIC:
	  Com_DPrintf("UI_CIN_PlayCinematic\n");
	  return CIN_PlayCinematic(VMAS(1), args[2], args[3], args[4], args[5], args[6]);

	case UI_CIN_STOPCINEMATIC:
	  return CIN_StopCinematic(args[1]);

	case UI_CIN_RUNCINEMATIC:
	  return CIN_RunCinematic(args[1]);

	case UI_CIN_DRAWCINEMATIC:
	  CIN_DrawCinematic(args[1]);
	  return 0;

	case UI_CIN_SETEXTENTS:
	  CIN_SetExtents(args[1], args[2], args[3], args[4], args[5]);
	  return 0;

	case UI_R_REMAP_SHADER:
		re.RemapShader( VMAS(1), VMAS(2), VMAS(3) );
		return 0;

	case UI_SP_REGISTER:
		return !!SP_Register(VMAS(1), SP_REGISTER_MENU);

	case UI_SP_GETSTRINGTEXTSTRING:
		const char* text;

		text = SP_GetStringTextString(VMAS(1));
		Q_strncpyz( VMAP(2, char, args[3]), text, args[3] );
		return qtrue;

/*
Ghoul2 Insert Start
*/
	case UI_G2_ANGLEOVERRIDE:
		return G2API_SetBoneAngles((g2handle_t)args[1], args[2], VMAS(3), VMAP(4, const vec_t, 3), args[5],
							 (const Eorientations) args[6], (const Eorientations) args[7], (const Eorientations) args[8],
							 VMAA(9, qhandle_t, args[2] + 1), args[10], args[11] );
/*
Ghoul2 Insert End
*/

	case MVAPI_GET_VERSION:
		return (int)VM_GetGameversion(uivm);
	}

	if (VM_MVAPILevel(uivm) >= 3) {
		switch (args[0]) {
		case UI_MVAPI_R_ADDREFENTITYTOSCENE2:
			re.AddRefEntityToScene(VMAV(1, const refEntity_t), qtrue);
			return 0;
		case UI_MVAPI_SETVIRTUALSCREEN:
			CL_UISetVirtualScreen(VMF(1), VMF(2));
			return 0;
		case MVAPI_FS_FLOCK:
			return (int)FS_FLock(args[1], (flockCmd_t)args[2], (qboolean)!!args[3], MODULE_UI);
		case MVAPI_SET_VERSION:
			VM_SetGameversion( uivm, (mvversion_t)args[1] );
			return 0;
		}
	}

	if (VM_MVMenuLevel(uivm) >= 2) {
		switch (args[0]) {
			// download popup
		case UI_MVAPI_CONTINUE_DOWNLOAD:
			CL_ContinueCurrentDownload((dldecision_t)args[1]);
			return qtrue;
		case UI_MVAPI_GETDLLIST:
			return UI_ConcatDLList(VMAA(1, dlfile_t, args[2]), args[2]);
		case UI_MVAPI_RMDLPREFIX:
			return FS_RMDLPrefix(VMAS(1));
		case UI_MVAPI_DELDLFILE:
			return UI_DeleteDLFile(VMAV(1, const dlfile_t));
		}
	}

	Com_Error( ERR_DROP, "Bad UI system trap: %lli", (long long int)args[0] );
	return 0;
}

/*
====================
CL_ShutdownUI
====================
*/
void CL_ShutdownUI( void ) {
	cls.keyCatchers &= ~KEYCATCH_UI;
	cls.uiStarted = qfalse;
	if ( !uivm ) {
		return;
	}
	VM_Call( uivm, UI_SHUTDOWN );
	VM_Free( uivm );
	uivm = NULL;
}

/*
====================
CL_InitUI
jk2mv has it's own dll for the main menu
====================
*/
void CL_InitUI(qboolean mainMenu) {
	vmInterpret_t		interpret;
	int v;
	int apilevel = MIN(mv_apienabled->integer, MV_APILEVEL);

	// mv_menuOverride  1 -> force ui module everywhere
	// mv_menuOverride  0 -> mvmenu on main menu; ui module ingame
	// mv_menuOverride -1 -> force mvmenu everywhere

	cvar_t *ui_menulevel = Cvar_Get("ui_menulevel", "0", CVAR_ROM | CVAR_INTERNAL, qfalse);
	Cvar_Set("ui_menulevel", "0");

	if ( (!mainMenu || mv_menuOverride->integer == 1) && mv_menuOverride->integer != -1 ) {
		if (cl_connectedToPureServer != 0) {
			// if sv_pure is set we only allow qvms to be loaded
			interpret = VMI_COMPILED;
		} else {
			interpret = (vmInterpret_t)(int)Cvar_VariableValue("vm_ui");
		}
		uivm = VM_Create("ui", (qboolean)!!mv_menuOverride->integer, CL_UISystemCalls, interpret);
	}

	// Load the mvmenu if we want the mainMenu or failed to load a ui module earlier
	if ( (mainMenu && !mv_menuOverride->integer) || mv_menuOverride->integer == -1 || !uivm ) {
		apilevel = MV_APILEVEL;

		uivm = VM_Create("jk2mvmenu", qtrue, CL_UISystemCalls, VMI_NATIVE);
		VM_SetGameversion(uivm, VERSION_UNDEF);
	}

	if ( !uivm ) {
		Com_Error( ERR_FATAL, "VM_Create on UI failed" );
	}

	cls.uiStarted = qtrue;

	// sanity check
	v = VM_Call( uivm, UI_GETAPIVERSION, 0, 0, 0, 0, 0, 0, 0 ,0 ,0 ,0 ,0, (mainMenu ? MV_MENULEVEL_MAX : 0) );
	if (v != UI_API_16_VERSION && v != UI_API_15_VERSION) {
		CL_ShutdownUI();
		Com_Error(ERR_DROP, "User Interface is version %d, expected %d or %d", v, UI_API_16_VERSION, UI_API_15_VERSION);
	} else {
		int apireq;

		// Using a cvar to deliver the wishlevel so we can just tunnel the menulevel through UI_GETAPIVERSION
		// We initialise the cvar with 0 whenever we load a module; the mvmenu and mvsdk ui set a value != 0 to signal
		// that they support the menu api;
		if ( mainMenu && ui_menulevel->integer )
		{
			if ( ui_menulevel->integer < MV_MENULEVEL_MIN )
				Com_Error(ERR_DROP, "Trying to load a menu with too low level (%d < %d)", ui_menulevel->integer, MV_MENULEVEL_MIN);
			else if ( ui_menulevel->integer <= MV_MENULEVEL_MAX )
				VM_SetMVMenuLevel( uivm, ui_menulevel->integer );
			else
				VM_SetMVMenuLevel( uivm, 0 );
		}
		else
		{
			if ( mainMenu ) Com_Printf("WARNING: Using unsupported module as mainMenu\n");
			VM_SetMVMenuLevel( uivm, 0 );
		}

		apireq = VM_Call( uivm, UI_INIT, mainMenu ? qfalse : (cls.state >= CA_AUTHORIZING && cls.state <= CA_ACTIVE), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, apilevel );
		if (apireq > apilevel) {
			apireq = apilevel;
		}
		VM_SetMVAPILevel(uivm, apireq);
		Com_DPrintf("UIVM uses MVAPI level %i.\n", apireq);

		if (apireq >= 1) {
			VM_Call(uivm, MVAPI_AFTER_INIT);
		}
	}
}

/*
====================
CL_InitMVMenu

Swap UI with mvmenu if it's not loaded already
====================
*/
void CL_InitMVMenu( void ) {
	if ( !VM_MVMenuLevel(uivm) ) {
		CL_ShutdownUI();
		CL_InitUI(qtrue);
	}
}

qboolean UI_usesUniqueCDKey() {
	if (uivm) {
		return (qboolean)(VM_Call( uivm, UI_HASUNIQUECDKEY) == qtrue);
	} else {
		return qfalse;
	}
}

/*
====================
UI_GameCommand

See if the current console command is claimed by the ui
====================
*/
qboolean UI_GameCommand( void ) {
	if ( !uivm ) {
		return qfalse;
	}

	return (qboolean)!!VM_Call( uivm, UI_CONSOLE_COMMAND, cls.realtime );
}

/*
====================
UI_ConcatDLList

Get both, all downloaded files and all blacklisted entrys
Load the blacklist entrys first so they have a better chance
to fit in the buffer
====================
*/
int UI_ConcatDLList(dlfile_t *files, const int maxfiles) {
	int i;

	CL_ReadBlacklistFile();
	for (i = 0; i < cls.downloadBlacklistLen && i < maxfiles; i++) {
		Q_strncpyz(files->name, cls.downloadBlacklist[i].name, sizeof(files->name));
		files->checkksum = cls.downloadBlacklist[i].checksum;
		files->blacklisted = qtrue;

		files++;
	}
	CL_BlacklistWriteCloseFile();

	return i + FS_GetDLList(files, maxfiles - i);
}

qboolean UI_DeleteDLFile(const dlfile_t *file) {
	if (file->blacklisted) {
		int i;

		CL_ReadBlacklistFile();

		for (i = 0; i < cls.downloadBlacklistLen; i++) {
			if (cls.downloadBlacklist[i].checksum == file->checkksum) {
				if (!CL_BlacklistRemoveFile(&cls.downloadBlacklist[i])) {
					CL_BlacklistWriteCloseFile();
					return qfalse;
				}

				break;
			}
		}

		CL_BlacklistWriteCloseFile();
		return qtrue;
	} else {
		return FS_DeleteDLFile(file->name);
	}
}
