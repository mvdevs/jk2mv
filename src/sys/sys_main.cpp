#include <csignal>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <sys/stat.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include "../qcommon/qcommon.h"
#include "sys_local.h"
#include "con_local.h"

#ifndef DEDICATED
#include "SDL.h"
#endif

#ifdef WIN32
#include "../win32/win_local.h"
#else
#include "../unix/unix_local.h"
#endif

#ifndef DEDICATED
cvar_t *com_minimized;
cvar_t *com_unfocused;
#endif

/*
========================================================================

EVENT LOOP

========================================================================
*/

#define	MAX_QUED_EVENTS		256
#define	MASK_QUED_EVENTS	( MAX_QUED_EVENTS - 1 )

static sysEvent_t	eventQue[MAX_QUED_EVENTS] = {};
static int			eventHead = 0, eventTail = 0;
static byte		sys_packetReceived[MAX_MSGLEN] = {};

sysEvent_t Sys_GetEvent(void) {
	sysEvent_t	ev;
	char		*s;
	netadr_t	adr;
	msg_t		netmsg;

	// return if we have data
	if (eventHead > eventTail) {
		eventTail++;
		return eventQue[(eventTail - 1) & MASK_QUED_EVENTS];
	}

	// check for console commands
	s = Sys_ConsoleInput();
	if (s) {
		char	*b;
		int		len;

		len = (int)strlen(s) + 1;
		b = (char *)Z_Malloc(len, TAG_EVENT, qfalse);
		strcpy(b, s);
		Sys_QueEvent(0, SE_CONSOLE, 0, 0, len, b);
	}

	// check for network packets
	MSG_Init(&netmsg, sys_packetReceived, sizeof(sys_packetReceived));
	if (Sys_GetPacket(&adr, &netmsg)) {
		netadr_t		*buf;
		int				len;

		// copy out to a seperate buffer for qeueing
		len = sizeof(netadr_t) + netmsg.cursize;
		buf = (netadr_t *)Z_Malloc(len, TAG_EVENT, qfalse);
		*buf = adr;
		memcpy(buf + 1, netmsg.data, netmsg.cursize);
		Sys_QueEvent(0, SE_PACKET, 0, 0, len, buf);
	}

	// return if we have data
	if (eventHead > eventTail) {
		eventTail++;
		return eventQue[(eventTail - 1) & MASK_QUED_EVENTS];
	}

	// create an empty event to return

	memset(&ev, 0, sizeof(ev));
	ev.evTime = Sys_Milliseconds();

	return ev;
}

/*
================
Sys_QueEvent

A time of 0 will get the current time
Ptr should either be null, or point to a block of data that can
be freed by the game later.
================
*/
void Sys_QueEvent(int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr) {
	sysEvent_t	*ev;

	ev = &eventQue[eventHead & MASK_QUED_EVENTS];

	if (eventHead - eventTail >= MAX_QUED_EVENTS) {
		Com_DPrintf("Sys_QueEvent: overflow (event type %i)\n", type);
		// we are discarding an event, but don't leak memory
		if (ev->evPtr) {
			Z_Free(ev->evPtr);
		}
		eventTail++;
	}

	eventHead++;

	if (time == 0) {
		time = Sys_Milliseconds();
	}

	ev->evTime = time;
	ev->evType = type;
	ev->evValue = value;
	ev->evValue2 = value2;
	ev->evPtrLength = ptrLength;
	ev->evPtr = ptr;
}

/*
==================
Sys_GetClipboardData
==================
*/
char *Sys_GetClipboardData( void ) {
#ifdef DEDICATED
	return NULL;
#else
	if ( !SDL_HasClipboardText() )
		return NULL;

	char *cbText = SDL_GetClipboardText();
	size_t len = strlen( cbText ) + 1;

	char *buf = (char *)Z_Malloc( len, TAG_CLIPBOARD );
	Q_strncpyz( buf, cbText, len );

	SDL_free( cbText );
	return buf;
#endif
}

/*
=================
Sys_ConsoleInput

Handle new console input
=================
*/
char *Sys_ConsoleInput(void)
{
	return CON_Input( );
}

void Sys_Print( const char *msg ) {
	// TTimo - prefix for text that shows up in console but not in notify
	// backported from RTCW
	if ( !Q_strncmp( msg, "[skipnotify]", 12 ) ) {
		msg += 12;
	}
	if ( msg[0] == '*' ) {
		msg += 1;
	}
	ConsoleLogAppend( msg );
	CON_Print( msg );
}

/*
================
Sys_Init

Called after the common systems (cvars, files, etc)
are initialized
================
*/
void Sys_Init( void ) {
#ifndef DEDICATED
	Cmd_AddCommand ("in_restart", IN_Restart);
#endif
	Cvar_Get( "arch", ARCH_STRING, CVAR_ROM );
	Cvar_Get( "username", Sys_GetCurrentUser(), CVAR_ROM );
#ifndef DEDICATED
	com_unfocused = Cvar_Get( "com_unfocused", "0", CVAR_ROM );
	com_minimized = Cvar_Get( "com_minimized", "0", CVAR_ROM );
#endif
}

