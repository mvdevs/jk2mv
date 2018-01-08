// cl_cgame.c  -- client system interaction with client game

#include "client.h"

#include "../game/botlib.h"

#if !defined(FX_EXPORT_H_INC)
	#include "FXExport.h"
#endif

#include "FxUtil.h"

#if !defined(CROFFSYSTEM_H_INC)
	#include "../qcommon/RoffSystem.h"
#endif

#ifdef _DONETPROFILE_
#include "../qcommon/INetProfile.h"
#endif

/*
Ghoul2 Insert Start
*/

#if !defined(G2_H_INC)
	#include "../ghoul2/G2_local.h"
#endif

#include "../qcommon/strip.h"

#ifdef G2_COLLISION_ENABLED
extern CMiniHeap *G2VertSpaceClient;
#endif

/*
Ghoul2 Insert End
*/
extern	botlib_export_t	*botlib_export;

extern qboolean loadCamera(const char *name);
extern void startCamera(int time);
extern qboolean getCameraInfo(int time, vec3_t *origin, vec3_t *angles);

void FX_FeedTrail(const effectTrailArgStruct_t *a);

/*
====================
CL_GetGameState
====================
*/
void CL_GetGameState( gameState_t *gs ) {
	*gs = cl.gameState;
}

/*
====================
CL_GetUserCmd
====================
*/
qboolean CL_GetUserCmd( int cmdNumber, usercmd_t *ucmd ) {
	// cmds[cmdNumber] is the last properly generated command

	// can't return anything that we haven't created yet
	if ( cmdNumber > cl.cmdNumber ) {
		Com_Error( ERR_DROP, "CL_GetUserCmd: %i >= %i", cmdNumber, cl.cmdNumber );
	}

	// the usercmd has been overwritten in the wrapping
	// buffer because it is too far out of date
	if ( cmdNumber <= cl.cmdNumber - CMD_BACKUP ) {
		return qfalse;
	}

	*ucmd = cl.cmds[ cmdNumber & CMD_MASK ];

	return qtrue;
}

int CL_GetCurrentCmdNumber( void ) {
	return cl.cmdNumber;
}


