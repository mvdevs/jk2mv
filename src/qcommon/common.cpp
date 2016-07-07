// common.c -- misc functions used in client and server

#include <math.h>
#include <setjmp.h>
#ifndef WIN32
# include <fenv.h>
#endif

#include "../qcommon/q_shared.h"
#include "../sys/sys_local.h"
#include "qcommon.h"
#include "../client/client.h"
#include "strip.h"
#include "mv_setup.h"
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define MAX_NUM_ARGVS	50

#define MIN_DEDICATED_COMHUNKMEGS 16//1 //We need more than 1 for VMs when we are also using temporary hunk memory for bot nav functions
#define MIN_COMHUNKMEGS 128 //NOTE: Was 56
#define DEF_COMHUNKMEGS "128"
//#define DEF_COMZONEMEGS "16"

int		com_argc;
char	*com_argv[MAX_NUM_ARGVS+1];

////////////////////////////////////////////////
//
#ifdef TAGDEF	// itu?
#undef TAGDEF
#endif
#define TAGDEF(blah) #blah
const static char *psTagStrings[TAG_COUNT+1]=	// +1 because TAG_COUNT will itself become a string here. Oh well.
{
	#include "../qcommon/tags.h"
};
//
////////////////////////////////////////////////

static void Z_Details_f(void);

jmp_buf abortframe;		// an ERR_DROP occured, exit the entire frame

FILE *debuglogfile;
static fileHandle_t logfile;
fileHandle_t	com_journalFile;			// events are written here
fileHandle_t	com_journalDataFile;		// config files are written here

cvar_t	*com_viewlog;
cvar_t	*com_speeds;
cvar_t	*com_developer;
cvar_t	*com_dedicated;
cvar_t	*com_timescale;
cvar_t	*com_fixedtime;
cvar_t	*com_dropsim;		// 0.0 to 1.0, simulated packet drops
cvar_t	*com_journal;
cvar_t	*com_timedemo;
cvar_t	*com_sv_running;
cvar_t	*com_cl_running;
cvar_t	*com_logfile;		// 1 = buffer log, 2 = flush after each print
cvar_t	*com_showtrace;
cvar_t	*com_version;
cvar_t	*com_blood;
cvar_t	*com_buildScript;	// for automated data building scripts
cvar_t	*com_introPlayed;
cvar_t	*cl_paused;
cvar_t	*sv_paused;
cvar_t	*com_cameraMode;
cvar_t	*com_busyWait;

cvar_t	*mv_apienabled;

// com_speeds times
int		time_game;
int		time_frontend;		// renderer frontend time
int		time_backend;		// renderer backend time

int			com_frameTime;
int			com_frameMsec;
int			com_frameNumber;

qboolean	com_errorEntered;
qboolean	com_fullyInitialized;
qboolean	com_demoplaying;

char	com_errorMessage[MAXPRINTMSG];

void Com_WriteConfig_f( void );
void CIN_CloseAllVideos();

//============================================================================

static char	*rd_buffer;
static int	rd_buffersize;
static void	(*rd_flush)( char *buffer );

void Com_BeginRedirect (char *buffer, int buffersize, void (*flush)( char *) )
{
	if (!buffer || !buffersize || !flush)
		return;
	rd_buffer = buffer;
	rd_buffersize = buffersize;
	rd_flush = flush;

	*rd_buffer = 0;
}

void Com_EndRedirect (void)
{
	if ( rd_flush ) {
		rd_flush(rd_buffer);
	}

	rd_buffer = NULL;
	rd_buffersize = 0;
	rd_flush = NULL;
}

static void Com_Puts_Ext( qboolean extendedColors, char *msg )
{
	if ( rd_buffer ) {
		if ((strlen (msg) + strlen(rd_buffer)) > (rd_buffersize - 1)) {
			rd_flush(rd_buffer);
			*rd_buffer = 0;
		}
		Q_strcat(rd_buffer, rd_buffersize, msg);
		rd_flush(rd_buffer);
		*rd_buffer = 0;
		return;
	}

	// echo to console if we're not a dedicated server
	if ( com_dedicated && !com_dedicated->integer ) {
		CL_ConsolePrint( msg, extendedColors );
	}

	// echo to dedicated console and early console
	Sys_Print( msg, extendedColors );

	// logfile
	if ( com_logfile && com_logfile->integer ) {
		if ( !logfile ) {
			struct tm *newtime;
			time_t aclock;

			time( &aclock );
			newtime = localtime( &aclock );

			logfile = FS_FOpenFileWrite( "qconsole.log" );
			Com_Printf( "logfile opened on %s\n", asctime( newtime ) );
			if ( com_logfile->integer > 1 ) {
				// force it to not buffer so we get valid
				// data even if we are crashing
				FS_ForceFlush(logfile);
			}
		}
		if ( logfile && FS_Initialized()) {
			FS_Write(msg, (int)strlen(msg), logfile);
		}
	}

#if defined(_WIN32) && defined(_DEBUG)
	if ( *msg )
	{
		OutputDebugStringA ( Q_CleanStr(msg, (qboolean)MV_USE102COLOR) );
		OutputDebugStringA ("\n");
	}
#endif
}

/*
=============
Com_Printf

Both client and server can use this, and it will output
to the apropriate place.

A raw string should NEVER be passed as fmt, because of "%f" type crashers.
=============
*/
void QDECL Com_Printf( const char *fmt, ... )
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start (argptr,fmt);
	vsprintf (msg,fmt,argptr);
	va_end (argptr);

	Com_Puts_Ext( qfalse, msg );
}

void QDECL Com_Printf_Ext( qboolean extendedColors, const char *fmt, ... )
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start (argptr,fmt);
	vsprintf (msg,fmt,argptr);
	va_end (argptr);

	Com_Puts_Ext( extendedColors, msg);
}


/*
================
Com_DPrintf

A Com_Printf that only shows up if the "developer" cvar is set
================
*/
void QDECL Com_DPrintf( const char *fmt, ...) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	if ( !com_developer || !com_developer->integer ) {
		return;			// don't confuse non-developers with techie stuff...
	}

	va_start (argptr,fmt);
	vsprintf (msg,fmt,argptr);
	va_end (argptr);

	Com_Puts_Ext (qfalse, msg);
}

// Outputs to the VC / Windows Debug window (only in debug compile)
void QDECL Com_OPrintf( const char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start (argptr,fmt);
	vsprintf (msg,fmt,argptr);
	va_end (argptr);
#ifdef WIN32
	OutputDebugStringA(msg);
#else
	printf("%s", msg);
#endif
}

/*
=============
Com_Error

Both client and server can use this, and it will
do the apropriate things.
=============
*/
Q_NORETURN void QDECL Com_Error( int code, const char *fmt, ... ) {
	va_list		argptr;
	static int	lastErrorTime;
	static int	errorCount;
	int			currentTime;

	// when we are running automated scripts, make sure we
	// know if anything failed
	if ( com_buildScript && com_buildScript->integer ) {
		code = ERR_FATAL;
	}

	// make sure we can get at our local stuff
	FS_PureServerSetLoadedPaks( "", "" );

	// if we are getting a solid stream of ERR_DROP, do an ERR_FATAL
	currentTime = Sys_Milliseconds();
	if ( currentTime - lastErrorTime < 100 ) {
		if ( ++errorCount > 3 ) {
			code = ERR_FATAL;
		}
	} else {
		errorCount = 0;
	}
	lastErrorTime = currentTime;

	if ( com_errorEntered ) {
		Sys_Error( "recursive error after: %s", com_errorMessage );
	}
	com_errorEntered = qtrue;

	va_start (argptr,fmt);
	vsprintf (com_errorMessage,fmt,argptr);
	va_end (argptr);

	if ( code != ERR_DISCONNECT ) {
		Cvar_Get("com_errorMessage", "", CVAR_ROM);	//give com_errorMessage a default so it won't come back to life after a resetDefaults
		Cvar_Set("com_errorMessage", com_errorMessage);
	}

	if ( code == ERR_SERVERDISCONNECT ) {
		VM_Forced_Unload_Start();
		CL_Disconnect( qtrue );
		CL_FlushMemory( );
		VM_Forced_Unload_Done();
		com_errorEntered = qfalse;
		longjmp(abortframe, -1);
	} else if ( code == ERR_DROP || code == ERR_DISCONNECT ) {
		Com_Printf ("********************\nERROR: %s\n********************\n", com_errorMessage);
		VM_Forced_Unload_Start();
		SV_Shutdown (va("Server crashed: %s\n",  com_errorMessage));
		CL_Disconnect( qtrue );
		CL_FlushMemory( );
		VM_Forced_Unload_Done();
		com_errorEntered = qfalse;
		longjmp(abortframe, -1);
	} else if ( code == ERR_NEED_CD ) {
		VM_Forced_Unload_Start();
		SV_Shutdown( "Server didn't have CD\n" );
		if ( com_cl_running && com_cl_running->integer ) {
			CL_Disconnect( qtrue );
			CL_FlushMemory( );
			VM_Forced_Unload_Done();
			com_errorEntered = qfalse;
		} else {
			Com_Printf("Server didn't have CD\n" );
			VM_Forced_Unload_Done();
		}

		com_errorEntered = qfalse;
		longjmp(abortframe, -1);
	} else {
		VM_Forced_Unload_Start();
		CL_Shutdown ();
		SV_Shutdown (va("Server fatal crashed: %s\n", com_errorMessage));
		VM_Forced_Unload_Done();
	}

	Com_Shutdown ();

	Sys_Error ("%s", com_errorMessage);
}


/*
=============
Com_Quit_f

Both client and server can use this, and it will
do the apropriate things.
=============
*/
void Com_Quit_f( void ) {
	// don't try to shutdown if we are in a recursive error
	if ( !com_errorEntered ) {
		VM_Forced_Unload_Start();
		SV_Shutdown ("Server quit\n");
		CL_Shutdown ();
		VM_Forced_Unload_Done();
		Com_Shutdown ();
		FS_Shutdown(qtrue);
	}
	Sys_Quit ();
}



/*
============================================================================

COMMAND LINE FUNCTIONS

+ characters seperate the commandLine string into multiple console
command lines.

All of these are valid:

quake3 +set test blah +map test
quake3 set test blah+map test
quake3 set test blah + map test

============================================================================
*/

#define	MAX_CONSOLE_LINES	32
int		com_numConsoleLines;
char	*com_consoleLines[MAX_CONSOLE_LINES];

/*
==================
Com_ParseCommandLine

Break it up into multiple console lines
==================
*/
void Com_ParseCommandLine( char *commandLine ) {
	com_consoleLines[0] = commandLine;
	com_numConsoleLines = 1;

	while ( *commandLine ) {
		// look for a + seperating character
		// if commandLine came from a file, we might have real line seperators
		if ( *commandLine == '+' || *commandLine == '\n' ) {
			if ( com_numConsoleLines == MAX_CONSOLE_LINES ) {
				return;
			}
			com_consoleLines[com_numConsoleLines] = commandLine + 1;
			com_numConsoleLines++;
			*commandLine = 0;
		}
		commandLine++;
	}
}


/*
===================
Com_SafeMode

Check for "safe" on the command line, which will
skip loading of jk2mvconfig.cfg
===================
*/
qboolean Com_SafeMode( void ) {
	int		i;

	for ( i = 0 ; i < com_numConsoleLines ; i++ ) {
		Cmd_TokenizeString( com_consoleLines[i] );
		if ( !Q_stricmp( Cmd_Argv(0), "safe" )
			|| !Q_stricmp( Cmd_Argv(0), "cvar_restart" ) ) {
			com_consoleLines[i][0] = 0;
			return qtrue;
		}
	}
	return qfalse;
}