static void Sys_Exit( int ex ) {
	Sys_PlatformExit();

	IN_Shutdown();
#ifndef DEDICATED
	SDL_Quit();
#endif

	NET_Shutdown();

	Com_ShutdownZoneMemory();

	CON_Shutdown();

    exit( ex );
}

#if !defined(DEDICATED)
static void Sys_ErrorDialog( const char *error )
{
	time_t rawtime;
	char timeStr[32] = {}; // should really only reach ~19 chars
	char crashLogPath[MAX_OSPATH];

	time( &rawtime );
	strftime( timeStr, sizeof( timeStr ), "%Y-%m-%d_%H-%M-%S", localtime( &rawtime ) ); // or gmtime
	Com_sprintf( crashLogPath, sizeof( crashLogPath ),
					"%s%ccrashlog-%s.txt",
					Sys_DefaultHomePath(), PATH_SEP, timeStr );

	Sys_Mkdir( Sys_DefaultHomePath() );

	FILE *fp = fopen( crashLogPath, "w" );
	if ( fp )
	{
		ConsoleLogWriteOut( fp );
		fclose( fp );

		const char *errorMessage = va( "%s\n\nThe crash log was written to %s", error, crashLogPath );
		if ( SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "Error", errorMessage, NULL ) < 0 )
		{
			fprintf( stderr, "%s", errorMessage );
		}
	}
	else
	{
		// Getting pretty desperate now
		ConsoleLogWriteOut( stderr );
		fflush( stderr );

		const char *errorMessage = va( "%s\nCould not write the crash log file, but we printed it to stderr.\n"
										"Try running the game using a command line interface.", error );
		if ( SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "Error", errorMessage, NULL ) < 0 )
		{
			// We really have hit rock bottom here :(
			fprintf( stderr, "%s", errorMessage );
		}
	}
}
#endif

void QDECL Sys_Error( const char *error, ... )
{
	va_list argptr;
	char    string[1024];

	va_start (argptr,error);
	vsnprintf(string, sizeof(string), error, argptr);
	va_end (argptr);

	Sys_Print( string );

	// Only print Sys_ErrorDialog for client binary. The dedicated
	// server binary is meant to be a command line program so you would
	// expect to see the error printed.
#if !defined(DEDICATED)
	Sys_ErrorDialog( string );
#endif

	Sys_Exit( 3 );
}

void Sys_Quit (void) {
    Sys_Exit(0);
}

/*
============
Sys_FileTime

returns -1 if not present
============
*/
int Sys_FileTime( char *path )
{
	struct stat buf;

	if ( stat( path, &buf ) == -1 )
		return -1;

	return buf.st_mtime;
}

/*
=================
Sys_SigHandler
=================
*/
void Sys_SigHandler( int signal )
{
	static qboolean signalcaught = qfalse;

	if( signalcaught )
	{
		fprintf( stderr, "DOUBLE SIGNAL FAULT: Received signal %d, exiting...\n",
			signal );
	}
	else
	{
		signalcaught = qtrue;
		//VM_Forced_Unload_Start();
#ifndef DEDICATED
		CL_Shutdown();
		//CL_Shutdown(va("Received signal %d", signal), qtrue, qtrue);
#endif
		SV_Shutdown(va("Received signal %d", signal) );
		//VM_Forced_Unload_Done();
	}

	if( signal == SIGTERM || signal == SIGINT )
		Sys_Exit( 1 );
	else
		Sys_Exit( 2 );
}

int main ( int argc, char* argv[] )
{
	int		i;
	char	commandLine[ MAX_STRING_CHARS ] = { 0 };

    Sys_PlatformInit();
	CON_Init();

	// get the initial time base
	Sys_Milliseconds();

#ifdef MACOS_X
	// This is passed if we are launched by double-clicking
	if ( argc >= 2 && Q_strncmp ( argv[1], "-psn", 4 ) == 0 )
		argc = 1;
#endif

	// Concatenate the command line for passing to Com_Init
	for( i = 1; i < argc; i++ )
	{
		const bool containsSpaces = (strchr(argv[i], ' ') != NULL);
		if (containsSpaces)
			Q_strcat( commandLine, sizeof( commandLine ), "\"" );

		Q_strcat( commandLine, sizeof( commandLine ), argv[ i ] );

		if (containsSpaces)
			Q_strcat( commandLine, sizeof( commandLine ), "\"" );

		Q_strcat( commandLine, sizeof( commandLine ), " " );
	}

	Com_Init (commandLine);

	NET_Init();

	// main game loop
	while (1)
	{
		bool shouldSleep = false;

		if ( com_dedicated->integer )
		{
			shouldSleep = true;
		}

#if !defined(DEDICATED)
		if ( com_minimized->integer )
		{
			shouldSleep = true;
		}
#endif

		if ( shouldSleep )
		{
			Sys_Sleep( 5 );
		}

		// make sure mouse and joystick are only called once a frame
		IN_Frame();

		// run the game
		Com_Frame();
	}

	// never gets here
	return 0;
}

void Sys_ShowConsole(int visLevel, qboolean quitOnClose)
{
}