/*
====================
CL_GetParseEntityState
====================
*/
qboolean	CL_GetParseEntityState( int parseEntityNumber, entityState_t *state ) {
	// can't return anything that hasn't been parsed yet
	if ( parseEntityNumber >= cl.parseEntitiesNum ) {
		Com_Error( ERR_DROP, "CL_GetParseEntityState: %i >= %i",
			parseEntityNumber, cl.parseEntitiesNum );
	}

	// can't return anything that has been overwritten in the circular buffer
	if ( parseEntityNumber <= cl.parseEntitiesNum - MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	*state = cl.parseEntities[ parseEntityNumber & ( MAX_PARSE_ENTITIES - 1 ) ];
	return qtrue;
}

/*
====================
CL_GetCurrentSnapshotNumber
====================
*/
void	CL_GetCurrentSnapshotNumber( int *snapshotNumber, int *serverTime ) {
	*snapshotNumber = cl.snap.messageNum;
	*serverTime = cl.snap.serverTime;
}

/*
====================
multiprotocol support
====================
*/
qboolean	CL_GetSnapshot16(int snapshotNumber, snapshot_t *snapshot) {
	clSnapshot_t	*clSnap;
	int				i, count;

	if ( snapshotNumber > cl.snap.messageNum ) {
		Com_Error( ERR_DROP, "CL_GetSnapshot: snapshotNumber > cl.snapshot.messageNum" );
	}

	// if the frame has fallen out of the circular buffer, we can't return it
	if ( cl.snap.messageNum - snapshotNumber >= PACKET_BACKUP ) {
		return qfalse;
	}

	// if the frame is not valid, we can't return it
	clSnap = &cl.snapshots[snapshotNumber & PACKET_MASK];
	if ( !clSnap->valid ) {
		return qfalse;
	}

	// if the entities in the frame have fallen out of their
	// circular buffer, we can't return it
	if ( cl.parseEntitiesNum - clSnap->parseEntitiesNum >= MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	// write the snapshot
	snapshot->snapFlags = clSnap->snapFlags;
	snapshot->serverCommandSequence = clSnap->serverCommandNum;
	snapshot->ping = clSnap->ping;
	snapshot->serverTime = clSnap->serverTime;
	Com_Memcpy( snapshot->areamask, clSnap->areamask, sizeof( snapshot->areamask ) );
	snapshot->ps = clSnap->ps;

	// wp glowing workaround.. this keeps yourself from glowing like a candle when and after charging the blaster pistol on high svs.time
	if (!(cls.fixes & MVFIX_WPGLOWING) && snapshot->ps.weaponstate != WEAPON_CHARGING_ALT && snapshot->ps.weaponstate != WEAPON_CHARGING)
		snapshot->ps.weaponChargeTime = 0;

	count = clSnap->numEntities;
	if ( count > MAX_ENTITIES_IN_SNAPSHOT ) {
		Com_DPrintf( "CL_GetSnapshot: truncated %i entities to %i\n", count, MAX_ENTITIES_IN_SNAPSHOT );
		count = MAX_ENTITIES_IN_SNAPSHOT;
	}
	snapshot->numEntities = count;

	for ( i = 0 ; i < count ; i++ ) {

		int entNum =  ( clSnap->parseEntitiesNum + i ) & (MAX_PARSE_ENTITIES-1) ;

		// copy everything but the ghoul2 pointer
		memcpy(&snapshot->entities[i], &cl.parseEntities[ entNum ], sizeof(entityState_t));

		// wp glowing workaround.. this keeps others from glowing like a candle when and after charging the blaster pistol on high svs.time
		if (!(cls.fixes & MVFIX_WPGLOWING) && ( snapshot->entities[i].eType == ET_BODY || snapshot->entities[i].eType == ET_PLAYER )) {
			snapshot->entities[i].constantLight = 0;
		}
	}

	// FIXME: configstring changes and server commands!!!

	return qtrue;
}

clSnapshot15_t *CL_GetSnapshot15from16(clSnapshot_t *snapshot) {
	static clSnapshot15_t retn;

	memset(&retn, 0, sizeof(clSnapshot15_t));
	retn.valid = snapshot->valid;
	retn.snapFlags = snapshot->snapFlags;
	retn.serverTime = snapshot->serverTime;
	retn.messageNum = snapshot->messageNum;
	retn.deltaNum = snapshot->deltaNum;
	retn.ping = snapshot->ping;
	memcpy(retn.areamask, snapshot->areamask, sizeof(retn.areamask));
	retn.cmdNum = snapshot->cmdNum;
	retn.numEntities = snapshot->numEntities;
	retn.parseEntitiesNum = snapshot->parseEntitiesNum;
	retn.serverCommandNum = snapshot->serverCommandNum;

	// tricky but works atleast on x86
	memcpy(&retn.ps, &snapshot->ps, (((size_t)&snapshot->ps.saberIndex) - (size_t)&snapshot->ps));
	memcpy(&retn.ps.saberIndex, &snapshot->ps.saberIndex, ((size_t)&(&snapshot->ps)[1] - (size_t)&snapshot->ps.saberIndex));

	return &retn;
}

qboolean	CL_GetSnapshot15(int snapshotNumber, snapshot15_t *snapshot) {
	clSnapshot15_t	*clSnap;
	int				i, count;

	if (snapshotNumber > cl.snap.messageNum) {
		Com_Error(ERR_DROP, "CL_GetSnapshot: snapshotNumber > cl.snapshot.messageNum");
	}

	// if the frame has fallen out of the circular buffer, we can't return it
	if (cl.snap.messageNum - snapshotNumber >= PACKET_BACKUP) {
		return qfalse;
	}

	// if the frame is not valid, we can't return it
	clSnap = CL_GetSnapshot15from16(&cl.snapshots[snapshotNumber & PACKET_MASK]);
	if (!clSnap->valid) {
		return qfalse;
	}

	// if the entities in the frame have fallen out of their
	// circular buffer, we can't return it
	if (cl.parseEntitiesNum - clSnap->parseEntitiesNum >= MAX_PARSE_ENTITIES) {
		return qfalse;
	}

	// write the snapshot
	snapshot->snapFlags = clSnap->snapFlags;
	snapshot->serverCommandSequence = clSnap->serverCommandNum;
	snapshot->ping = clSnap->ping;
	snapshot->serverTime = clSnap->serverTime;
	Com_Memcpy(snapshot->areamask, clSnap->areamask, sizeof(snapshot->areamask));
	snapshot->ps = clSnap->ps;

	// wp glowing workaround.. this keeps yourself from glowing like a candle when and after charging the blaster pistol on high svs.time
	if (!(cls.fixes & MVFIX_WPGLOWING) && snapshot->ps.weaponstate != WEAPON_CHARGING_ALT && snapshot->ps.weaponstate != WEAPON_CHARGING)
		snapshot->ps.weaponChargeTime = 0;

	count = clSnap->numEntities;
	if (count > MAX_ENTITIES_IN_SNAPSHOT) {
		Com_DPrintf("CL_GetSnapshot: truncated %i entities to %i\n", count, MAX_ENTITIES_IN_SNAPSHOT);
		count = MAX_ENTITIES_IN_SNAPSHOT;
	}
	snapshot->numEntities = count;

	for (i = 0; i < count; i++) {

		int entNum = (clSnap->parseEntitiesNum + i) & (MAX_PARSE_ENTITIES - 1);

		// copy everything but the ghoul2 pointer
		memcpy(&snapshot->entities[i], &cl.parseEntities[entNum], sizeof(entityState_t));

		// wp glowing workaround.. this keeps others from glowing like a candle when and after charging the blaster pistol on high svs.time
		if (!(cls.fixes & MVFIX_WPGLOWING) && (snapshot->entities[i].eType == ET_BODY || snapshot->entities[i].eType == ET_PLAYER)) {
			snapshot->entities[i].constantLight = 0;
		}
	}

	// FIXME: configstring changes and server commands!!!

	return qtrue;
}

qboolean	CL_GetSnapshot(int snapshotNumber, snapshot_t *snapshot) {
	if (VM_GetGameversion(cgvm) != VERSION_1_02) {
		return CL_GetSnapshot16(snapshotNumber, snapshot);
	} else {
		return CL_GetSnapshot15(snapshotNumber, (snapshot15_t *)snapshot);
	}
}

/*
=====================
CL_SetUserCmdValue
=====================
*/
void CL_SetUserCmdValue( int userCmdValue, float sensitivityScale, int fpSel, int invenSel ) {
	cl.cgameUserCmdValue = userCmdValue;
	cl.cgameSensitivity = sensitivityScale;
	cl.cgameForceSelection = fpSel;
	cl.cgameInvenSelection = invenSel;
}

/*
=====================
CL_SetClientForceAngle
=====================
*/
void CL_SetClientForceAngle(int time, const vec3_t angle)
{
	cl.cgameViewAngleForceTime = time;
	VectorCopy(angle, cl.cgameViewAngleForce);
}

void CL_SetTurnExtents(float turnAdd, float turnSub, int turnTime)
{
	cl.cgameTurnExtentAdd = turnAdd;
	cl.cgameTurnExtentSub = turnSub;
	cl.cgameTurnExtentTime = turnTime;
}

/*
=====================
CL_AddCgameCommand
=====================
*/
void CL_AddCgameCommand( const char *cmdName ) {
	Cmd_AddCommand( cmdName, NULL );
}

/*
=====================
CL_CgameError
=====================
*/
void CL_CgameError( const char *string ) {
	Com_Error( ERR_DROP, "%s", string );
}


int gCLTotalClientNum = 0;
//keep track of the total number of clients
extern cvar_t	*cl_autolodscale;
//if we want to do autolodscaling

void CL_DoAutoLODScale(void)
{
	float finalLODScaleFactor = 0;

	if ( gCLTotalClientNum >= 8 )
	{
		finalLODScaleFactor = (gCLTotalClientNum/-8.0f);
	}


	Cvar_Set( "r_autolodscalevalue", va("%f", finalLODScaleFactor) );
}

/*
=====================
CL_ConfigstringModified
=====================
*/
void CL_ConfigstringModified( void ) {
	char		*old, *s;
	int			i, index;
	char		*dup;
	gameState_t	oldGs;
	int			len;

	index = atoi( Cmd_Argv(1) );
	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		Com_Error( ERR_DROP, "configstring > MAX_CONFIGSTRINGS" );
	}
	// get everything after "cs <num>"
	s = Cmd_ArgsFrom(2);

	old = cl.gameState.stringData + cl.gameState.stringOffsets[ index ];
	if ( !strcmp( old, s ) ) {
		return;		// unchanged
	}

	// build the new gameState_t
	oldGs = cl.gameState;

	Com_Memset( &cl.gameState, 0, sizeof( cl.gameState ) );

	// leave the first 0 for uninitialized strings
	cl.gameState.dataCount = 1;

	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		if ( i == index ) {
			dup = s;
		} else {
			dup = oldGs.stringData + oldGs.stringOffsets[ i ];
		}
		if ( !dup[0] ) {
			continue;		// leave with the default empty string
		}

		len = (int)strlen(dup);

		if ( len + 1 + cl.gameState.dataCount > MAX_GAMESTATE_CHARS ) {
			Com_Error( ERR_DROP, "MAX_GAMESTATE_CHARS exceeded" );
		}

		// ClientSide AntiGalak - replace "galak_mech" with "galak-mech"...
		if ( i >= CS_PLAYERS && i < (CS_PLAYERS + MAX_CLIENTS) )
		{
			char *model;
			char *modelEnd;
			char *galakPos;

			model = Q_stristr( dup, "model\\" ); //Can't use Info_ValueForKey, because it "copies" what it finds...
			if ( model )
			{
				modelEnd = Q_stristr( model, "\\" );
				galakPos = Q_stristr( model, "galak_mech" );

				if ( modelEnd ) modelEnd = Q_stristr( modelEnd+1, "\\" );

				if ( galakPos && !(modelEnd && galakPos > modelEnd) )
				{
					galakPos[5] = '-';
				}
			}
		}

		// append it to the gameState string buffer
		cl.gameState.stringOffsets[ i ] = cl.gameState.dataCount;
		Com_Memcpy( cl.gameState.stringData + cl.gameState.dataCount, dup, len + 1 );
		cl.gameState.dataCount += len + 1;
	}

	if (cl_autolodscale && cl_autolodscale->integer)
	{
		if (index >= CS_PLAYERS &&
			index < CS_CHARSKINS)
		{ //this means that a client was updated in some way. Go through and count the clients.
			int clientCount = 0;
			i = CS_PLAYERS;

			while (i < CS_CHARSKINS)
			{
				s = cl.gameState.stringData + cl.gameState.stringOffsets[ i ];

				if (s && s[0])
				{
					clientCount++;
				}

				i++;
			}

			gCLTotalClientNum = clientCount;

#ifdef _DEBUG
			Com_DPrintf("%i clients\n", gCLTotalClientNum);
#endif

			CL_DoAutoLODScale();
		}
	}

	if ( index == CS_SYSTEMINFO ) {
		// parse serverId and other cvars
		CL_SystemInfoChanged();
	}

}