/*
===============
Com_StartupVariable

Searches for command line parameters that are set commands.
If match is not NULL, only that cvar will be looked for.
That is necessary because cddir and basedir need to be set
before the filesystem is started, but all other sets shouls
be after execing the config and default.
===============
*/
void Com_StartupVariable( const char *match ) {
	int		i;
	char	*s;
	cvar_t	*cv;

	for (i=0 ; i < com_numConsoleLines ; i++) {
		Cmd_TokenizeString( com_consoleLines[i] );
		if ( strcmp( Cmd_Argv(0), "set" ) ) {
			continue;
		}

		s = Cmd_Argv(1);
		if ( !match || !strcmp( s, match ) ) {
			Cvar_Set( s, Cmd_Argv(2) );
			cv = Cvar_Get( s, "", 0 );
			cv->flags |= CVAR_USER_CREATED;
//			com_consoleLines[i] = 0;
		}
	}
}


/*
=================
Com_AddStartupCommands

Adds command line parameters as script statements
Commands are seperated by + signs

Returns qtrue if any late commands were added, which
will keep the demoloop from immediately starting
=================
*/
qboolean Com_AddStartupCommands( void ) {
	int		i;
	qboolean	added;

	added = qfalse;
	// quote every token, so args with semicolons can work
	for (i=0 ; i < com_numConsoleLines ; i++) {
		if ( !com_consoleLines[i] || !com_consoleLines[i][0] ) {
			continue;
		}

		// set commands won't override menu startup
		if ( Q_stricmpn( com_consoleLines[i], "set", 3 ) ) {
			added = qtrue;
		}
		Cbuf_AddText( com_consoleLines[i] );
		Cbuf_AddText( "\n" );
	}

	return added;
}


//============================================================================

void Info_Print( const char *s ) {
	char	key[512];
	char	value[512];
	char	*o;
	int		l;

	if (*s == '\\')
		s++;
	while (*s)
	{
		o = key;
		while (*s && *s != '\\')
			*o++ = *s++;

		l = (int)(o - key);
		if (l < 20)
		{
			Com_Memset (o, ' ', 20-l);
			key[20] = 0;
		}
		else
			*o = 0;
		Com_Printf ("%s", key);

		if (!*s)
		{
			Com_Printf ("MISSING VALUE\n");
			return;
		}

		o = value;
		s++;
		while (*s && *s != '\\')
			*o++ = *s++;
		*o = 0;

		if (*s)
			s++;
		Com_Printf ("%s\n", value);
	}
}

/*
============
Com_StringContains
============
*/
char *Com_StringContains(char *str1, char *str2, int casesensitive) {
	size_t len, i, j;

	len = strlen(str1) - strlen(str2);
	for (i = 0; i <= len; i++, str1++) {
		for (j = 0; str2[j]; j++) {
			if (casesensitive) {
				if (str1[j] != str2[j]) {
					break;
				}
			}
			else {
				if (toupper(str1[j]) != toupper(str2[j])) {
					break;
				}
			}
		}
		if (!str2[j]) {
			return str1;
		}
	}
	return NULL;
}

/*
============
Com_Filter
============
*/
int Com_Filter(char *filter, char *name, int casesensitive)
{
	char buf[MAX_TOKEN_CHARS];
	char *ptr;
	int i, found;

	while(*filter) {
		if (*filter == '*') {
			filter++;
			for (i = 0; *filter; i++) {
				if (*filter == '*' || *filter == '?') break;
				buf[i] = *filter;
				filter++;
			}
			buf[i] = '\0';
			if (strlen(buf)) {
				ptr = Com_StringContains(name, buf, casesensitive);
				if (!ptr) return qfalse;
				name = ptr + strlen(buf);
			}
		}
		else if (*filter == '?') {
			filter++;
			name++;
		}
		else if (*filter == '[' && *(filter+1) == '[') {
			filter++;
		}
		else if (*filter == '[') {
			filter++;
			found = qfalse;
			while(*filter && !found) {
				if (*filter == ']' && *(filter+1) != ']') break;
				if (*(filter+1) == '-' && *(filter+2) && (*(filter+2) != ']' || *(filter+3) == ']')) {
					if (casesensitive) {
						if (*name >= *filter && *name <= *(filter+2)) found = qtrue;
					}
					else {
						if (toupper(*name) >= toupper(*filter) &&
							toupper(*name) <= toupper(*(filter+2))) found = qtrue;
					}
					filter += 3;
				}
				else {
					if (casesensitive) {
						if (*filter == *name) found = qtrue;
					}
					else {
						if (toupper(*filter) == toupper(*name)) found = qtrue;
					}
					filter++;
				}
			}
			if (!found) return qfalse;
			while(*filter) {
				if (*filter == ']' && *(filter+1) != ']') break;
				filter++;
			}
			filter++;
			name++;
		}
		else {
			if (casesensitive) {
				if (*filter != *name) return qfalse;
			}
			else {
				if (toupper(*filter) != toupper(*name)) return qfalse;
			}
			filter++;
			name++;
		}
	}
	return qtrue;
}

/*
============
Com_FilterPath
============
*/
int Com_FilterPath(char *filter, char *name, int casesensitive)
{
	int i;
	char new_filter[MAX_QPATH];
	char new_name[MAX_QPATH];

	for (i = 0; i < MAX_QPATH-1 && filter[i]; i++) {
		if ( filter[i] == '\\' || filter[i] == ':' ) {
			new_filter[i] = '/';
		}
		else {
			new_filter[i] = filter[i];
		}
	}
	new_filter[i] = '\0';
	for (i = 0; i < MAX_QPATH-1 && name[i]; i++) {
		if ( name[i] == '\\' || name[i] == ':' ) {
			new_name[i] = '/';
		}
		else {
			new_name[i] = name[i];
		}
	}
	new_name[i] = '\0';
	return Com_Filter(new_filter, new_name, casesensitive);
}

/*
============
Com_HashKey
============
*/
int Com_HashKey(char *string, int maxlen) {
	int hash, i;

	hash = 0;
	for (i = 0; i < maxlen && string[i] != '\0'; i++) {
		hash += string[i] * (119 + i);
	}
	hash = (hash ^ (hash >> 10) ^ (hash >> 20));
	return hash;
}

/*
================
Com_RealTime
================
*/
int Com_RealTime(qtime_t *qtime) {
	time_t t;
	struct tm *tms;

	t = time(NULL);
	if (!qtime)
		return 0;

	tms = localtime(&t);
	if (tms) {
		qtime->tm_sec = tms->tm_sec;
		qtime->tm_min = tms->tm_min;
		qtime->tm_hour = tms->tm_hour;
		qtime->tm_mday = tms->tm_mday;
		qtime->tm_mon = tms->tm_mon;
		qtime->tm_year = tms->tm_year;
		qtime->tm_wday = tms->tm_wday;
		qtime->tm_yday = tms->tm_yday;
		qtime->tm_isdst = tms->tm_isdst;
	}

    // there is no other way then casting
    // since mods could use the unix time
	return (int)t;
}



// This handles zone memory allocation.
// It is a wrapper around malloc with a tag id and a magic number at the start

#define ZONE_MAGIC			0x21436587

typedef struct zoneHeader_s
{
		int					iMagic;
		memtag_t			eTag;
		int					iSize;
struct	zoneHeader_s		*pNext;
struct	zoneHeader_s		*pPrev;
} zoneHeader_t;

typedef struct
{
	int iMagic;

} zoneTail_t;

static inline zoneTail_t *ZoneTailFromHeader(zoneHeader_t *pHeader)
{
  return (zoneTail_t*) ((char*)(pHeader + 1) + PAD(pHeader->iSize, Q_ALIGNOF(zoneTail_t)));
}

#ifdef DETAILED_ZONE_DEBUG_CODE
map <void*,int> mapAllocatedZones;
#endif


typedef struct zoneStats_s
{
	int		iCount;
	int		iCurrent;
	int		iPeak;

	// I'm keeping these updated on the fly, since it's quicker for cache-pool
	//	purposes rather than recalculating each time...
	//
	int		iSizesPerTag [TAG_COUNT];
	int		iCountsPerTag[TAG_COUNT];

} zoneStats_t;

typedef struct zone_s
{
	zoneStats_t				Stats;
	zoneHeader_t			Header;
} zone_t;

cvar_t	*com_validateZone;

zone_t	TheZone = {};




// Scans through the linked list of mallocs and makes sure no data has been overwritten

void Z_Validate(void)
{
	if(!com_validateZone || !com_validateZone->integer)
	{
		return;
	}

	zoneHeader_t *pMemory = TheZone.Header.pNext;
	while (pMemory)
	{
		#ifdef DETAILED_ZONE_DEBUG_CODE
		// this won't happen here, but wtf?
		int& iAllocCount = mapAllocatedZones[pMemory];
		if (iAllocCount <= 0)
		{
			Com_Error(ERR_FATAL, "Z_Validate(): Bad block allocation count!");
			return;
		}
		#endif

		if(pMemory->iMagic != ZONE_MAGIC)
		{
			Com_Error(ERR_FATAL, "Z_Validate(): Corrupt zone header!");
			return;
		}

		if (ZoneTailFromHeader(pMemory)->iMagic != ZONE_MAGIC)
		{
			Com_Error(ERR_FATAL, "Z_Validate(): Corrupt zone tail!");
			return;
		}

		pMemory = pMemory->pNext;
	}
}



// static mem blocks to reduce a lot of small zone overhead
//
typedef struct
{
	zoneHeader_t	Header;
//	byte mem[0];
	zoneTail_t		Tail;
} StaticZeroMem_t;

typedef struct
{
	zoneHeader_t	Header;
	byte mem[2];
	zoneTail_t		Tail;
} StaticMem_t;

StaticZeroMem_t gZeroMalloc  =
	{ {ZONE_MAGIC, TAG_STATIC,0,NULL,NULL},{ZONE_MAGIC}};
StaticMem_t gEmptyString =
	{ {ZONE_MAGIC, TAG_STATIC,2,NULL,NULL}, {'\0', '\0'}, {ZONE_MAGIC}};
StaticMem_t gNumberString[] = {
	{ {ZONE_MAGIC, TAG_STATIC,2,NULL,NULL}, {'0', '\0'}, {ZONE_MAGIC}},
	{ {ZONE_MAGIC, TAG_STATIC,2,NULL,NULL}, {'1', '\0'}, {ZONE_MAGIC}},
	{ {ZONE_MAGIC, TAG_STATIC,2,NULL,NULL}, {'2', '\0'}, {ZONE_MAGIC}},
	{ {ZONE_MAGIC, TAG_STATIC,2,NULL,NULL}, {'3', '\0'}, {ZONE_MAGIC}},
	{ {ZONE_MAGIC, TAG_STATIC,2,NULL,NULL}, {'4', '\0'}, {ZONE_MAGIC}},
	{ {ZONE_MAGIC, TAG_STATIC,2,NULL,NULL}, {'5', '\0'}, {ZONE_MAGIC}},
	{ {ZONE_MAGIC, TAG_STATIC,2,NULL,NULL}, {'6', '\0'}, {ZONE_MAGIC}},
	{ {ZONE_MAGIC, TAG_STATIC,2,NULL,NULL}, {'7', '\0'}, {ZONE_MAGIC}},
	{ {ZONE_MAGIC, TAG_STATIC,2,NULL,NULL}, {'8', '\0'}, {ZONE_MAGIC}},
	{ {ZONE_MAGIC, TAG_STATIC,2,NULL,NULL}, {'9', '\0'}, {ZONE_MAGIC}},
};