/*
===================
CL_GetServerCommand

Set up argc/argv for the given command
===================
*/
qboolean CL_GetServerCommand( int serverCommandNumber ) {
	char	*s;
	char	*cmd;
	static char bigConfigString[BIG_INFO_STRING];

	// if we have irretrievably lost a reliable command, drop the connection
	if ( serverCommandNumber <= clc.serverCommandSequence - MAX_RELIABLE_COMMANDS ) {
		// when a demo record was started after the client got a whole bunch of
		// reliable commands then the client never got those first reliable commands
		if ( clc.demoplaying )
			return qfalse;
		Com_Error( ERR_DROP, "CL_GetServerCommand: a reliable command was cycled out" );
		return qfalse;
	}

	if ( serverCommandNumber > clc.serverCommandSequence ) {
		Com_Error( ERR_DROP, "CL_GetServerCommand: requested a command not received" );
		return qfalse;
	}

	s = clc.serverCommands[ serverCommandNumber & ( MAX_RELIABLE_COMMANDS - 1 ) ];
	clc.lastExecutedServerCommand = serverCommandNumber;

	Com_DPrintf( "serverCommand: %i : %s\n", serverCommandNumber, s );

rescan:
	Cmd_TokenizeString( s );
	cmd = Cmd_Argv(0);

	if ( !strcmp( cmd, "disconnect" ) ) {
		Com_Error (ERR_SERVERDISCONNECT, "%s", SP_GetStringTextString("SVINGAME_SERVER_DISCONNECTED"));//"Server disconnected");
	}

	if ( !strcmp( cmd, "bcs0" ) ) {
		Com_sprintf( bigConfigString, BIG_INFO_STRING, "cs %s \"%s", Cmd_Argv(1), Cmd_Argv(2) );
		return qfalse;
	}

	if ( !strcmp( cmd, "bcs1" ) ) {
		s = Cmd_Argv(2);
		if( strlen(bigConfigString) + strlen(s) >= BIG_INFO_STRING ) {
			Com_Error( ERR_DROP, "bcs exceeded BIG_INFO_STRING" );
		}
		strcat( bigConfigString, s );
		return qfalse;
	}

	if ( !strcmp( cmd, "bcs2" ) ) {
		s = Cmd_Argv(2);
		if( strlen(bigConfigString) + strlen(s) + 1 >= BIG_INFO_STRING ) {
			Com_Error( ERR_DROP, "bcs exceeded BIG_INFO_STRING" );
		}
		strcat( bigConfigString, s );
		strcat( bigConfigString, "\"" );
		s = bigConfigString;
		goto rescan;
	}

	if ( !strcmp( cmd, "cs" ) ) {
		CL_ConfigstringModified();
		// reparse the string, because CL_ConfigstringModified may have done another Cmd_TokenizeString()
		Cmd_TokenizeString( s );
		return qtrue;
	}

	if ( !strcmp( cmd, "map_restart" ) ) {
		// clear notify lines and outgoing commands before passing
		// the restart to the cgame
		Con_ClearNotify();
		Com_Memset( cl.cmds, 0, sizeof( cl.cmds ) );
	
		if (cl_autoDemo->integer && !clc.demoplaying ) {
			demoAutoComplete();
			demoAutoRecord();
		}
		return qtrue;
	}

	// the clientLevelShot command is used during development
	// to generate 128*128 screenshots from the intermission
	// point of levels for the menu system to use
	// we pass it along to the cgame to make apropriate adjustments,
	// but we also clear the console and notify lines here
	if ( !strcmp( cmd, "clientLevelShot" ) ) {
		// don't do it if we aren't running the server locally,
		// otherwise malicious remote servers could overwrite
		// the existing thumbnails
		if ( !com_sv_running->integer ) {
			return qfalse;
		}
		// close the console
		Con_Close();
		// take a special screenshot next frame
		Cbuf_AddText( "wait ; wait ; wait ; wait ; screenshot levelshot\n" );
		return qtrue;
	}

	// we may want to put a "connect to other server" command here

	// cgame can now act on the command
	return qtrue;
}


/*
====================
CL_CM_LoadMap

Just adds default parameters that cgame doesn't need to know about
====================
*/
void CL_CM_LoadMap( const char *mapname ) {
	int		checksum;

	CM_LoadMap( mapname, qtrue, &checksum );
}

/*
====================
CL_ShutdonwCGame

====================
*/
void CL_ShutdownCGame( void ) {
	cls.keyCatchers &= ~KEYCATCH_CGAME;
	cls.cgameStarted = qfalse;
	if ( !cgvm ) {
		return;
	}
	VM_Call( cgvm, CG_SHUTDOWN );
	VM_Free( cgvm );
	cgvm = NULL;
	cls.fixes = MVFIX_NONE;
#ifdef _DONETPROFILE_
	ClReadProf().ShowTotals();
#endif

	if (cl_autoDemo->integer && !clc.demoplaying) {
		demoAutoComplete();
	}
}

// wp glowing workaround.. this keeps yourself from glowing like a candle when charging the blaster pistol on high svs.time
void MV_AddLightToScene(const vec3_t org, float intensity, float r, float g, float b) {
	if (!(cls.fixes & MVFIX_WPGLOWING) && cl.snap.valid) {
		if (cl.snap.ps.weaponstate == WEAPON_CHARGING_ALT || cl.snap.ps.weaponstate == WEAPON_CHARGING) {
			int	scl;
			int	si, sr, sg, sb;

			scl = cl.snap.ps.weaponChargeTime;
			sr = scl & 255;
			sg = (scl >> 8) & 255;
			sb = (scl >> 16) & 255;
			si = ((scl >> 24) & 255) * 4;

			if (intensity == si && sb == b && sg == g && sr == r) {
				return;
			}
		}
	}

	re.AddLightToScene(org, intensity, r, g, b);
}

/*
====================
CL_CgameSetVirtualScreen
====================
*/
void CL_CgameSetVirtualScreen(float w, float h) {
	cls.cgxadj = SCREEN_WIDTH / w;
	cls.cgyadj = SCREEN_HEIGHT / h;
}