qboolean gbMemFreeupOccured = qfalse;
void *Z_Malloc(int iSize, memtag_t eTag, qboolean bZeroit /* = qfalse */)
{
	gbMemFreeupOccured = qfalse;

	if (iSize == 0)
	{
		zoneHeader_t *pMemory = (zoneHeader_t *) &gZeroMalloc;
		return &pMemory[1];
	}

	int iRealSize = sizeof(zoneHeader_t) + PAD(iSize, Q_ALIGNOF(zoneTail_t)) + sizeof(zoneTail_t);

	// Allocate a chunk...
	//
	zoneHeader_t *pMemory = NULL;
	while (pMemory == NULL)
	{
		if (bZeroit) {
			pMemory = (zoneHeader_t *) calloc ( iRealSize, 1 );
		} else {
			pMemory = (zoneHeader_t *) malloc ( iRealSize );
		}
		if (!pMemory)
		{
			// new bit, if we fail to malloc memory, try dumping some of the cached stuff that's non-vital and try again...
			//

			// ditch the BSP cache...
			//
			extern qboolean CM_DeleteCachedMap(qboolean bGuaranteedOkToDelete);
			if (CM_DeleteCachedMap(qfalse))
			{
				gbMemFreeupOccured = qtrue;
				continue;		// we've just ditched a whole load of memory, so try again with the malloc
			}


			// ditch any sounds not used on this level...
			//
			extern qboolean SND_RegisterAudio_LevelLoadEnd(qboolean bDeleteEverythingNotUsedThisLevel);
			if (SND_RegisterAudio_LevelLoadEnd(qtrue))
			{
				gbMemFreeupOccured = qtrue;
				continue;		// we've dropped at least one sound, so try again with the malloc
			}

#ifndef DEDICATED
			// ditch any image_t's (and associated GL memory) not used on this level...
			//
			extern qboolean RE_RegisterImages_LevelLoadEnd(void);
			if (RE_RegisterImages_LevelLoadEnd())
			{
				gbMemFreeupOccured = qtrue;
				continue;		// we've dropped at least one image, so try again with the malloc
			}
#endif

			// ditch the model-binaries cache...  (must be getting desperate here!)
			//
			extern qboolean RE_RegisterModels_LevelLoadEnd(qboolean bDeleteEverythingNotUsedThisLevel);
			if (RE_RegisterModels_LevelLoadEnd(qtrue))
			{
				gbMemFreeupOccured = qtrue;
				continue;
			}

			// as a last panic measure, dump all the audio memory, but not if we're in the audio loader
			//	(which is annoying, but I'm not sure how to ensure we're not dumping any memory needed by the sound
			//	currently being loaded if that was the case)...
			//
			// note that this keeps querying until it's freed up as many bytes as the requested size, but freeing
			//	several small blocks might not mean that one larger one is satisfiable after freeup, however that'll
			//	just make it go round again and try for freeing up another bunch of blocks until the total is satisfied
			//	again (though this will have freed twice the requested amount in that case), so it'll either work
			//	eventually or not free up enough and drop through to the final ERR_DROP. No worries...
			//
			extern qboolean gbInsideLoadSound;
			extern int SND_FreeOldestSound();
			if (!gbInsideLoadSound)
			{
				int iBytesFreed = SND_FreeOldestSound();
				if (iBytesFreed)
				{
					int iTheseBytesFreed = 0;
					while ( (iTheseBytesFreed = SND_FreeOldestSound()) != 0)
					{
						iBytesFreed += iTheseBytesFreed;
						if (iBytesFreed >= iRealSize)
							break;	// early opt-out since we've managed to recover enough (mem-contiguity issues aside)
					}
					gbMemFreeupOccured = qtrue;
					continue;
				}
			}

			// sigh, dunno what else to try, I guess we'll have to give up and report this as an out-of-mem error...
			//
			// findlabel:  "recovermem"

			Com_Printf(S_COLOR_RED"Z_Malloc(): Failed to alloc %d bytes (TAG_%s) !!!!!\n", iSize, psTagStrings[eTag]);
			Z_Details_f();
			Com_Error(ERR_FATAL,"(Repeat): Z_Malloc(): Failed to alloc %d bytes (TAG_%s) !!!!!\n", iSize, psTagStrings[eTag]);
			return NULL;
		}
	}




	// Link in
	pMemory->iMagic	= ZONE_MAGIC;
	pMemory->eTag	= eTag;
	pMemory->iSize	= iSize;
	pMemory->pNext  = TheZone.Header.pNext;
	TheZone.Header.pNext = pMemory;
	if (pMemory->pNext)
	{
		pMemory->pNext->pPrev = pMemory;
	}
	pMemory->pPrev = &TheZone.Header;
	//
	// add tail...
	//
	ZoneTailFromHeader(pMemory)->iMagic = ZONE_MAGIC;

	// Update stats...
	//
	TheZone.Stats.iCurrent += iSize;
	TheZone.Stats.iCount++;
	TheZone.Stats.iSizesPerTag	[eTag] += iSize;
	TheZone.Stats.iCountsPerTag	[eTag]++;

	if (TheZone.Stats.iCurrent > TheZone.Stats.iPeak)
	{
		TheZone.Stats.iPeak	= TheZone.Stats.iCurrent;
	}

#ifdef DETAILED_ZONE_DEBUG_CODE
	mapAllocatedZones[pMemory]++;
#endif

	Z_Validate();	// check for corruption

	void *pvReturnMem = &pMemory[1];
	return pvReturnMem;
}


static void Zone_FreeBlock(zoneHeader_t *pMemory)
{
	if (pMemory->eTag != TAG_STATIC)	// belt and braces, should never hit this though
	{
		// Update stats...
		//
		TheZone.Stats.iCount--;
		TheZone.Stats.iCurrent -= pMemory->iSize;
		TheZone.Stats.iSizesPerTag	[pMemory->eTag] -= pMemory->iSize;
		TheZone.Stats.iCountsPerTag	[pMemory->eTag]--;

		// Sanity checks...
		//
		assert(pMemory->pPrev->pNext == pMemory);
		assert(!pMemory->pNext || (pMemory->pNext->pPrev == pMemory));

		// Unlink and free...
		//
		pMemory->pPrev->pNext = pMemory->pNext;
		if(pMemory->pNext)
		{
			pMemory->pNext->pPrev = pMemory->pPrev;
		}
		free (pMemory);


		#ifdef DETAILED_ZONE_DEBUG_CODE
		// this has already been checked for in execution order, but wtf?
		int& iAllocCount = mapAllocatedZones[pMemory];
		if (iAllocCount == 0)
		{
			Com_Error(ERR_FATAL, "Zone_FreeBlock(): Double-freeing block!");
			return;
		}
		iAllocCount--;
		#endif
	}
}

// stats-query function to ask how big a malloc is...
//
int Z_Size(void *pvAddress)
{
	zoneHeader_t *pMemory = ((zoneHeader_t *)pvAddress) - 1;

	if (pMemory->eTag == TAG_STATIC)
	{
		return 0;	// kind of
	}

	if (pMemory->iMagic != ZONE_MAGIC)
	{
		Com_Error(ERR_FATAL, "Z_Size(): Not a valid zone header!");
		return 0;	// won't get here
	}

	return pMemory->iSize;
}


// Frees a block of memory...
//
void Z_Free(void *pvAddress)
{
	if (pvAddress == NULL)	// I've put this in as a safety measure because of some bits of #ifdef BSPC stuff	-Ste.
	{
		//Com_Error(ERR_FATAL, "Z_Free(): NULL arg");
		return;
	}

	zoneHeader_t *pMemory = ((zoneHeader_t *)pvAddress) - 1;

	if (pMemory->eTag == TAG_STATIC)
	{
		return;
	}

	#ifdef DETAILED_ZONE_DEBUG_CODE
	//
	// check this error *before* barfing on bad magics...
	//
	int& iAllocCount = mapAllocatedZones[pMemory];
	if (iAllocCount <= 0)
	{
		Com_Error(ERR_FATAL, "Z_Free(): Block already-freed, or not allocated through Z_Malloc!");
		return;
	}
	#endif

	if (pMemory->iMagic != ZONE_MAGIC)
	{
		Com_Error(ERR_FATAL, "Z_Free(): Corrupt zone header!");
		return;
	}
	if (ZoneTailFromHeader(pMemory)->iMagic != ZONE_MAGIC)
	{
		Com_Error(ERR_FATAL, "Z_Free(): Corrupt zone tail!");
		return;
	}

	Zone_FreeBlock(pMemory);
}


int Z_MemSize(memtag_t eTag)
{
	return TheZone.Stats.iSizesPerTag[eTag];
}

// Frees all blocks with the specified tag...
//
void Z_TagFree(memtag_t eTag)
{
//#ifdef _DEBUG
//	int iZoneBlocks = TheZone.Stats.iCount;
//#endif

	zoneHeader_t *pMemory = TheZone.Header.pNext;
	while (pMemory)
	{
		zoneHeader_t *pNext = pMemory->pNext;
		if ( (eTag == TAG_ALL) || (pMemory->eTag == eTag))
		{
			Zone_FreeBlock(pMemory);
		}
		pMemory = pNext;
	}

// these stupid pragmas don't work here???!?!?!
//
//#ifdef _DEBUG
//#pragma warning( disable : 4189)
//	int iBlocksFreed = iZoneBlocks - TheZone.Stats.iCount;
//#pragma warning( default : 4189)
//#endif
}


void *S_Malloc( int iSize ) {
	return Z_Malloc( iSize, TAG_SMALL );
}


#ifdef _DEBUG
static void Z_MemRecoverTest_f(void)
{
	// needs to be in _DEBUG only, not good for final game!
	// fixme: findmeste: Remove this sometime
	//
	int iTotalMalloc = 0;
	while (1)
	{
		int iThisMalloc = 5* (1024 * 1024);
		Z_Malloc(iThisMalloc, TAG_SPECIAL_MEM_TEST, qfalse);	// and lose, just to consume memory
		iTotalMalloc += iThisMalloc;

		if (gbMemFreeupOccured)
			break;
	}

	Z_TagFree(TAG_SPECIAL_MEM_TEST);
}
#endif



// Gives a summary of the zone memory usage

static void Z_Stats_f(void)
{
	Com_Printf("\nThe zone is using %d bytes (%.2fMB) in %d memory blocks\n",
								  TheZone.Stats.iCurrent,
									        (float)TheZone.Stats.iCurrent / 1024.0f / 1024.0f,
													  TheZone.Stats.iCount
				);

	Com_Printf("The zone peaked at %d bytes (%.2fMB)\n",
									TheZone.Stats.iPeak,
									         (float)TheZone.Stats.iPeak / 1024.0f / 1024.0f
				);
}

// Gives a detailed breakdown of the memory blocks in the zone

static void Z_Details_f(void)
{
	Com_Printf("---------------------------------------------------------------------------\n");
	Com_Printf("%20s %9s\n","Zone Tag","Bytes");
	Com_Printf("%20s %9s\n","--------","-----");
	for (int i=0; i<TAG_COUNT; i++)
	{
		int iThisCount = TheZone.Stats.iCountsPerTag[i];
		int iThisSize  = TheZone.Stats.iSizesPerTag	[i];

		if (iThisCount)
		{
			// can you believe that using %2.2f as a format specifier doesn't bloody work?
			//	It ignores the left-hand specifier. Sigh, now I've got to do shit like this...
			//
			float	fSize		= (float)(iThisSize) / 1024.0f / 1024.0f;
			int		iSize		= fSize;
			int		iRemainder 	= 100.0f * (fSize - floor(fSize));
			Com_Printf("%20s %9d (%2d.%02dMB) in %6d blocks (%9d average)\n",
					    psTagStrings[i],
							  iThisSize,
								iSize,iRemainder,
								           iThisCount, iThisSize / iThisCount
					   );
		}
	}
	Com_Printf("---------------------------------------------------------------------------\n");

	Z_Stats_f();
}

// Shuts down the zone memory system and frees up all memory
void Com_ShutdownZoneMemory(void)
{
	Com_Printf("Shutting down zone memory .....\n");

	Cmd_RemoveCommand("zone_stats");
	Cmd_RemoveCommand("zone_details");

	if(TheZone.Stats.iCount)
	{
		Com_Printf("Automatically freeing %d blocks making up %d bytes\n", TheZone.Stats.iCount, TheZone.Stats.iCurrent);
		Z_TagFree(TAG_ALL);

		assert(!TheZone.Stats.iCount);
		assert(!TheZone.Stats.iCurrent);
	}
}

// Initialises the zone memory system

void Com_InitZoneMemory( void )
{
	Com_Printf("Initialising zone memory .....\n");

	memset(&TheZone, 0, sizeof(TheZone));
	TheZone.Header.iMagic = ZONE_MAGIC;

#ifdef _DEBUG
	com_validateZone = Cvar_Get("com_validateZone", "1", 0);
#else
	com_validateZone = Cvar_Get("com_validateZone", "0", 0);
#endif

	Cmd_AddCommand("zone_stats", Z_Stats_f);
	Cmd_AddCommand("zone_details", Z_Details_f);

#ifdef _DEBUG
	Cmd_AddCommand("zone_memrecovertest", Z_MemRecoverTest_f);
#endif
}




/*
========================
CopyString

 NOTE:	never write over the memory CopyString returns because
		memory from a memstatic_t might be returned
========================
*/
char *CopyString( const char *in ) {
	char	*out;

	if (!in[0]) {
		return ((char *)&gEmptyString) + sizeof(zoneHeader_t);
	}
	else if (!in[1]) {
		if (in[0] >= '0' && in[0] <= '9') {
			return ((char *)&gNumberString[in[0]-'0']) + sizeof(zoneHeader_t);
		}
	}

	out = (char *)S_Malloc((int)strlen(in) + 1);
	strcpy (out, in);
	return out;
}







/*
==============================================================================

Goals:
	reproducable without history effects -- no out of memory errors on weird map to map changes
	allow restarting of the client without fragmentation
	minimize total pages in use at run time
	minimize total pages needed during load time

  Single block of memory with stack allocators coming from both ends towards the middle.

  One side is designated the temporary memory allocator.

  Temporary memory can be allocated and freed in any order.

  A highwater mark is kept of the most in use at any time.

  When there is no temporary memory allocated, the permanent and temp sides
  can be switched, allowing the already touched temp memory to be used for
  permanent storage.

  Temp memory must never be allocated on two ends at once, or fragmentation
  could occur.

  If we have any in-use temp memory, additional temp allocations must come from
  that side.

  If not, we can choose to make either side the new temp side and push future
  permanent allocations to the other side.  Permanent allocations should be
  kept on the side that has the current greatest wasted highwater mark.

==============================================================================
*/


#define	HUNK_MAGIC	0x89537892
#define	HUNK_FREE_MAGIC	0x89537893

typedef struct {
	int		magic;
	int		size;
} hunkHeader_t;

typedef struct {
	int		mark;
	int		permanent;
	int		temp;
	int		tempHighwater;
} hunkUsed_t;

typedef struct hunkblock_s {
	int size;
	byte printed;
	struct hunkblock_s *next;
	char *label;
	char *file;
	int line;
} hunkblock_t;

static	hunkblock_t *hunkblocks;

static	hunkUsed_t	hunk_low, hunk_high;
static	hunkUsed_t	*hunk_permanent, *hunk_temp;

static	byte	*s_hunkData = NULL;
static	int		s_hunkTotal;

static	int		s_zoneTotal;

/*
=================
Com_Meminfo_f
=================
*/
void Com_Meminfo_f( void ) {
//	memblock_t	*block;
//	int			zoneBytes, zoneBlocks;
//	int			smallZoneBytes, smallZoneBlocks;
	int			botlibBytes, rendererBytes;
	int			unused;

//	zoneBytes = 0;
	botlibBytes = 0;
	rendererBytes = 0;
//	zoneBlocks = 0;
/*	for (block = mainzone->blocklist.next ; ; block = block->next) {
		if ( Cmd_Argc() != 1 ) {
			Com_Printf ("block:%p    size:%7i    tag:%3i\n",
				block, block->size, block->tag);
		}
		if ( block->tag ) {
			zoneBytes += block->size;
			zoneBlocks++;
			if ( block->tag == TAG_BOTLIB ) {
				botlibBytes += block->size;
			} else if ( block->tag == TAG_RENDERER ) {
				rendererBytes += block->size;
			}
		}

		if (block->next == &mainzone->blocklist) {
			break;			// all blocks have been hit
		}
		if ( (byte *)block + block->size != (byte *)block->next) {
			Com_Printf ("ERROR: block size does not touch the next block\n");
		}
		if ( block->next->prev != block) {
			Com_Printf ("ERROR: next block doesn't have proper back link\n");
		}
		if ( !block->tag && !block->next->tag ) {
			Com_Printf ("ERROR: two consecutive free blocks\n");
		}
	}
*/
/*
	smallZoneBytes = 0;
	smallZoneBlocks = 0;
	for (block = smallzone->blocklist.next ; ; block = block->next) {
		if ( block->tag ) {
			smallZoneBytes += block->size;
			smallZoneBlocks++;
		}

		if (block->next == &smallzone->blocklist) {
			break;			// all blocks have been hit
		}
	}
*/
	Z_Details_f();

	Com_Printf( "%8i bytes total hunk\n", s_hunkTotal );
	Com_Printf( "%8i bytes total zone\n", s_zoneTotal );
	Com_Printf( "\n" );
	Com_Printf( "%8i low mark\n", hunk_low.mark );
	Com_Printf( "%8i low permanent\n", hunk_low.permanent );
	if ( hunk_low.temp != hunk_low.permanent ) {
		Com_Printf( "%8i low temp\n", hunk_low.temp );
	}
	Com_Printf( "%8i low tempHighwater\n", hunk_low.tempHighwater );
	Com_Printf( "\n" );
	Com_Printf( "%8i high mark\n", hunk_high.mark );
	Com_Printf( "%8i high permanent\n", hunk_high.permanent );
	if ( hunk_high.temp != hunk_high.permanent ) {
		Com_Printf( "%8i high temp\n", hunk_high.temp );
	}
	Com_Printf( "%8i high tempHighwater\n", hunk_high.tempHighwater );
	Com_Printf( "\n" );
	Com_Printf( "%8i total hunk in use\n", hunk_low.permanent + hunk_high.permanent );
	unused = 0;
	if ( hunk_low.tempHighwater > hunk_low.permanent ) {
		unused += hunk_low.tempHighwater - hunk_low.permanent;
	}
	if ( hunk_high.tempHighwater > hunk_high.permanent ) {
		unused += hunk_high.tempHighwater - hunk_high.permanent;
	}
	Com_Printf( "%8i unused highwater\n", unused );
	Com_Printf( "\n" );
//	Com_Printf( "%8i bytes in %i zone blocks\n", zoneBytes, zoneBlocks	);
	Com_Printf( "		%8i bytes in dynamic botlib\n", botlibBytes );
	Com_Printf( "		%8i bytes in dynamic renderer\n", rendererBytes );
//	Com_Printf( "		%8i bytes in dynamic other\n", zoneBytes - ( botlibBytes + rendererBytes ) );
//	Com_Printf( "		%8i bytes in small Zone memory\n", smallZoneBytes );
}

/*
===============
Com_TouchMemory

Touch all known used data to make sure it is paged in
===============
*/
void Com_TouchMemory( void ) {
	int		start, end;
	int		i, j;
	int		sum;
//	memblock_t	*block;


	Z_Validate();

	start = Sys_Milliseconds();

	sum = 0;

	zoneHeader_t *pMemory = TheZone.Header.pNext;
	while (pMemory)
	{
		byte *pMem = (byte *) &pMemory[1];
		j = pMemory->iSize >> 2;
		for (i=0; i<j; i+=64){
			sum += ((int*)pMem)[i];
		}

		pMemory = pMemory->pNext;
	}


	j = hunk_low.permanent >> 2;
	for ( i = 0 ; i < j ; i+=64 ) {			// only need to touch each page
		sum += ((int *)s_hunkData)[i];
	}

	i = ( s_hunkTotal - hunk_high.permanent ) >> 2;
	j = hunk_high.permanent >> 2;
	for (  ; i < j ; i+=64 ) {			// only need to touch each page
		sum += ((int *)s_hunkData)[i];
	}

/*	for (block = mainzone->blocklist.next ; ; block = block->next) {
		if ( block->tag ) {
			j = block->size >> 2;
			for ( i = 0 ; i < j ; i+=64 ) {				// only need to touch each page
				sum += ((int *)block)[i];
			}
		}
		if ( block->next == &mainzone->blocklist ) {
			break;			// all blocks have been hit
		}
	}
*/
	end = Sys_Milliseconds();

	Com_Printf( "Com_TouchMemory: %i msec\n", end - start );
}




/*
=================
Hunk_Log
=================
*/
void Hunk_Log( void) {
	hunkblock_t	*block;
	char		buf[4096];
	int size, numBlocks;

	if (!logfile || !FS_Initialized())
		return;
	size = 0;
	numBlocks = 0;
	Com_sprintf(buf, sizeof(buf), "\r\n================\r\nHunk log\r\n================\r\n");
	FS_Write(buf, (int)strlen(buf), logfile);
	for (block = hunkblocks ; block; block = block->next) {
#ifdef HUNK_DEBUG
 		Com_sprintf(buf, sizeof(buf), "size\t%8d\t%s\tline\t%d\t(%s)\r\n", block->size, block->file, block->line, block->label);
		FS_Write(buf, (int)strlen(buf), logfile);
#endif
		size += block->size;
		numBlocks++;
	}
	Com_sprintf(buf, sizeof(buf), "%d Hunk memory\r\n", size);
	FS_Write(buf, (int)strlen(buf), logfile);
	Com_sprintf(buf, sizeof(buf), "%d hunk blocks\r\n", numBlocks);
	FS_Write(buf, (int)strlen(buf), logfile);
}

/*
=================
Hunk_SmallLog
=================
*/
void Hunk_SmallLog( void) {
	hunkblock_t	*block, *block2;
	char		buf[4096];
	int size, locsize, numBlocks;

	if (!logfile || !FS_Initialized())
		return;
	for (block = hunkblocks ; block; block = block->next) {
		block->printed = qfalse;
	}
	size = 0;
	numBlocks = 0;
	Com_sprintf(buf, sizeof(buf), "\r\n================\r\nHunk Small log\r\n================\r\n");
	FS_Write(buf, (int)strlen(buf), logfile);
	for (block = hunkblocks; block; block = block->next) {
		if (block->printed) {
			continue;
		}
		locsize = block->size;
		for (block2 = block->next; block2; block2 = block2->next) {
			if (block->line != block2->line) {
				continue;
			}
			if (Q_stricmp(block->file, block2->file)) {
				continue;
			}
			size += block2->size;
			locsize += block2->size;
			block2->printed = qtrue;
		}
#ifdef HUNK_DEBUG
		Com_sprintf(buf, sizeof(buf), "size = %8d: %s, line: %d (%s)\r\n", locsize, block->file, block->line, block->label);
		FS_Write(buf, (int)strlen(buf), logfile);
#endif
		size += block->size;
		numBlocks++;
	}
	Com_sprintf(buf, sizeof(buf), "%d Hunk memory\r\n", size);
	FS_Write(buf, (int)strlen(buf), logfile);
	Com_sprintf(buf, sizeof(buf), "%d hunk blocks\r\n", numBlocks);
	FS_Write(buf, (int)strlen(buf), logfile);
}