/*
====================
CL_CgameSystemCalls

The cgame module is making a system call
====================
*/
extern bool RicksCrazyOnServer;
intptr_t CL_CgameSystemCalls(intptr_t *args) {
	// fix syscalls from 1.02 to match 1.04
	// this is a mess... can it be done better?
	if (VM_GetGameversion(cgvm) == VERSION_1_02) {
		if (args[0] == 52)
			args[0] = CG_ANYLANGUAGE_READCHARFROMSTRING;
		else if (args[0] <= 300 && args[0] >= 286)
			args[0] += 2;
		else if (args[0] == 285)
			args[0] = CG_G2_INITGHOUL2MODEL;
	}

	// set cgame ghoul2 context
	RicksCrazyOnServer = false;

	switch( args[0] ) {
	case CG_PRINT:
		Com_Printf( "%s", VMAS(1) );
		return 0;
	case CG_ERROR:
		Com_Error( ERR_DROP, "%s", VMAS(1) );
		return 0;
	case CG_MILLISECONDS:
		return Sys_Milliseconds();
	case CG_CVAR_REGISTER:
		Cvar_Register( VMAV(1, vmCvar_t), VMAS(2), VMAS(3), args[4] );
		return 0;
	case CG_CVAR_UPDATE:
		Cvar_Update( VMAV(1, vmCvar_t) );
		return 0;
	case CG_CVAR_SET:
		Cvar_Set2( VMAS(1), VMAS(2), qtrue, qtrue );
		return 0;
	case CG_CVAR_VARIABLESTRINGBUFFER:
		Cvar_VariableStringBuffer( VMAS(1), VMAP(2, char, args[3]), args[3], qtrue );
		return 0;
	case CG_ARGC:
		return Cmd_Argc();
	case CG_ARGV:
		Cmd_ArgvBuffer( args[1], VMAP(2, char, args[3]), args[3] );
		return 0;
	case CG_ARGS:
		Cmd_ArgsBuffer( VMAP(1, char, args[2]), args[2] );
		return 0;
	case CG_FS_FOPENFILE:
		return FS_FOpenFileByMode( VMAS(1), VMAV(2, int), (fsMode_t)args[3] );
	case CG_FS_READ:
		FS_Read2( VMAP(1, char, args[2]), args[2], args[3] );
		return 0;
	case CG_FS_WRITE:
		FS_Write( VMAP(1, const char, args[2]), args[2], args[3] );
		return 0;
	case CG_FS_FCLOSEFILE:
		FS_FCloseFile( args[1] );
		return 0;
	case CG_SENDCONSOLECOMMAND:
		Cbuf_AddText( VMAS(1) );
		return 0;
	case CG_ADDCOMMAND:
		CL_AddCgameCommand( VMAS(1) );
		return 0;
	case CG_REMOVECOMMAND:
		Cmd_RemoveCommand( VMAS(1) );
		return 0;
	case CG_SENDCLIENTCOMMAND:
		CL_AddReliableCommand( VMAS(1) );
		return 0;
	case CG_UPDATESCREEN:
		// this is used during lengthy level loading, so pump message loop
//		Com_EventLoop();	// FIXME: if a server restarts here, BAD THINGS HAPPEN!
// We can't call Com_EventLoop here, a restart will crash and this _does_ happen
// if there is a map change while we are downloading at pk3.
// ZOID
		SCR_UpdateScreen();
		return 0;
	case CG_CM_LOADMAP:
		CL_CM_LoadMap( VMAS(1) );
		return 0;
	case CG_CM_NUMINLINEMODELS:
		return CM_NumInlineModels();
	case CG_CM_INLINEMODEL:
		return CM_InlineModel( args[1] );
	case CG_CM_TEMPBOXMODEL:
		return CM_TempBoxModel( VMAP(1, const vec_t, 3), VMAP(2, const vec_t, 3), qfalse );
	case CG_CM_TEMPCAPSULEMODEL:
		return CM_TempBoxModel( VMAP(1, const vec_t, 3), VMAP(2, const vec_t, 3), qtrue );
	case CG_CM_POINTCONTENTS:
		return CM_PointContents( VMAP(1, const vec_t, 3), args[2] );
	case CG_CM_TRANSFORMEDPOINTCONTENTS:
		return CM_TransformedPointContents( VMAP(1, const vec_t, 3), args[2], VMAP(3, const vec_t, 3), VMAP(4, const vec_t, 3) );
	case CG_CM_BOXTRACE:
		CM_BoxTrace( VMAV(1, trace_t), VMAP(2, const vec_t, 3), VMAP(3, const vec_t, 3), VMAP(4, const vec_t, 3), VMAP(5, const vec_t, 3), args[6], args[7], qfalse );
		return 0;
	case CG_CM_CAPSULETRACE:
		CM_BoxTrace( VMAV(1, trace_t), VMAP(2, const vec_t, 3), VMAP(3, const vec_t, 3), VMAP(4, const vec_t, 3), VMAP(5, const vec_t, 3), args[6], args[7], qtrue );
		return 0;
	case CG_CM_TRANSFORMEDBOXTRACE:
		CM_TransformedBoxTrace( VMAV(1, trace_t), VMAP(2, const vec_t, 3), VMAP(3, const vec_t, 3), VMAP(4, const vec_t, 3), VMAP(5, const vec_t, 3), args[6], args[7], VMAP(8, const vec_t, 3), VMAP(9, const vec_t, 3), qfalse );
		return 0;
	case CG_CM_TRANSFORMEDCAPSULETRACE:
		CM_TransformedBoxTrace( VMAV(1, trace_t), VMAP(2, const vec_t, 3), VMAP(3, const vec_t, 3), VMAP(4, const vec_t, 3), VMAP(5, const vec_t, 3), args[6], args[7], VMAP(8, const vec_t, 3), VMAP(9, const vec_t, 3), qtrue );
		return 0;
	case CG_CM_MARKFRAGMENTS:
		return re.MarkFragments( args[1], VMAA(2, const vec3_t, args[1]), VMAP(3, const vec_t, 3), args[4], VMAA(5, vec3_t, args[4]), args[6], VMAA(7, markFragment_t, args[6]) );
	case CG_S_MUTESOUND:
		S_MuteSound( args[1], args[2] );
		return 0;
	case CG_S_STARTSOUND:
		S_StartSound( VMAP(1, const vec_t, 3), args[2], args[3], args[4] );
		return 0;
	case CG_S_STARTLOCALSOUND:
		S_StartLocalSound( args[1], args[2] );
		return 0;
	case CG_S_CLEARLOOPINGSOUNDS:
		S_ClearLoopingSounds((qboolean)!!args[1]);
		return 0;
	case CG_S_ADDLOOPINGSOUND:
		S_AddLoopingSound( args[1], VMAP(2, const vec_t, 3), VMAP(3, const vec_t, 3), args[4] );
		return 0;
	case CG_S_ADDREALLOOPINGSOUND:
		S_AddRealLoopingSound( args[1], VMAP(2, const vec_t, 3), VMAP(3, const vec_t, 3), args[4] );
		return 0;
	case CG_S_STOPLOOPINGSOUND:
		S_StopLoopingSound( args[1] );
		return 0;
	case CG_S_UPDATEENTITYPOSITION:
		S_UpdateEntityPosition( args[1], VMAP(2, const vec_t, 3) );
		return 0;
	case CG_S_RESPATIALIZE:
		S_Respatialize( args[1], VMAP(2, const vec_t, 3), VMAP(3, vec3_t, 3), args[4] );
		return 0;
	case CG_S_REGISTERSOUND:
		return S_RegisterSound( VMAS(1) );
	case CG_S_STARTBACKGROUNDTRACK:
		S_StartBackgroundTrack( VMAS(1), VMAS(2), (qboolean)!!args[3] );
		return 0;
	case CG_R_LOADWORLDMAP:
		re.LoadWorld( VMAS(1) );
		return 0;
	case CG_R_REGISTERMODEL:
		return re.RegisterModel( VMAS(1) );
	case CG_R_REGISTERSKIN:
		return re.RegisterSkin( VMAS(1) );
	case CG_R_REGISTERSHADER:
		return re.RegisterShader( VMAS(1) );
	case CG_R_REGISTERSHADERNOMIP:
		return re.RegisterShaderNoMip( VMAS(1) );
	case CG_R_REGISTERFONT:
		return re.RegisterFont( VMAS(1) );
	case CG_R_FONT_STRLENPIXELS:
		return re.Font_StrLenPixels( VMAS(1), args[2], VMF(3), cls.cgxadj, cls.cgyadj );
	case CG_R_FONT_STRLENCHARS:
		return re.Font_StrLenChars( VMAS(1) );
	case CG_R_FONT_STRHEIGHTPIXELS:
		return re.Font_HeightPixels( args[1], VMF(2), cls.cgxadj, cls.cgyadj );
	case CG_R_FONT_DRAWSTRING:
		re.Font_DrawString( args[1], args[2], VMAS(3), VMAP(4, const vec_t, 4), args[5], args[6], VMF(7), cls.cgxadj, cls.cgyadj );
		return 0;
	case CG_LANGUAGE_ISASIAN:
		return re.Language_IsAsian();
	case CG_LANGUAGE_USESSPACES:
		return re.Language_UsesSpaces();
	case CG_ANYLANGUAGE_READCHARFROMSTRING:
		return re.AnyLanguage_ReadCharFromString( VMAS(1), VMAV(2, int), VMAV(3, qboolean) );
	case CG_R_CLEARSCENE:
		re.ClearScene();
		return 0;
	case CG_R_ADDREFENTITYTOSCENE:
		re.AddRefEntityToScene(VMAV(1, const refEntity_t), qfalse);
		return 0;
	case CG_R_ADDPOLYTOSCENE:
		re.AddPolyToScene( args[1], args[2], VMAA(3, const polyVert_t, args[2]), 1 );
		return 0;
	case CG_R_ADDPOLYSTOSCENE:
		// args[2] * args[4] > INT_MAX
		if ( args[4] > 0 && args[2] > INT_MAX / args[4] ) {
			Com_Error( ERR_DROP, "CG_R_ADDPOLYSTOSCENE: too many vertices" );
		}
		re.AddPolyToScene( args[1], args[2], VMAA(3, const polyVert_t, args[2] * args[4]), args[4] );
		return 0;
	case CG_R_LIGHTFORPOINT:
		return re.LightForPoint( VMAP(1, vec_t, 3), VMAP(2, vec_t, 3), VMAP(3, vec_t, 3), VMAP(4, vec_t, 3) );
	case CG_R_ADDLIGHTTOSCENE:
		MV_AddLightToScene(VMAP(1, const vec_t, 3), VMF(2), VMF(3), VMF(4), VMF(5));
		return 0;
	case CG_R_ADDADDITIVELIGHTTOSCENE:
		re.AddAdditiveLightToScene( VMAP(1, const vec_t, 3), VMF(2), VMF(3), VMF(4), VMF(5) );
		return 0;
	case CG_R_RENDERSCENE:
		re.RenderScene( VMAV(1, const refdef_t) );
		return 0;
	case CG_R_SETCOLOR:
		re.SetColor( VMAP(1, vec_t, 4) );
		return 0;
	case CG_R_DRAWSTRETCHPIC:
		re.DrawStretchPic( VMF(1), VMF(2), VMF(3), VMF(4), VMF(5), VMF(6), VMF(7), VMF(8), args[9], cls.cgxadj, cls.cgyadj );
		return 0;
	case CG_R_MODELBOUNDS:
		re.ModelBounds( args[1], VMAP(2, vec_t, 3), VMAP(3, vec_t, 3) );
		return 0;
	case CG_R_LERPTAG:
		return re.LerpTag( VMAV(1, orientation_t), args[2], args[3], args[4], VMF(5), VMAS(6) );
	case CG_R_DRAWROTATEPIC:
		re.DrawRotatePic( VMF(1), VMF(2), VMF(3), VMF(4), VMF(5), VMF(6), VMF(7), VMF(8), VMF(9), args[10], cls.cgxadj, cls.cgyadj );
		return 0;
	case CG_R_DRAWROTATEPIC2:
		re.DrawRotatePic2( VMF(1), VMF(2), VMF(3), VMF(4), VMF(5), VMF(6), VMF(7), VMF(8), VMF(9), args[10], cls.cgxadj, cls.cgyadj );
		return 0;
	case CG_GETGLCONFIG:
		CL_GetVMGLConfig(VMAV(1, vmglconfig_t));
		return 0;
	case CG_GETGAMESTATE:
		CL_GetGameState( VMAV(1, gameState_t) );
		return 0;
	case CG_GETCURRENTSNAPSHOTNUMBER:
		CL_GetCurrentSnapshotNumber( VMAV(1, int), VMAV(2, int) );
		return 0;
	case CG_GETSNAPSHOT:
		return CL_GetSnapshot( args[1], VMAV(2, snapshot_t) );
	case CG_GETSERVERCOMMAND:
		return CL_GetServerCommand( args[1] );
	case CG_GETCURRENTCMDNUMBER:
		return CL_GetCurrentCmdNumber();
	case CG_GETUSERCMD:
		return CL_GetUserCmd( args[1], VMAV(2, usercmd_t) );
	case CG_SETUSERCMDVALUE:
		CL_SetUserCmdValue( args[1], VMF(2), args[3], args[4] );
		return 0;
	case CG_SETCLIENTFORCEANGLE:
		CL_SetClientForceAngle(args[1], VMAP(2, const vec_t, 3));
		return 0;
	case CG_SETCLIENTTURNEXTENT:
		CL_SetTurnExtents(VMF(1), VMF(2), args[3]);
		return 0;

	case CG_OPENUIMENU:
		VM_Call( uivm, UI_SET_ACTIVE_MENU, args[1] );
		return 0;

	case CG_MEMORY_REMAINING:
		return Hunk_MemoryRemaining();
  case CG_KEY_ISDOWN:
		return Key_IsDown( Key_GetProtocolKey15(VM_GetGameversion(cgvm), args[1]) ); // 1.02 keynums -> 1.04 keynums
  case CG_KEY_GETCATCHER:
		return Key_GetCatcher();
  case CG_KEY_SETCATCHER:
		Key_SetCatcher( args[1] );
	return 0;
  case CG_KEY_GETKEY:
	  return Key_GetProtocolKey(VM_GetGameversion(cgvm), Key_GetKey(VMAS(1))); // 1.04 keynums -> 1.02 keynums (return)



	case CGAME_MEMSET:
		Com_Memset( VMAP(1, char, args[3]), args[2], args[3] );
		return 0;
	case CGAME_MEMCPY:
		Com_Memcpy( VMAP(1, char, args[3]), VMAP(2, char, args[3]), args[3] );
		return 0;
	case CGAME_STRNCPY:
		return VM_strncpy( args[1], args[2], args[3] );
	case CGAME_SIN:
		return FloatAsInt( sinf( VMF(1) ) );
	case CGAME_COS:
		return FloatAsInt( cosf( VMF(1) ) );
	case CGAME_ATAN2:
		return FloatAsInt( atan2f( VMF(1), VMF(2) ) );
	case CGAME_SQRT:
		return FloatAsInt( VMF(1) < 0 ? 0 : sqrtf( VMF(1) ) );
	case CGAME_FLOOR:
		return FloatAsInt( floorf( VMF(1) ) );
	case CGAME_CEIL:
		return FloatAsInt( ceilf( VMF(1) ) );
	case CGAME_ACOS:
		return FloatAsInt( Q_acos( VMF(1) ) );

	case CG_PC_ADD_GLOBAL_DEFINE:
		return botlib_export->PC_AddGlobalDefine( VMAS(1) );
	case CG_PC_LOAD_SOURCE:
		return botlib_export->PC_LoadSourceHandle( VMAS(1) );
	case CG_PC_FREE_SOURCE:
		return botlib_export->PC_FreeSourceHandle( args[1] );
	case CG_PC_READ_TOKEN:
		return botlib_export->PC_ReadTokenHandle( args[1], VMAV(2, pc_token_t) );
	case CG_PC_SOURCE_FILE_AND_LINE:
		return botlib_export->PC_SourceFileAndLine( args[1], VMAP(2, char, MAX_QPATH), VMAV(3, int) );
	case CG_PC_LOAD_GLOBAL_DEFINES:
		return botlib_export->PC_LoadGlobalDefines ( VMAS(1) );
	case CG_PC_REMOVE_ALL_GLOBAL_DEFINES:
		botlib_export->PC_RemoveAllGlobalDefines ( );
		return 0;

	case CG_S_STOPBACKGROUNDTRACK:
		S_StopBackgroundTrack();
		return 0;

	case CG_REAL_TIME:
		return Com_RealTime( VMAV(1, qtime_t) );
	case CG_SNAPVECTOR:
		Sys_SnapVector(VMAP(1, vec_t, 3));
		return 0;

	case CG_CIN_PLAYCINEMATIC:
	  return CIN_PlayCinematic(VMAS(1), args[2], args[3], args[4], args[5], args[6]);

	case CG_CIN_STOPCINEMATIC:
	  return CIN_StopCinematic(args[1]);

	case CG_CIN_RUNCINEMATIC:
	  return CIN_RunCinematic(args[1]);

	case CG_CIN_DRAWCINEMATIC:
	  CIN_DrawCinematic(args[1]);
	  return 0;

	case CG_CIN_SETEXTENTS:
	  CIN_SetExtents(args[1], args[2], args[3], args[4], args[5]);
	  return 0;

	case CG_R_REMAP_SHADER:
		re.RemapShader( VMAS(1), VMAS(2), VMAS(3) );
		return 0;

	case CG_R_GET_LIGHT_STYLE:
		re.GetLightStyle(args[1], VMAP(2, byte, 4));
		return 0;

	case CG_R_SET_LIGHT_STYLE:
		re.SetLightStyle(args[1], args[2]);
		return 0;

	case CG_R_GET_BMODEL_VERTS:
		re.GetBModelVerts( args[1], VMAP(2, vec3_t, 4), VMAP(3, vec_t, 3) );
		return 0;

	case CG_FX_ADDLINE:
		FX_AddLine(NULL, VMAP(1, const vec_t, 3), VMAP(2, const vec_t, 3), VMF(3), VMF(4), VMF(5),
									VMF(6), VMF(7), VMF(8),
									VMAP(9, const vec_t, 3), VMAP(10, const vec_t, 3), VMF(11),
									args[12], args[13], args[14]);
		return 0;

/*
	case CG_LOADCAMERA:
		return loadCamera(VMA(1));

	case CG_STARTCAMERA:
		startCamera(args[1]);
		return 0;

	case CG_GETCAMERAINFO:
		return getCameraInfo(args[1], VMA(2), VMA(3));
*/
	case CG_GET_ENTITY_TOKEN:
		return re.GetEntityToken( VMAP(1, char, args[2]), args[2] );
	case CG_R_INPVS:
		return re.inPVS( VMAP(1, const vec_t, 3), VMAP(2, const vec_t, 3) );

#ifndef DEBUG_DISABLEFXCALLS
	case CG_FX_REGISTER_EFFECT:
		return FX_RegisterEffect(VMAS(1));

	case CG_FX_PLAY_SIMPLE_EFFECT:
		FX_PlaySimpleEffect(VMAS(1), VMAP(2, const vec_t, 3));
		return 0;

	case CG_FX_PLAY_EFFECT:
		FX_PlayEffect(VMAS(1), VMAP(2, const vec_t, 3), VMAP(3, const vec_t, 3));
		return 0;

	case CG_FX_PLAY_ENTITY_EFFECT:
		FX_PlayEntityEffect(VMAS(1), VMAP(2, const vec_t, 3), VMAP(3, const vec3_t, 3), args[4], args[5]);
		return 0;

	case CG_FX_PLAY_SIMPLE_EFFECT_ID:
		FX_PlaySimpleEffectID(args[1], VMAP(2, const vec_t, 3));
		return 0;

	case CG_FX_PLAY_EFFECT_ID:
		FX_PlayEffectID(args[1], VMAP(2, const vec_t, 3), VMAP(3, const vec_t, 3));
		return 0;

	case CG_FX_PLAY_ENTITY_EFFECT_ID:
		FX_PlayEntityEffectID(args[1], VMAP(2, const vec_t, 3), VMAP(3, const vec3_t, 3), args[4], args[5]);
		return 0;

	case CG_FX_PLAY_BOLTED_EFFECT_ID:
		FX_PlayBoltedEffectID(args[1], VMAV(2, sharedBoltInterface_t));
		return 0;

	case CG_FX_ADD_SCHEDULED_EFFECTS:
		FX_AddScheduledEffects();
		return 0;

	case CG_FX_INIT_SYSTEM:
		return FX_InitSystem();

	case CG_FX_FREE_SYSTEM:
		return FX_FreeSystem();

	case CG_FX_ADJUST_TIME:
		FX_AdjustTime_Pos(args[1], VMAP(2, const vec_t, 3), VMAP(3, const vec3_t, 3));
		return 0;

	case CG_FX_ADDPOLY:
		const addpolyArgStruct_t *p;

		p = VMAV(1, const addpolyArgStruct_t);

		if (p)
		{
			FX_AddPoly(NULL, p->p, p->ev, p->numVerts, p->vel, p->accel, p->alpha1, p->alpha2,
				p->alphaParm, p->rgb1, p->rgb2, p->rgbParm, p->rotationDelta, p->bounce, p->motionDelay,
				p->killTime, p->shader, p->flags);
		}
		return 0;

	case CG_FX_ADDBEZIER:
		const addbezierArgStruct_t *b;

		b = VMAV(1, const addbezierArgStruct_t);

		if (b)
		{
			FX_AddBezier(b->start, b->end, b->control1, b->control1Vel, b->control2, b->control2Vel,
				b->size1, b->size2, b->sizeParm, b->alpha1, b->alpha2, b->alphaParm, b->sRGB,
				b->eRGB, b->rgbParm, b->killTime, b->shader, b->flags);
		}
		return 0;

	case CG_FX_ADDPRIMITIVE:
		const effectTrailArgStruct_t *a;

		a = VMAV(1, const effectTrailArgStruct_t);

		if (a)
		{
			FX_FeedTrail(a);
		}
		return 0;

	case CG_FX_ADDSPRITE:
		const addspriteArgStruct_t *s;

		s = VMAV(1, const addspriteArgStruct_t);

		if (s)
		{
			static const vec3_t rgb = { 1, 1, 1 };
			//FX_AddSprite(NULL, s->origin, s->vel, s->accel, s->scale, s->dscale, s->sAlpha, s->eAlpha,
			//	s->rotation, s->bounce, s->life, s->shader, s->flags);
			FX_AddParticle(NULL, s->origin, s->vel, s->accel, s->scale, s->dscale, 0, s->sAlpha, s->eAlpha, 0,
				rgb, rgb, 0, s->rotation, 0, vec3_origin, vec3_origin, s->bounce, 0, 0, s->life,
				s->shader, s->flags);
		}
		return 0;
#else
	case CG_FX_REGISTER_EFFECT:
	case CG_FX_PLAY_SIMPLE_EFFECT:
	case CG_FX_PLAY_EFFECT:
	case CG_FX_PLAY_ENTITY_EFFECT:
	case CG_FX_PLAY_SIMPLE_EFFECT_ID:
	case CG_FX_PLAY_EFFECT_ID:
	case CG_FX_PLAY_ENTITY_EFFECT_ID:
	case CG_FX_PLAY_BOLTED_EFFECT_ID:
	case CG_FX_ADD_SCHEDULED_EFFECTS:
	case CG_FX_INIT_SYSTEM:
	case CG_FX_FREE_SYSTEM:
	case CG_FX_ADJUST_TIME:
	case CG_FX_ADDPOLY:
	case CG_FX_ADDBEZIER:
	case CG_FX_ADDPRIMITIVE:
	case CG_FX_ADDSPRITE:
		return 0;
#endif

	case CG_SP_PRINT:
		CL_SP_Print(args[1], args[2]);
		return 0;

	case CG_ROFF_CLEAN:
		return theROFFSystem.Clean(qtrue);

	case CG_ROFF_UPDATE_ENTITIES:
		theROFFSystem.UpdateEntities(qtrue);
		return 0;

	case CG_ROFF_CACHE:
		return theROFFSystem.Cache( VMAS(1), qtrue );

	case CG_ROFF_PLAY:
		return theROFFSystem.Play(args[1], args[2], (qboolean)!!args[3], qtrue );

	case CG_ROFF_PURGE_ENT:
		return theROFFSystem.PurgeEnt( args[1], qtrue );

/*
Ghoul2 Insert Start
*/

	case CG_G2_LISTSURFACES:
		// G2API_ListSurfaces( (CGhoul2Info *) args[1] );
		G2API_ListSurfaces((g2handle_t)args[1], args[2]);
		return 0;

	case CG_G2_LISTBONES:
		// G2API_ListBones( (CGhoul2Info *) args[1], args[2]);
		G2API_ListBones((g2handle_t)args[1], args[2], args[3]);
		return 0;

	case CG_G2_HAVEWEGHOULMODELS:
		return G2API_HaveWeGhoul2Models((g2handle_t)args[1]);

	case CG_G2_SETMODELS:
		// G2API_SetGhoul2ModelIndexes((g2handle_t)args[1], (qhandle_t *)VMA(2), (qhandle_t *)VMA(3));
		return 0;

	case CG_G2_GETBOLT:
		return G2API_GetBoltMatrix((g2handle_t)args[1], args[2], args[3], VMAV(4, mdxaBone_t), VMAP(5, const vec_t, 3), VMAP(6, const vec_t, 3), args[7], VMAA(8, const qhandle_t, G2API_GetMaxModelIndex(false) + 1), VMAP(9, const vec_t, 3));

	case CG_G2_GETBOLT_NOREC:
		gG2_GBMNoReconstruct = qtrue;
		return G2API_GetBoltMatrix((g2handle_t)args[1], args[2], args[3], VMAV(4, mdxaBone_t), VMAP(5, const vec_t, 3), VMAP(6, const vec_t, 3), args[7], VMAA(8, const qhandle_t, G2API_GetMaxModelIndex(false) + 1), VMAP(9, const vec_t, 3));

	case CG_G2_GETBOLT_NOREC_NOROT:
		gG2_GBMNoReconstruct = qtrue;
		gG2_GBMUseSPMethod = qtrue;
		return G2API_GetBoltMatrix((g2handle_t)args[1], args[2], args[3], VMAV(4, mdxaBone_t), VMAP(5, const vec_t, 3), VMAP(6, const vec_t, 3), args[7], VMAA(8, const qhandle_t, G2API_GetMaxModelIndex(false) + 1), VMAP(9, const vec_t, 3));

	case CG_G2_INITGHOUL2MODEL:
		return	G2API_InitGhoul2Model(VMAV(1, g2handle_t), VMAS(2), args[3], (qhandle_t) args[4],
			(qhandle_t) args[5], args[6], args[7]);

	case CG_G2_COLLISIONDETECT:
#ifdef G2_COLLISION_ENABLED
		G2API_CollisionDetect(VMAA(1, CollisionRecord_t, MAX_G2_COLLISIONS),
								   (g2handle_t)args[2],
								   VMAP(3, const vec_t, 3),
								   VMAP(4, const vec_t, 3),
								   args[5],
								   args[6],
								   VMAP(7, const vec_t, 3),
								   VMAP(8, const vec_t, 3),
								   VMAP(9, const vec_t, 3),
								   G2VertSpaceClient,
								   args[10],
								   args[11],
								   VMF(12) );
#endif
		return 0;

	case CG_G2_ANGLEOVERRIDE:
		return G2API_SetBoneAngles((g2handle_t)args[1], args[2], VMAS(3), VMAP(4, vec_t, 3), args[5],
							 (const Eorientations) args[6], (const Eorientations) args[7], (const Eorientations) args[8],
							 VMAA(9, qhandle_t, args[2] + 1), args[10], args[11] );

	case CG_G2_CLEANMODELS:
		G2API_CleanGhoul2Models(VMAV(1, g2handle_t));
		return 0;

	case CG_G2_PLAYANIM:
		return G2API_SetBoneAnim((g2handle_t)args[1], args[2], VMAS(3), args[4], args[5],
								args[6], VMF(7), args[8], VMF(9), args[10]);
	case CG_G2_GETGLANAME:
		//	return (int)G2API_GetGLAName(*((CGhoul2Info_v *)VMA(1)), args[2]);
		{
			char *local;
			local = G2API_GetGLAName((g2handle_t)args[1], args[2]);
			if (local)
			{
				char *point = VMAP(3, char, strlen(local) + 1);
				strcpy(point, local);
			}
		}
		return 0;

	case CG_G2_COPYGHOUL2INSTANCE:
		return G2API_CopyGhoul2Instance((g2handle_t)args[1], (g2handle_t)args[2], args[3]);

	case CG_G2_COPYSPECIFICGHOUL2MODEL:
		G2API_CopySpecificG2Model((g2handle_t)args[1], args[2], (g2handle_t)args[3], args[4]);
		return 0;

	case CG_G2_DUPLICATEGHOUL2INSTANCE:
		G2API_DuplicateGhoul2Instance((g2handle_t)args[1], VMAV(2, g2handle_t));
		return 0;

	case CG_G2_HASGHOUL2MODELONINDEX:
		return G2API_HasGhoul2ModelOnIndex(VMAV(1, const g2handle_t), args[2]);

	case CG_G2_REMOVEGHOUL2MODEL:
		return G2API_RemoveGhoul2Model(VMAV(1, g2handle_t), args[2]);

	case CG_G2_ADDBOLT:
		return G2API_AddBolt((g2handle_t)args[1], args[2], VMAS(3));

//	case CG_G2_REMOVEBOLT:
//		return G2API_RemoveBolt(*((CGhoul2Info_v *)VMA(1)), args[2]);

	case CG_G2_SETBOLTON:
		G2API_SetBoltInfo((g2handle_t)args[1], args[2], args[3]);
		return 0;
/*
Ghoul2 Insert End
*/
	case CG_G2_GIVEMEVECTORFROMMATRIX:
		G2API_GiveMeVectorFromMatrix(VMAV(1, const mdxaBone_t), (Eorientations)(args[2]), VMAP(3, vec_t, 3));
		return 0;

	case CG_G2_SETROOTSURFACE:
		return G2API_SetRootSurface((g2handle_t)args[1], args[2], VMAS(3));

	case CG_G2_SETSURFACEONOFF:
		return G2API_SetSurfaceOnOff((g2handle_t)args[1], VMAS(2), /*(const int)VMA(3)*/args[3]);

	case CG_G2_SETNEWORIGIN:
		return G2API_SetNewOrigin((g2handle_t)args[1], /*(const int)VMA(2)*/args[2]);

	case CG_SP_GETSTRINGTEXTSTRING:
//	case CG_SP_GETSTRINGTEXT:
		const char* text;

//		if (args[0] == CG_SP_GETSTRINGTEXT)
//		{
//			text = SP_GetStringText( args[1] );
//		}
//		else
		{
			text = SP_GetStringTextString( VMAS(1) );
		}

		if ( text[0] )
		{
			Q_strncpyz( VMAP(2, char, args[3]), text, args[3] );
			return qtrue;
		}
		else
		{
			Q_strncpyz( VMAP(2, char, args[3]), "??", args[3] );
			return qfalse;
		}
		break;

	case CG_SP_REGISTER:
		return !!SP_Register(VMAS(1), SP_REGISTER_CLIENT);

	case CG_SET_SHARED_BUFFER:
		cl.mSharedMemory = VMAP(1, char, MAX_CG_SHARED_BUFFER_SIZE);
		return 0;

	case MVAPI_GET_VERSION:
		return (int)VM_GetGameversion(cgvm);
	}

	if (VM_MVAPILevel(cgvm) >= 1) {
		switch (args[0]) {
		case MVAPI_CONTROL_FIXES:
			return (int)CL_MVAPI_ControlFixes(args[1]);
		}
	}

	if (VM_MVAPILevel(cgvm) >= 3) {
		switch (args[0]) {
		case CG_MVAPI_R_ADDREFENTITYTOSCENE2:
			re.AddRefEntityToScene(VMAV(1, const refEntity_t), qtrue);
			return 0;
		case CG_MVAPI_SETVIRTUALSCREEN:
			CL_CgameSetVirtualScreen(VMF(1), VMF(2));
			return 0;
		case MVAPI_FS_FLOCK:
			return (int)FS_FLock(args[1], (flockCmd_t)args[2], (qboolean)!!args[3]);
		case MVAPI_SET_VERSION:
			VM_SetGameversion( cgvm, (mvversion_t)args[1] );
			return 0;
		}
	}

	Com_Error( ERR_DROP, "Bad cgame system trap: %lli", (long long int)args[0] );
	return 0;
}


/*
====================
CL_InitCGame

Should only be called by CL_StartHunkUsers
====================
*/
void CL_InitCGame( void ) {
	const char			*info;
	const char			*mapname;
	int					t1, t2;
	vmInterpret_t		interpret;
	int apireq;

	t1 = Sys_Milliseconds();

	// put away the console
	Con_Close();

	// find the current mapname
	info = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SERVERINFO ];
	mapname = Info_ValueForKey( info, "mapname" );
	Com_sprintf( cl.mapname, sizeof( cl.mapname ), "maps/%s.bsp", mapname );

	// load the dll or bytecode
	if ( cl_connectedToPureServer != 0 ) {
		// if sv_pure is set we only allow qvms to be loaded
		interpret = VMI_COMPILED;
	}
	else {
		interpret = (vmInterpret_t)(int)Cvar_VariableValue( "vm_cgame" );
	}
	cgvm = VM_Create( "cgame", qfalse, CL_CgameSystemCalls, interpret );
	if ( !cgvm ) {
		Com_Error( ERR_DROP, "VM_Create on cgame failed" );
	}
	cls.state = CA_LOADING;

	// init for this gamestate
	// use the lastExecutedServerCommand instead of the serverCommandSequence
	// otherwise server commands sent just before a gamestate are dropped
	apireq = VM_Call(cgvm, CG_INIT, clc.serverMessageSequence, clc.lastExecutedServerCommand,
		clc.clientNum, 0, 0, 0, 0, 0, 0, 0, 0, MIN(mv_apienabled->integer, MV_APILEVEL));
	if (apireq > mv_apienabled->integer) {
		apireq = mv_apienabled->integer;
	}
	VM_SetMVAPILevel(cgvm, apireq);
	Com_DPrintf("CGameVM uses MVAPI level %i.\n", apireq);

	if (apireq >= 1) {
		VM_Call(cgvm, MVAPI_AFTER_INIT);
	}
	
	demoAutoInit();

	// we will send a usercmd this frame, which
	// will cause the server to send us the first snapshot
	cls.state = CA_PRIMED;

	t2 = Sys_Milliseconds();

	Com_Printf( "CL_InitCGame: %5.2f seconds\n", (t2-t1)/1000.0 );

	// have the renderer touch all its images, so they are present
	// on the card even if the driver does deferred loading
	re.EndRegistration();

	// make sure everything is paged in