/*
=================
Com_InitZoneMemory
=================
*/
void Com_InitHunkMemory( void ) {
	cvar_t	*cv;
	int nMinAlloc;
	char *pMsg = NULL;

	// make sure the file system has allocated and "not" freed any temp blocks
	// this allows the config and product id files ( journal files too ) to be loaded
	// by the file system without redunant routines in the file system utilizing different
	// memory systems
	if (FS_LoadStack() != 0) {
		Com_Error( ERR_FATAL, "Hunk initialization failed. File system load stack not zero");
	}

	// allocate the stack based hunk allocator
	cv = Cvar_Get( "com_hunkMegs", DEF_COMHUNKMEGS, CVAR_LATCH | CVAR_ARCHIVE | CVAR_GLOBAL );

	// if we are not dedicated min allocation is 56, otherwise min is 1
	if (com_dedicated && com_dedicated->integer) {
		nMinAlloc = MIN_DEDICATED_COMHUNKMEGS;
		pMsg = "Minimum com_hunkMegs for a dedicated server is %i, allocating %i megs.\n";
	}
	else {
		nMinAlloc = MIN_COMHUNKMEGS;
		pMsg = "Minimum com_hunkMegs is %i, allocating %i megs.\n";
	}

	if ( cv->integer < nMinAlloc ) {
		s_hunkTotal = 1024 * 1024 * nMinAlloc;
	    Com_Printf(pMsg, nMinAlloc, s_hunkTotal / (1024 * 1024));
	} else {
		s_hunkTotal = cv->integer * 1024 * 1024;
	}


	// bk001205 - was malloc
	s_hunkData = (unsigned char *)calloc( s_hunkTotal + 31, 1 );
	if ( !s_hunkData ) {
		Com_Error( ERR_FATAL, "Hunk data failed to allocate %i megs", s_hunkTotal / (1024*1024) );
	}
	// cacheline align
	s_hunkData = (byte *) ( ( (intptr_t)s_hunkData + 31 ) & ~31 );
	Hunk_Clear();

	Cmd_AddCommand( "meminfo", Com_Meminfo_f );
#ifdef HUNK_DEBUG
	Cmd_AddCommand( "hunklog", Hunk_Log );
	Cmd_AddCommand( "hunksmalllog", Hunk_SmallLog );
#endif
}

/*
====================
Hunk_MemoryRemaining
====================
*/
int	Hunk_MemoryRemaining( void ) {
	int		low, high;

	low = hunk_low.permanent > hunk_low.temp ? hunk_low.permanent : hunk_low.temp;
	high = hunk_high.permanent > hunk_high.temp ? hunk_high.permanent : hunk_high.temp;

	return s_hunkTotal - ( low + high );
}

/*
===================
Hunk_SetMark

The server calls this after the level and game VM have been loaded
===================
*/
void Hunk_SetMark( void ) {
	hunk_low.mark = hunk_low.permanent;
	hunk_high.mark = hunk_high.permanent;
}

/*
=================
Hunk_ClearToMark

The client calls this before starting a vid_restart or snd_restart
=================
*/
void Hunk_ClearToMark( void ) {
	hunk_low.permanent = hunk_low.temp = hunk_low.mark;
	hunk_high.permanent = hunk_high.temp = hunk_high.mark;
}

/*
=================
Hunk_CheckMark
=================
*/
qboolean Hunk_CheckMark( void ) {
	if( hunk_low.mark || hunk_high.mark ) {
		return qtrue;
	}
	return qfalse;
}

void CL_ShutdownCGame( void );
void CL_ShutdownUI( void );
void SV_ShutdownGameProgs( void );

/*
=================
Hunk_Clear

The server calls this before shutting down or loading a new map
=================
*/
void Hunk_Clear( void ) {

#ifndef DEDICATED
	CL_ShutdownCGame();
	CL_ShutdownUI();
#endif
	SV_ShutdownGameProgs();
#ifndef DEDICATED
	CIN_CloseAllVideos();
#endif
	hunk_low.mark = 0;
	hunk_low.permanent = 0;
	hunk_low.temp = 0;
	hunk_low.tempHighwater = 0;

	hunk_high.mark = 0;
	hunk_high.permanent = 0;
	hunk_high.temp = 0;
	hunk_high.tempHighwater = 0;

	hunk_permanent = &hunk_low;
	hunk_temp = &hunk_high;

	Com_Printf( "Hunk_Clear: reset the hunk ok\n" );
	VM_Clear();
#ifdef HUNK_DEBUG
	hunkblocks = NULL;
#endif
}

static void Hunk_SwapBanks( void ) {
	hunkUsed_t	*swap;

	// can't swap banks if there is any temp already allocated
	if ( hunk_temp->temp != hunk_temp->permanent ) {
		return;
	}

	// if we have a larger highwater mark on this side, start making
	// our permanent allocations here and use the other side for temp
	if ( hunk_temp->tempHighwater - hunk_temp->permanent >
		hunk_permanent->tempHighwater - hunk_permanent->permanent ) {
		swap = hunk_temp;
		hunk_temp = hunk_permanent;
		hunk_permanent = swap;
	}
}

/*
=================
Hunk_Alloc

Allocate permanent (until the hunk is cleared) memory
=================
*/
#ifdef HUNK_DEBUG
void *Hunk_AllocDebug( int size, ha_pref preference, char *label, char *file, int line ) {
#else
void *Hunk_Alloc( int size, ha_pref preference ) {
#endif
	void	*buf;

	if ( s_hunkData == NULL)
	{
		Com_Error( ERR_FATAL, "Hunk_Alloc: Hunk memory system not initialized" );
	}

	// can't do preference if there is any temp allocated
	if (preference == h_dontcare || hunk_temp->temp != hunk_temp->permanent) {
		Hunk_SwapBanks();
	} else {
		if (preference == h_low && hunk_permanent != &hunk_low) {
			Hunk_SwapBanks();
		} else if (preference == h_high && hunk_permanent != &hunk_high) {
			Hunk_SwapBanks();
		}
	}

#ifdef HUNK_DEBUG
	size += sizeof(hunkblock_t);
#endif

	// round to cacheline
	size = (size+31)&~31;

	if ( hunk_low.temp + hunk_high.temp + size > s_hunkTotal ) {
#ifdef HUNK_DEBUG
		Hunk_Log();
		Hunk_SmallLog();
#endif
		Com_Error( ERR_DROP, "Hunk_Alloc failed on %i", size );
	}

	if ( hunk_permanent == &hunk_low ) {
		buf = (void *)(s_hunkData + hunk_permanent->permanent);
		hunk_permanent->permanent += size;
	} else {
		hunk_permanent->permanent += size;
		buf = (void *)(s_hunkData + s_hunkTotal - hunk_permanent->permanent );
	}

	hunk_permanent->temp = hunk_permanent->permanent;

	Com_Memset( buf, 0, size );

#ifdef HUNK_DEBUG
	{
		hunkblock_t *block;

		block = (hunkblock_t *) buf;
		block->size = size - sizeof(hunkblock_t);
		block->file = file;
		block->label = label;
		block->line = line;
		block->next = hunkblocks;
		hunkblocks = block;
		buf = ((byte *) buf) + sizeof(hunkblock_t);
	}
#endif
	return buf;
}

/*
=================
Hunk_AllocateTempMemory

This is used by the file loading system.
Multiple files can be loaded in temporary memory.
When the files-in-use count reaches zero, all temp memory will be deleted
=================
*/
void *Hunk_AllocateTempMemory( int size ) {
	void		*buf;
	hunkHeader_t	*hdr;

	// return a Z_Malloc'd block if the hunk has not been initialized
	// this allows the config and product id files ( journal files too ) to be loaded
	// by the file system without redunant routines in the file system utilizing different
	// memory systems
	if ( s_hunkData == NULL )
	{
		return Z_Malloc(size, TAG_HUNK, qtrue);
	}

	Hunk_SwapBanks();

	size = ( (size+3)&~3 ) + sizeof( hunkHeader_t );

	if ( hunk_temp->temp + hunk_permanent->permanent + size > s_hunkTotal ) {
		Com_Error( ERR_DROP, "Hunk_AllocateTempMemory: failed on %i", size );
	}

	if ( hunk_temp == &hunk_low ) {
		buf = (void *)(s_hunkData + hunk_temp->temp);
		hunk_temp->temp += size;
	} else {
		hunk_temp->temp += size;
		buf = (void *)(s_hunkData + s_hunkTotal - hunk_temp->temp );
	}

	if ( hunk_temp->temp > hunk_temp->tempHighwater ) {
		hunk_temp->tempHighwater = hunk_temp->temp;
	}

	hdr = (hunkHeader_t *)buf;
	buf = (void *)(hdr+1);

	hdr->magic = HUNK_MAGIC;
	hdr->size = size;

	// don't bother clearing, because we are going to load a file over it
	return buf;
}


/*
==================
Hunk_FreeTempMemory
==================
*/
void Hunk_FreeTempMemory( void *buf ) {
	hunkHeader_t	*hdr;

	  // free with Z_Free if the hunk has not been initialized
	  // this allows the config and product id files ( journal files too ) to be loaded
	  // by the file system without redunant routines in the file system utilizing different
	  // memory systems
	if ( s_hunkData == NULL )
	{
		Z_Free(buf);
		return;
	}


	hdr = ( (hunkHeader_t *)buf ) - 1;
	if ( hdr->magic != HUNK_MAGIC ) {
		Com_Error( ERR_FATAL, "Hunk_FreeTempMemory: bad magic" );
	}

	hdr->magic = HUNK_FREE_MAGIC;

	// this only works if the files are freed in stack order,
	// otherwise the memory will stay around until Hunk_ClearTempMemory
	if ( hunk_temp == &hunk_low ) {
		if ( hdr == (void *)(s_hunkData + hunk_temp->temp - hdr->size ) ) {
			hunk_temp->temp -= hdr->size;
		} else {
			Com_Printf( "Hunk_FreeTempMemory: not the final block\n" );
		}
	} else {
		if ( hdr == (void *)(s_hunkData + s_hunkTotal - hunk_temp->temp ) ) {
			hunk_temp->temp -= hdr->size;
		} else {
			Com_Printf( "Hunk_FreeTempMemory: not the final block\n" );
		}
	}
}


/*
=================
Hunk_ClearTempMemory

The temp space is no longer needed.  If we have left more
touched but unused memory on this side, have future
permanent allocs use this side.
=================
*/
void Hunk_ClearTempMemory( void ) {
	if ( s_hunkData != NULL ) {
		hunk_temp->temp = hunk_temp->permanent;
	}
}

/*
=================
Hunk_Trash
=================
*/
void Hunk_Trash( void ) {
	return;
#if 0
	int length, i, rnd;
	char *buf, value;

	if ( s_hunkData == NULL )
		return;

#ifdef _DEBUG
	Com_Error(ERR_DROP, "hunk trashed\n");
	return;
#endif

	Cvar_Set("com_jp", "1");
	Hunk_SwapBanks();

	if ( hunk_permanent == &hunk_low ) {
		buf = (char *)(s_hunkData + hunk_permanent->permanent);
	} else {
		buf = (char *)(s_hunkData + s_hunkTotal - hunk_permanent->permanent );
	}
	length = hunk_permanent->permanent;

	if (length > 0x7FFFF) {
		//randomly trash data within buf
		rnd = random() * (length - 0x7FFFF);
		value = 31;
		for (i = 0; i < 0x7FFFF; i++) {
			value *= 109;
			buf[rnd+i] ^= value;
		}
	}
#endif
}

/*
===================================================================

EVENTS AND JOURNALING

In addition to these events, .cfg files are also copied to the
journaled file
===================================================================
*/

// bk001129 - here we go again: upped from 64
#define	MAX_PUSHED_EVENTS	            256
// bk001129 - init, also static
static int		com_pushedEventsHead = 0;
static int             com_pushedEventsTail = 0;
// bk001129 - static
static sysEvent_t	com_pushedEvents[MAX_PUSHED_EVENTS];

/*
=================
Com_InitJournaling
=================
*/
void Com_InitJournaling( void ) {
	Com_StartupVariable( "journal" );
	com_journal = Cvar_Get ("journal", "0", CVAR_INIT);
	if ( !com_journal->integer ) {
		return;
	}

	if ( com_journal->integer == 1 ) {
		Com_Printf( "Journaling events\n");
		com_journalFile = FS_FOpenFileWrite( "journal.dat" );
		com_journalDataFile = FS_FOpenFileWrite( "journaldata.dat" );
	} else if ( com_journal->integer == 2 ) {
		Com_Printf( "Replaying journaled events\n");
		FS_FOpenFileRead( "journal.dat", &com_journalFile, qtrue );
		FS_FOpenFileRead( "journaldata.dat", &com_journalDataFile, qtrue );
	}

	if ( !com_journalFile || !com_journalDataFile ) {
		Cvar_Set( "com_journal", "0" );
		com_journalFile = 0;
		com_journalDataFile = 0;
		Com_Printf( "Couldn't open journal files\n" );
	}
}

/*
=================
Com_GetRealEvent
=================
*/
sysEvent_t	Com_GetRealEvent( void ) {
	int			r;
	sysEvent_t	ev;

	// either get an event from the system or the journal file
	if ( com_journal->integer == 2 ) {
		r = FS_Read( &ev, sizeof(ev), com_journalFile );
		if ( r != sizeof(ev) ) {
			Com_Error( ERR_FATAL, "Error reading from journal file" );
		}
		if ( ev.evPtrLength ) {
			ev.evPtr = Z_Malloc( ev.evPtrLength, TAG_EVENT, qtrue );
			r = FS_Read( ev.evPtr, ev.evPtrLength, com_journalFile );
			if ( r != ev.evPtrLength ) {
				Com_Error( ERR_FATAL, "Error reading from journal file" );
			}
		}
	} else {
		ev = Sys_GetEvent();

		// write the journal value out if needed
		if ( com_journal->integer == 1 ) {
			r = FS_Write( &ev, sizeof(ev), com_journalFile );
			if ( r != sizeof(ev) ) {
				Com_Error( ERR_FATAL, "Error writing to journal file" );
			}
			if ( ev.evPtrLength ) {
				r = FS_Write( ev.evPtr, ev.evPtrLength, com_journalFile );
				if ( r != ev.evPtrLength ) {
					Com_Error( ERR_FATAL, "Error writing to journal file" );
				}
			}
		}
	}

	return ev;
}


/*
=================
Com_InitPushEvent
=================
*/
// bk001129 - added
void Com_InitPushEvent( void ) {
  // clear the static buffer array
  // this requires SE_NONE to be accepted as a valid but NOP event
  memset( com_pushedEvents, 0, sizeof(com_pushedEvents) );
  // reset counters while we are at it
  // beware: GetEvent might still return an SE_NONE from the buffer
  com_pushedEventsHead = 0;
  com_pushedEventsTail = 0;
}


/*
=================
Com_PushEvent
=================
*/
void Com_PushEvent( sysEvent_t *event ) {
	sysEvent_t		*ev;
	static int printedWarning = 0; // bk001129 - init, bk001204 - explicit int

	ev = &com_pushedEvents[ com_pushedEventsHead & (MAX_PUSHED_EVENTS-1) ];

	if ( com_pushedEventsHead - com_pushedEventsTail >= MAX_PUSHED_EVENTS ) {

		// don't print the warning constantly, or it can give time for more...
		if ( !printedWarning ) {
			printedWarning = qtrue;
			Com_Printf( "WARNING: Com_PushEvent overflow\n" );
		}

		if ( ev->evPtr ) {
			Z_Free( ev->evPtr );
		}
		com_pushedEventsTail++;
	} else {
		printedWarning = qfalse;
	}

	*ev = *event;
	com_pushedEventsHead++;
}

/*
=================
Com_GetEvent
=================
*/
sysEvent_t	Com_GetEvent( void ) {
	if ( com_pushedEventsHead > com_pushedEventsTail ) {
		com_pushedEventsTail++;
		return com_pushedEvents[ (com_pushedEventsTail-1) & (MAX_PUSHED_EVENTS-1) ];
	}
	return Com_GetRealEvent();
}

/*
=================
Com_RunAndTimeServerPacket
=================
*/
void Com_RunAndTimeServerPacket( netadr_t *evFrom, msg_t *buf ) {
	int		t1, t2, msec;

	t1 = 0;

	if ( com_speeds->integer ) {
		t1 = Sys_Milliseconds ();
	}

	SV_PacketEvent( *evFrom, buf );

	if ( com_speeds->integer ) {
		t2 = Sys_Milliseconds ();
		msec = t2 - t1;
		if ( com_speeds->integer == 3 ) {
			Com_Printf( "SV_PacketEvent time: %i\n", msec );
		}
	}
}

/*
=================
Com_EventLoop

Returns last event time
=================
*/
int Com_EventLoop( void ) {
	sysEvent_t	ev;
	netadr_t	evFrom;
	byte		bufData[MAX_MSGLEN];
	msg_t		buf;

	MSG_Init( &buf, bufData, sizeof( bufData ) );

	while ( 1 ) {
		ev = Com_GetEvent();

		// if no more events are available
		if ( ev.evType == SE_NONE ) {
			// manually send packet events for the loopback channel
			while ( NET_GetLoopPacket( NS_CLIENT, &evFrom, &buf ) ) {
				CL_PacketEvent( evFrom, &buf );
			}

			while ( NET_GetLoopPacket( NS_SERVER, &evFrom, &buf ) ) {
				// if the server just shut down, flush the events
				if ( com_sv_running->integer ) {
					Com_RunAndTimeServerPacket( &evFrom, &buf );
				}
			}

			return ev.evTime;
		}


		switch ( ev.evType ) {
		default:
		  // bk001129 - was ev.evTime
			Com_Error( ERR_FATAL, "Com_EventLoop: bad event type %i", ev.evType );
			break;
		case SE_NONE:
			break;
		case SE_KEY:
			CL_KeyEvent( ev.evValue, (qboolean)ev.evValue2, ev.evTime );
			break;
		case SE_CHAR:
			CL_CharEvent( ev.evValue );
			break;
		case SE_MOUSE:
			CL_MouseEvent( ev.evValue, ev.evValue2, ev.evTime );
			break;
		case SE_JOYSTICK_AXIS:
			CL_JoystickEvent( ev.evValue, ev.evValue2, ev.evTime );
			break;
		case SE_CONSOLE:
			if ( ((char *)ev.evPtr)[0] == '\\' || ((char *)ev.evPtr)[0] == '/' )
			{
				Cbuf_AddText( (char *)ev.evPtr+1 );
			}
			else
			{
				Cbuf_AddText( (char *)ev.evPtr );
			}
			Cbuf_AddText( "\n" );
			break;
		}

		// free any block data
		if ( ev.evPtr ) {
			Z_Free( ev.evPtr );
		}
	}

	return 0;	// never reached
}

/*
================
Com_Milliseconds

Can be used for profiling, but will be journaled accurately
================
*/
int Com_Milliseconds (void) {
	sysEvent_t	ev;

	// get events and push them until we get a null event with the current time
	do {

		ev = Com_GetRealEvent();
		if ( ev.evType != SE_NONE ) {
			Com_PushEvent( &ev );
		}
	} while ( ev.evType != SE_NONE );

	return ev.evTime;
}

//============================================================================

/*
=============
Com_Error_f

Just throw a fatal error to
test error shutdown procedures
=============
*/
static void Com_Error_f (void) {
	if ( Cmd_Argc() > 1 ) {
		Com_Error( ERR_DROP, "Testing drop error" );
	} else {
		Com_Error( ERR_FATAL, "Testing fatal error" );
	}
}


/*
=============
Com_Freeze_f

Just freeze in place for a given number of seconds to test
error recovery
=============
*/
static void Com_Freeze_f (void) {
	float	s;
	int		start, now;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "freeze <seconds>\n" );
		return;
	}
	s = atof( Cmd_Argv(1) );

	start = Com_Milliseconds();

	while ( 1 ) {
		now = Com_Milliseconds();
		if ( ( now - start ) * 0.001 > s ) {
			break;
		}
	}
}