//	if (!Sys_LowPhysicalMemory())
	{
		Com_TouchMemory();
	}

	// clear anything that got printed
	Con_ClearNotify ();
#ifdef _DONETPROFILE_
	ClReadProf().Reset();
#endif
}


/*
====================
CL_GameCommand

See if the current console command is claimed by the cgame
====================
*/
qboolean CL_GameCommand( void ) {
	if ( !cgvm ) {
		return qfalse;
	}

	return (qboolean)!!VM_Call( cgvm, CG_CONSOLE_COMMAND );
}



/*
=====================
CL_CGameRendering
=====================
*/
void CL_CGameRendering( stereoFrame_t stereo ) {
	VM_Call( cgvm, CG_DRAW_ACTIVE_FRAME, cl.serverTime, stereo, clc.demoplaying );
	VM_Debug( 0 );
}


/*
=================
CL_AdjustTimeDelta

Adjust the clients view of server time.

We attempt to have cl.serverTime exactly equal the server's view
of time plus the timeNudge, but with variable latencies over
the internet it will often need to drift a bit to match conditions.

Our ideal time would be to have the adjusted time approach, but not pass,
the very latest snapshot.

Adjustments are only made when a new snapshot arrives with a rational
latency, which keeps the adjustment process framerate independent and
prevents massive overadjustment during times of significant packet loss
or bursted delayed packets.
=================
*/

#define	RESET_TIME	500

void CL_AdjustTimeDelta( void ) {
	int		resetTime;
	int		newDelta;
	int		deltaDelta;

	cl.newSnapshots = qfalse;

	// the delta never drifts when replaying a demo
	if ( clc.demoplaying ) {
		return;
	}

	// if the current time is WAY off, just correct to the current value
	if ( com_sv_running->integer ) {
		resetTime = 100;
	} else {
		resetTime = RESET_TIME;
	}

	newDelta = cl.snap.serverTime - cls.realtime;
	deltaDelta = abs( newDelta - cl.serverTimeDelta );

	if ( deltaDelta > RESET_TIME ) {
		cl.serverTimeDelta = newDelta;
		cl.oldServerTime = cl.snap.serverTime;	// FIXME: is this a problem for cgame?
		cl.serverTime = cl.snap.serverTime;
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<RESET> " );
		}
	} else if ( deltaDelta > 100 ) {
		// fast adjust, cut the difference in half
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<FAST> " );
		}
		cl.serverTimeDelta = ( cl.serverTimeDelta + newDelta ) >> 1;
	} else {
		// slow drift adjust, only move 1 or 2 msec

		// if any of the frames between this and the previous snapshot
		// had to be extrapolated, nudge our sense of time back a little
		// the granularity of +1 / -2 is too high for timescale modified frametimes
		if ( com_timescale->value == 0 || com_timescale->value == 1 ) {
			if ( cl.extrapolatedSnapshot ) {
				cl.extrapolatedSnapshot = qfalse;
				cl.serverTimeDelta -= 2;
			} else {
				// otherwise, move our sense of time forward to minimize total latency
				cl.serverTimeDelta++;
			}
		}
	}

	if ( cl_showTimeDelta->integer ) {
		Com_Printf( "%i ", cl.serverTimeDelta );
	}
}