#ifdef MEM_DEBUG
	void SH_Register(void);
#endif

/*
=================
Com_Init
=================
*/

void Com_Init( char *commandLine ) {
	char	*s;

	Com_Printf( "%s %s %s\n", Q3_VERSION, CPUSTRING, __DATE__ );

	if (setjmp(abortframe)) {
		Sys_Error("Error during initialization");
	}

	// multiprotocol support
	// startup will be UNDEFINED
	MV_SetCurrentGameversion(VERSION_UNDEF);

	// bk001129 - do this before anything else decides to push events
	Com_InitPushEvent();

	Cvar_Init ();

	// prepare enough of the subsystems to handle
	// cvar and command buffer management
	Com_ParseCommandLine( commandLine );

//	Swap_Init ();
	Cbuf_Init ();

	Com_InitZoneMemory();
	Cmd_Init ();

	// override anything from the config files with command line args
	Com_StartupVariable( NULL );

	// get the developer cvar set as early as possible
	Com_StartupVariable( "developer" );

	// done early so bind command exists
	CL_InitKeyCommands();

	FS_InitFilesystem ();

	Com_InitJournaling();

	Cbuf_AddText ("exec mpdefault.cfg\n");

	// skip the jk2mpconfig.cfg if "safe" is on the command line
	if ( !Com_SafeMode() ) {
#ifdef DEDICATED
		Cbuf_AddText ("exec jk2mvserver.cfg\n");
#else
		Cbuf_AddText ("exec jk2mvconfig.cfg\n");
		Cbuf_AddText("exec jk2mvglobal.cfg\n");
#endif
	}

	Cbuf_AddText ("exec autoexec.cfg\n");

	Cbuf_Execute ();

	// override anything from the config files with command line args
	Com_StartupVariable( NULL );

	// get dedicated here for proper hunk megs initialization
#ifdef DEDICATED
	com_dedicated = Cvar_Get ("dedicated", "2", CVAR_ROM);
#else
	com_dedicated = Cvar_Get ("dedicated", "0", CVAR_LATCH);
#endif
	// allocate the stack based hunk allocator
	Com_InitHunkMemory();

	// if any archived cvars are modified after this, we will trigger a writing
	// of the config file
	cvar_modifiedFlags &= ~CVAR_ARCHIVE;

	//
	// init commands and vars
	//
	Cvar_Get("protocol", "16", CVAR_SERVERINFO | CVAR_ROM);

	com_blood = Cvar_Get("com_blood", "1", CVAR_ARCHIVE | CVAR_GLOBAL);

	com_developer = Cvar_Get ("developer", "0", CVAR_TEMP );
	com_logfile = Cvar_Get ("logfile", "0", CVAR_TEMP );

	com_timescale = Cvar_Get ("timescale", "1", CVAR_CHEAT | CVAR_SYSTEMINFO );
	com_fixedtime = Cvar_Get ("fixedtime", "0", CVAR_CHEAT);
	com_showtrace = Cvar_Get ("com_showtrace", "0", CVAR_CHEAT);
	com_dropsim = Cvar_Get ("com_dropsim", "0", CVAR_CHEAT);
	com_viewlog = Cvar_Get( "viewlog", "0", CVAR_CHEAT );
	com_speeds = Cvar_Get ("com_speeds", "0", 0);
	com_timedemo = Cvar_Get ("timedemo", "0", 0);
	com_cameraMode = Cvar_Get ("com_cameraMode", "0", CVAR_CHEAT);

	cl_paused = Cvar_Get ("cl_paused", "0", CVAR_ROM);
	sv_paused = Cvar_Get ("sv_paused", "0", CVAR_ROM);
	com_sv_running = Cvar_Get ("sv_running", "0", CVAR_ROM);
	com_cl_running = Cvar_Get ("cl_running", "0", CVAR_ROM);
	com_buildScript = Cvar_Get( "com_buildScript", "0", 0 );
	com_busyWait = Cvar_Get("com_busyWait", "0", CVAR_ARCHIVE | CVAR_GLOBAL);

	com_introPlayed = Cvar_Get("com_introplayed", "0", CVAR_ARCHIVE | CVAR_GLOBAL);

	Cvar_Get ("com_othertasks", "0", CVAR_ROM );
	Cvar_Get("com_ignoreothertasks", "0", CVAR_ARCHIVE | CVAR_GLOBAL);

	mv_apienabled = Cvar_Get("mv_apienabled", "1", CVAR_ROM);

	if ( com_dedicated->integer ) {
		if ( !com_viewlog->integer ) {
			Cvar_Set( "viewlog", "1" );
		}
	}

	if ( com_developer && com_developer->integer ) {
		Cmd_AddCommand ("error", Com_Error_f);
		Cmd_AddCommand ("freeze", Com_Freeze_f);
	}
	Cmd_AddCommand ("quit", Com_Quit_f);
	Cmd_AddCommand ("changeVectors", MSG_ReportChangeVectors_f );
	Cmd_AddCommand ("writeconfig", Com_WriteConfig_f );
	Cmd_SetCommandCompletionFunc( "writeconfig", Cmd_CompleteCfgName );

	s = va("%s %s %s", Q3_VERSION, CPUSTRING, __DATE__ );
	com_version = Cvar_Get ("version", s, CVAR_ROM | CVAR_SERVERINFO );

	SP_Init();
	Sys_Init();
	Netchan_Init( Com_Milliseconds() & 0xffff );	// pick a port value that should be nice and random
	VM_Init();
	SV_Init();

	com_dedicated->modified = qfalse;
	if ( !com_dedicated->integer ) {
		CL_Init();
	}

	// set com_frameTime so that if a map is started on the
	// command line it will still be able to count on com_frameTime
	// being random enough for a serverid
	com_frameTime = Com_Milliseconds();

	// add + commands from command line
	Com_AddStartupCommands();

	// start in full screen ui mode
	Cvar_Set("r_uiFullScreen", "1");

	CL_StartHunkUsers();

	// make sure single player is off by default
	Cvar_Set("ui_singlePlayerActive", "0");

#ifdef MEM_DEBUG
	SH_Register();
#endif

	com_fullyInitialized = qtrue;
	Com_Printf ("--- Common Initialization Complete ---\n");
}