/*
==================
CL_FirstSnapshot
==================
*/
extern void RE_RegisterMedia_LevelLoadEnd(void);
void CL_FirstSnapshot( void ) {
	// ignore snapshots that don't have entities
	if ( cl.snap.snapFlags & SNAPFLAG_NOT_ACTIVE ) {
		return;
	}

	RE_RegisterMedia_LevelLoadEnd();

	cls.state = CA_ACTIVE;

	WIN_SetTaskbarState(TBS_NOTIFY, 0, 0);

	// set the timedelta so we are exactly on this first frame
	cl.serverTimeDelta = cl.snap.serverTime - cls.realtime;
	cl.oldServerTime = cl.snap.serverTime;

	clc.timeDemoBaseTime = cl.snap.serverTime;

	// if this is the first frame of active play,
	// execute the contents of activeAction now
	// this is to allow scripting a timedemo to start right
	// after loading
	if ( cl_activeAction->string[0] ) {
		Cbuf_AddText( cl_activeAction->string );
		Cbuf_AddText( "\n" );
		Cvar_Set( "activeAction", "" );
	}
}

/*
==================
CL_SetCGameTime
==================
*/
void CL_SetCGameTime( void ) {
	// getting a valid frame message ends the connection process
	if ( cls.state != CA_ACTIVE ) {
		if ( cls.state != CA_PRIMED ) {
			return;
		}
		if ( clc.demoplaying ) {
			// we shouldn't get the first snapshot on the same frame
			// as the gamestate, because it causes a bad time skip
			if ( !clc.firstDemoFrameSkipped ) {
				clc.firstDemoFrameSkipped = qtrue;
				return;
			}
			CL_ReadDemoMessage();
		}
		if ( cl.newSnapshots ) {
			cl.newSnapshots = qfalse;
			CL_FirstSnapshot();
		}
		if ( cls.state != CA_ACTIVE ) {
			return;
		}
	}

	// if we have gotten to this point, cl.snap is guaranteed to be valid
	if ( !cl.snap.valid ) {
		Com_Error( ERR_DROP, "CL_SetCGameTime: !cl.snap.valid" );
	}

	// allow pause in single player
	if ( sv_paused->integer && cl_paused->integer && com_sv_running->integer ) {
		// paused
		return;
	}

	if ( cl.snap.serverTime < cl.oldFrameServerTime ) {
		Com_Error( ERR_DROP, "cl.snap.serverTime < cl.oldFrameServerTime" );
	}
	cl.oldFrameServerTime = cl.snap.serverTime;


	// get our current view of time

	if ( clc.demoplaying && cl_freezeDemo->integer ) {
		// cl_freezeDemo is used to lock a demo in place for single frame advances

	} else {
		// cl_timeNudge is a user adjustable cvar that allows more
		// or less latency to be added in the interest of better
		// smoothness or better responsiveness.
		int tn;

		tn = cl_timeNudge->integer;
#ifdef _DEBUG
		if (tn<-900) {
			tn = -900;
		} else if (tn>900) {
			tn = 900;
		}
#else
		if (tn<-30) {
			tn = -30;
		} else if (tn>30) {
			tn = 30;
		}
#endif

		cl.serverTime = cls.realtime + cl.serverTimeDelta - tn;

		// guarantee that time will never flow backwards, even if
		// serverTimeDelta made an adjustment or cl_timeNudge was changed
		if ( cl.serverTime < cl.oldServerTime ) {
			cl.serverTime = cl.oldServerTime;
		}
		cl.oldServerTime = cl.serverTime;

		// note if we are almost past the latest frame (without timeNudge),
		// so we will try and adjust back a bit when the next snapshot arrives
		if ( cls.realtime + cl.serverTimeDelta >= cl.snap.serverTime - 5 ) {
			cl.extrapolatedSnapshot = qtrue;
		}
	}

	// if we have gotten new snapshots, drift serverTimeDelta
	// don't do this every frame, or a period of packet loss would
	// make a huge adjustment
	if ( cl.newSnapshots ) {
		CL_AdjustTimeDelta();
	}

	if ( !clc.demoplaying ) {
		return;
	}

	// if we are playing a demo back, we can just keep reading
	// messages from the demo file until the cgame definately
	// has valid snapshots to interpolate between

	// a timedemo will always use a deterministic set of time samples
	// no matter what speed machine it is run on,
	// while a normal demo may have different time samples
	// each time it is played back
	if ( cl_timedemo->integer ) {
		if (!clc.timeDemoStart) {
			clc.timeDemoStart = Sys_Milliseconds();
		}
		clc.timeDemoFrames++;
		cl.serverTime = clc.timeDemoBaseTime + clc.timeDemoFrames * 50;
	}

	while ( cl.serverTime >= cl.snap.serverTime ) {
		// feed another messag, which should change
		// the contents of cl.snap
		CL_ReadDemoMessage();
		if ( cls.state != CA_ACTIVE ) {
			return;		// end of demo
		}
	}

}

/*
====================
CL_MVAPI_ControlFixes

disable / enable toggleable fixes from the cgvm
====================
*/
qboolean CL_MVAPI_ControlFixes(int fixes) {
	int mask = 0;

	switch (VM_MVAPILevel(cgvm)) {
	case 3:
	case 2:
	case 1:
		mask |= MVFIX_WPGLOWING;
	}

	cls.fixes = fixes & mask;

	return qfalse;
}