//==================================================================

void Com_WriteConfigToFile(const char *localfile, const char *globalfile) {
	fileHandle_t	fl = 0, fg = 0;

	fl = FS_FOpenFileWrite(localfile);
	if ( !fl ) {
		Com_Printf ("Couldn't write %s.\n", localfile);
		return;
	}

	if (globalfile != NULL) {
		fg = FS_FOpenBaseFileWrite(globalfile);
		if (!fg) {
			Com_Printf("Couldn't write %s.\n", globalfile);
			return;
		}
	}

	FS_Printf(fl, "// generated by jk2mv, do not modify\n");
	Key_WriteBindings(fl);
	Cvar_WriteVariables(fl, qtrue);
	if (globalfile == NULL) {
		Cvar_WriteVariables(fl, qfalse);
	}
	FS_FCloseFile(fl);

	if (globalfile != NULL) {
		FS_Printf(fg, "// generated by jk2mv, do not modify\n");
		Cvar_WriteVariables(fg, qfalse);
		FS_FCloseFile(fg);
	}
}


/*
===============
Com_WriteConfiguration

Writes key bindings and archived cvars to config file if modified
===============
*/
void Com_WriteConfiguration( void ) {
	// if we are quiting without fully initializing, make sure
	// we don't write out anything
	if ( !com_fullyInitialized ) {
		return;
	}

	if ( !(cvar_modifiedFlags & CVAR_ARCHIVE ) ) {
		return;
	}
	cvar_modifiedFlags &= ~CVAR_ARCHIVE;

#ifdef DEDICATED
	Com_WriteConfigToFile( "jk2mvserver.cfg", NULL );
#else
	Com_WriteConfigToFile("jk2mvconfig.cfg", "jk2mvglobal.cfg");
#endif
}


/*
===============
Com_WriteConfig_f

Write the config file to a specific name
===============
*/
void Com_WriteConfig_f( void ) {
	char	filename[MAX_QPATH];

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: writeconfig <filename>\n" );
		return;
	}

	Q_strncpyz( filename, Cmd_Argv(1), sizeof( filename ) );
	COM_DefaultExtension( filename, sizeof( filename ), ".cfg" );
	Com_Printf( "Writing %s.\n", filename );
	Com_WriteConfigToFile(filename, va("%s_globals.cfg", Cmd_Argv(1)));
}

/*
================
Com_ModifyMsec
================
*/
int Com_ModifyMsec( int msec ) {
	int		clampTime;

	//
	// modify time for debugging values
	//
	if ( com_fixedtime->integer ) {
		msec = com_fixedtime->integer;
	} else if ( com_timescale->value ) {
		msec *= com_timescale->value;
	} else if (com_cameraMode->integer) {
		msec *= com_timescale->value;
	}

	// don't let it scale below 1 msec
	if ( msec < 1 && com_timescale->value) {
		msec = 1;
	}

	if ( com_dedicated->integer ) {
		// dedicated servers don't want to clamp for a much longer
		// period, because it would mess up all the client's views
		// of time.
		if ( msec > 500 ) {
			Com_Printf( "Hitch warning: %i msec frame time\n", msec );
		}
		clampTime = 5000;
	} else
	if ( !com_sv_running->integer ) {
		// clients of remote servers do not want to clamp time, because
		// it would skew their view of the server's time temporarily
		clampTime = 5000;
	} else {
		// for local single player gaming
		// we may want to clamp the time to prevent players from
		// flying off edges when something hitches.
		clampTime = 200;
	}

	if ( msec > clampTime ) {
		msec = clampTime;
	}

	if ( com_demoplaying && ( (cl_paused && cl_paused->integer) || !com_timescale->value)) {
		msec = 0;	// if we're playing demo and brought up menu via ESC or have timescale set to 0, pause demo
	}

	return msec;
}

/*
=================
Com_TimeVal
=================
*/
int Com_TimeVal(int minMsec) {
	int timeVal;

	timeVal = Sys_Milliseconds() - com_frameTime;

	if (timeVal >= minMsec)
		timeVal = 0;
	else
		timeVal = minMsec - timeVal;

	return timeVal;
}

/*
=================
Com_Frame
=================
*/
void Com_Frame( void ) {
	int		msec, minMsec;
	int		timeVal;
	static int	lastTime = 0, bias = 0;

	int timeBeforeFirstEvents = 0;
	int timeBeforeServer = 0;
	int timeBeforeEvents = 0;
	int timeBeforeClient = 0;
	int timeAfter = 0;

	if (setjmp(abortframe)) {
		return;			// an ERR_DROP was thrown
	}

	// write config file if anything changed
	Com_WriteConfiguration();

	//
	// main event loop
	//
	if ( com_speeds->integer ) {
		timeBeforeFirstEvents = Sys_Milliseconds ();
	}

	// Figure out how much time we have
	if (!com_timedemo->integer) {
		if (com_dedicated->integer)
			minMsec = SV_FrameMsec();
		else {
			if (com_minimized->integer && com_maxfpsMinimized->integer > 0)
				minMsec = 1000 / com_maxfpsMinimized->integer;
			else if (com_unfocused->integer && com_maxfpsUnfocused->integer > 0)
				minMsec = 1000 / com_maxfpsUnfocused->integer;
			else if (com_maxfps->integer > 0)
				minMsec = 1000 / com_maxfps->integer;
			else
				minMsec = 1;

#ifndef DEDICATED
			// limit FPS while downloading so slow systems still reach full-speed
			if (CL_DownloadRunning())
				minMsec = 1000 / 20;
#endif

			timeVal = com_frameTime - lastTime;
			bias += timeVal - minMsec;

			if (bias > minMsec)
				bias = minMsec;

			// Adjust minMsec if previous frame took too long to render so
			// that framerate is stable at the requested value.
			minMsec -= bias;
		}
	} else
		minMsec = 1;

	timeVal = Com_TimeVal(minMsec);
	do {
		// Busy sleep the last millisecond for better timeout precision
		if (com_busyWait->integer || timeVal < 1)
			NET_Sleep(0);
		else
			NET_Sleep(timeVal - 1);
	} while ((timeVal = Com_TimeVal(minMsec)) != 0);

	// make sure mouse and joystick are only called once a frame
	IN_Frame();

	lastTime = com_frameTime;
	com_frameTime = Com_EventLoop();

	msec = com_frameTime - lastTime;

	Cbuf_Execute();

	// mess with msec if needed
	com_frameMsec = msec;
	msec = Com_ModifyMsec( msec );

	NET_HTTP_ProcessEvents();

	//
	// server side
	//
	if ( com_speeds->integer ) {
		timeBeforeServer = Sys_Milliseconds ();
	}

	SV_Frame( msec );

	// if "dedicated" has been modified, start up
	// or shut down the client system.
	// Do this after the server may have started,
	// but before the client tries to auto-connect
	if ( com_dedicated->modified ) {
		// get the latched value
		Cvar_Get( "dedicated", "0", 0 );
		com_dedicated->modified = qfalse;
		if ( !com_dedicated->integer ) {
			CON_DeleteConsoleWindow();
			CL_Init();
			CL_StartHunkUsers();	//fire up the UI!
		} else {
			CL_Shutdown();
			CON_CreateConsoleWindow();
		}
	}

	//
	// client system
	//
	if ( !com_dedicated->integer ) {
		//
		// run event loop a second time to get server to client packets
		// without a frame of latency
		//
		if ( com_speeds->integer ) {
			timeBeforeEvents = Sys_Milliseconds ();
		}
		Com_EventLoop();
		Cbuf_Execute ();


		//
		// client side
		//
		if ( com_speeds->integer ) {
			timeBeforeClient = Sys_Milliseconds ();
		}

		CL_Frame( msec );

		if ( com_speeds->integer ) {
			timeAfter = Sys_Milliseconds ();
		}
	}

	//
	// report timing information
	//
	if ( com_speeds->integer ) {
		int			all, sv, ev, cl;

		all = timeAfter - timeBeforeServer;
		sv = timeBeforeEvents - timeBeforeServer;
		ev = timeBeforeServer - timeBeforeFirstEvents + timeBeforeClient - timeBeforeEvents;
		cl = timeAfter - timeBeforeClient;
		sv -= time_game;
		cl -= time_frontend + time_backend;

		Com_Printf ("frame:%i all:%3i sv:%3i ev:%3i cl:%3i gm:%3i rf:%3i bk:%3i\n",
					 com_frameNumber, all, sv, ev, cl, time_game, time_frontend, time_backend );
	}

	//
	// trace optimization tracking
	//
	if ( com_showtrace->integer ) {

		extern	int c_traces, c_brush_traces, c_patch_traces;
		extern	int	c_pointcontents;

		Com_Printf ("%4i traces  (%ib %ip) %4i points\n", c_traces,
			c_brush_traces, c_patch_traces, c_pointcontents);
		c_traces = 0;
		c_brush_traces = 0;
		c_patch_traces = 0;
		c_pointcontents = 0;
	}

	com_frameNumber++;
}

/*
=================
Com_Shutdown
=================
*/
void MSG_shutdownHuffman();
void Com_Shutdown (void)
{
	CM_ClearMap();

	if (logfile) {
		FS_FCloseFile (logfile);
		logfile = 0;
		com_logfile->integer = 0;//don't open up the log file again!!
	}

	if ( com_journalFile ) {
		FS_FCloseFile( com_journalFile );
		com_journalFile = 0;
	}

	MSG_shutdownHuffman();
/*
	// Only used for testing changes to huffman frequency table when tuning.
	{
		extern float Huff_GetCR(void);
		char mess[256];
		sprintf(mess,"Eff. CR = %f\n",Huff_GetCR());
		OutputDebugString(mess);
	}
*/
}

void Com_Memcpy (void* dest, const void* src, const size_t count)
{
	memcpy(dest, src, count);
}

void Com_Memset (void* dest, const int val, const size_t count)
{
	memset(dest, val, count);
}

qboolean Com_Memcmp (const void *src0, const void *src1, const unsigned int count)
{
	return (qboolean)memcmp(src0, src1, count);
}

//------------------------------------------------------------------------

/*
=====================
Q_acos

the msvc acos doesn't always return a value between -PI and PI:

int i;
i = 1065353246;
acos(*(float*) &i) == -1.#IND0

	This should go in q_math but it is too late to add new traps
	to game and ui
=====================
*/
float Q_acos(float c) {
	float angle;

	angle = acos(c);

	if (angle > M_PI) {
		return (float)M_PI;
	}
	if (angle < -M_PI) {
		return (float)M_PI;
	}
	return angle;
}

// multiprotocol support
mvversion_t glbpro;

void MV_SetCurrentGameversion(mvversion_t version) {
	glbpro = version;

	if ( com_fullyInitialized )
	{ // Only do this if we're fully initialized
		if ( version == VERSION_UNDEF )	Cvar_Set("version", va("%s %s %s", Q3_VERSION, CPUSTRING, __DATE__ ));
		else							Cvar_Set("version", va("JK2MP: v1.%02dmv %s %s", version, CPUSTRING, __DATE__)); // Set the version to JK2MP for compatibility reasons
	}
}

mvversion_t MV_GetCurrentGameversion() {
	return glbpro;
}

mvprotocol_t MV_GetCurrentProtocol() {
	switch ( MV_GetCurrentGameversion() )
	{
		case VERSION_1_02:
		case VERSION_1_03:
			return PROTOCOL15;
		case VERSION_1_04:
			return PROTOCOL16;
		default:
			return PROTOCOL_UNDEF;
	}
}

void MV_CopyStringWithColors( const char *src, char *dst, int dstSize, int nonColors )
{
	int i;
	int nonColorCount = 0;

	const bool use102color = MV_USE102COLOR;

	const size_t srclen = strlen(src);

	for ( i = 0; i < srclen; i++ )
	{
		if ( i >= dstSize ) break;
		if ( nonColorCount >= nonColors ) break;

		if (!use102color) {
			if ( !Q_IsColorString(&src[i]) && (i < 1 || !Q_IsColorString(&src[i-1])) )
				nonColorCount++;
		} else {
			if ( !Q_IsColorString_1_02(&src[i]) && (i < 1 || !Q_IsColorString_1_02(&src[i-1])) )
				nonColorCount++;
		}

		dst[i] = src[i];
	}

	if ( i >= (dstSize - 1) ) dst[dstSize-1] = '\0';
	else					  dst[i] = '\0';
}

int MV_StrlenSkipColors( const char *str )
{
	int	  len = 0;
	const char *strPtr = str;

	const bool use102color = MV_USE102COLOR;

	while ( *strPtr != '\0' )
	{
		if ( Q_IsColorString(strPtr) || (use102color && Q_IsColorString_1_02(strPtr)) )
			strPtr++;
		else
			len++;

		strPtr++;
	}

	return len;
}


// for auto-complete (copied from OpenJK)
/*
=============================================================================

CONSOLE LINE EDITING

==============================================================================
*/

static const char *completionString;
static char shortestMatch[MAX_TOKEN_CHARS];
static int	matchCount;
// field we are working on, passed to Field_AutoComplete(&g_consoleCommand for instance)
static field_t *completionField;
static qboolean wasComplete;

#define S_COMPLETION_COLOR S_COLOR_JK2MV



/*
===============
FindMatches

===============
*/
static void FindMatches( const char *s ) {
	int		i;

	if (Q_stricmpn(s, completionString, (int)strlen(completionString))) {
		return;
	}
	matchCount++;
	if ( matchCount == 1 ) {
		Q_strncpyz( shortestMatch, s, sizeof( shortestMatch ) );
		return;
	}

	// cut shortestMatch to the amount common with s
	for ( i = 0 ; s[i] ; i++ ) {
		if ( tolower(shortestMatch[i]) != tolower(s[i]) ) {
			shortestMatch[i] = 0;
			break;
		}
	}
	if (!s[i])
	{
		shortestMatch[i] = 0;
	}
}

/*
===============
PrintMatches

===============
*/
static void PrintMatches( const char *s ) {
	if (!Q_stricmpn(s, shortestMatch, (int)strlen(shortestMatch))) {
		Com_Printf_Ext(qtrue, S_COMPLETION_COLOR "Cmd  " S_COLOR_WHITE "%s\n", s);
	}
}

/*
===============
PrintArgMatches

===============
*/
#if 0
// This is here for if ever commands with other argument completion
static void PrintArgMatches( const char *s ) {
	if ( !Q_stricmpn( s, shortestMatch, strlen( shortestMatch ) ) ) {
		Com_Printf( S_COLOR_WHITE"  %s\n", s );
	}
}
#endif

#ifndef DEDICATED
/*
===============
PrintKeyMatches

===============
*/
static void PrintKeyMatches( const char *s ) {
	if ( !Q_stricmpn( s, shortestMatch, (int)strlen( shortestMatch ) ) ) {
		Com_Printf_Ext(qtrue, S_COMPLETION_COLOR "Key  " S_COLOR_WHITE "%s\n", s );
	}
}
#endif

/*
===============
PrintFileMatches

===============
*/
static void PrintFileMatches( const char *s ) {
	if (!Q_stricmpn(s, shortestMatch, (int)strlen(shortestMatch))) {
		Com_Printf_Ext(qtrue, S_COMPLETION_COLOR "File " S_COLOR_WHITE "%s\n", s);
	}
}

/*
===============
PrintCvarMatches

===============
*/
static void PrintCvarMatches( const char *s ) {
	char value[TRUNCATE_LENGTH] = {0};

	if ( !Q_stricmpn( s, shortestMatch, (int)strlen( shortestMatch ) ) ) {
		Com_TruncateLongString( value, Cvar_VariableString( s ) );
		Com_Printf_Ext(qtrue, S_COMPLETION_COLOR "Cvar " S_COLOR_WHITE "%s = " S_COMPLETION_COLOR "\"", s);
		Com_Printf_Ext(qfalse, S_COLOR_WHITE "%s", value);
		Com_Printf_Ext(qtrue, S_COMPLETION_COLOR "\"" S_COLOR_WHITE "\n");
	}
}


/*
==================
Field_CheckRep
==================
*/
void Field_CheckRep( field_t *edit ) {
#ifndef NDEBUG
	int len = strlen(edit->buffer);

	assert( len < MAX_EDIT_LINE );
	assert( edit->widthInChars >= 0 );
	assert( edit->cursor >= 0 );
	assert( edit->cursor <= len );
	assert( edit->scroll >= 0 );
	assert( edit->scroll <= len );

	assert( 0 <= edit->historyTail && edit->historyTail < FIELD_HISTORY_SIZE );
	assert( 0 <= edit->historyHead && edit->historyHead < FIELD_HISTORY_SIZE );
	assert( 0 <= edit->currentTail && edit->currentTail < FIELD_HISTORY_SIZE );

	if ( edit->historyHead <= edit->historyTail )
		assert( edit->historyHead <= edit->currentTail && edit->currentTail <= edit->historyTail );
	else
		assert( edit->currentTail <= edit->historyTail || edit->historyHead <= edit->currentTail );

	assert( edit->buffer = edit->bufferHistory[edit->currentTail] );
#endif // NDEBUG
}

/*
==================
Field_Clear
==================
*/
void Field_Clear( field_t *edit ) {
	edit->buffer = edit->bufferHistory[0];
	Com_Memset( edit->buffer, 0, MAX_EDIT_LINE );
	edit->cursor = 0;
	edit->scroll = 0;
	edit->historyTail = 0;
	edit->historyHead = 0;
	edit->currentTail = 0;
	edit->typing = qfalse;
	edit->mod = qfalse;
}

/*
===============
Field_FindFirstSeparator
===============
*/
static char *Field_FindFirstSeparator( char *s ) {
	for ( size_t i=0; i<strlen( s ); i++ ) {
		if ( s[i] == ';' )
			return &s[ i ];
	}

	return NULL;
}

/*
===============
Field_Complete
===============
*/
static qboolean Field_Complete( void ) {
	int completionOffset;

	if ( matchCount == 0 )
	{
		wasComplete = qtrue;
		return qtrue;
	}

	completionOffset = (int)strlen(completionField->buffer) - (int)strlen(completionString);

	Q_strncpyz( &completionField->buffer[completionOffset], shortestMatch, MAX_EDIT_LINE - completionOffset );

	completionField->cursor = (int)strlen(completionField->buffer);

	if ( matchCount == 1 ) {
		Q_strcat( completionField->buffer, MAX_EDIT_LINE, " " );
		completionField->cursor++;
		wasComplete = qtrue;
		return qtrue;
	}

	Com_Printf( "%c%s\n", CONSOLE_PROMPT_CHAR, completionField->buffer );

	wasComplete = qfalse;
	return qfalse;
}

#ifndef DEDICATED
/*
===============
Field_CompleteKeyname
===============
*/
void Field_CompleteKeyname( void )
{
	matchCount = 0;
	shortestMatch[ 0 ] = 0;

	Key_KeynameCompletion( FindMatches );

	if( !Field_Complete( ) )
		Key_KeynameCompletion( PrintKeyMatches );
}
#endif

/*
===============
Field_CompleteFilename
===============
*/
void Field_CompleteFilename( const char *dir, const char *ext, qboolean stripExt )
{
	matchCount = 0;
	shortestMatch[ 0 ] = 0;

	FS_FilenameCompletion( dir, ext, stripExt, FindMatches );

	if ( !Field_Complete() )
		FS_FilenameCompletion( dir, ext, stripExt, PrintFileMatches );
}

/*
===============
Field_CompleteCommand
===============
*/
void Field_CompleteCommand( char *cmd, qboolean doCommands, qboolean doCvars, qboolean doArguments )
{
	int completionArgument = 0;

	// Skip leading whitespace and quotes
	cmd = Com_SkipCharset( cmd, " \"" );

	Cmd_TokenizeStringIgnoreQuotes( cmd );
	completionArgument = Cmd_Argc();

	// If there is trailing whitespace on the cmd
	if ( *(cmd + strlen( cmd )-1) == ' ' ) {
		completionString = "";
		completionArgument++;
	}
	else
		completionString = Cmd_Argv( completionArgument - 1 );

	if ( completionArgument > 1 ) {
		const char *baseCmd = Cmd_Argv( 0 );
		char *p;

#ifndef DEDICATED
		if ( baseCmd[0] == '\\' || baseCmd[0] == '/' )
			baseCmd++;
#endif

		if( ( p = Field_FindFirstSeparator( cmd ) ) )
			Field_CompleteCommand( p + 1, qtrue, qtrue, doArguments ); // Compound command
		else if ( doArguments )
			Cmd_CompleteArgument( baseCmd, cmd, completionArgument );
	}
	else {
		if ( completionString[0] == '\\' || completionString[0] == '/' )
			completionString++;

		matchCount = 0;
		shortestMatch[ 0 ] = 0;

		if ( strlen( completionString ) == 0 )
			return;

		if ( doCommands )
			Cmd_CommandCompletion( FindMatches );

		if ( doCvars )
			Cvar_CommandCompletion( FindMatches );

		if ( !Field_Complete() ) {
			// run through again, printing matches
			if ( doCommands )
				Cmd_CommandCompletion( PrintMatches );

			if ( doCvars )
				Cvar_CommandCompletion( PrintCvarMatches );
		}
	}
}

/*
===============
Field_AutoComplete

Perform Tab expansion
===============
*/
void Field_AutoComplete( field_t *field ) {
	if ( !field || !field->buffer[0] )
		return;

	completionField = field;

	Field_CompleteCommand( completionField->buffer, qtrue, qtrue, qtrue );
}

/*
===============
Field_AutoComplete2

Perform Tab expansion
===============
*/
void Field_AutoComplete2( field_t *field, qboolean doCommands, qboolean doCvars, qboolean doArguments ) {
	if ( !field || !field->buffer[0] )
		return;

	completionField = field;

	Field_CompleteCommand( completionField->buffer, doCommands, doCvars, doArguments );
}

int Field_GetLastMatchCount()
{
	return matchCount;
}

qboolean Field_WasComplete()
{
	return wasComplete;
}

/*
====================
FloatAsInt
====================
*/
int FloatAsInt( float f )
{
	floatint_t fi;
	fi.f = f;
	return fi.i;
}
