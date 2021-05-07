#include <csignal>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <sys/stat.h>
#define __STDC_FORMAT_MACROS
#if (defined(_MSC_VER) && _MSC_VER < 1800)
#include <stdint.h>
#else
#include <inttypes.h>
#endif
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#ifndef DEDICATED
#include "SDL.h"
#endif
#include "../qcommon/qcommon.h"
#include "../sys/sys_local.h"
#include "../sys/sys_public.h"
#include "con_local.h"

cvar_t *com_minimized;
cvar_t *com_unfocused;
cvar_t *com_maxfps;
cvar_t *com_maxfpsMinimized;
cvar_t *com_maxfpsUnfocused;

static volatile sig_atomic_t sys_signal = 0;

/*
==================
Sys_GetClipboardData
==================
*/
char *Sys_GetClipboardData(void) {
#ifdef DEDICATED
	return NULL;
#else
	if (!SDL_HasClipboardText())
		return NULL;

	char *cbText = SDL_GetClipboardText();
	size_t len = strlen(cbText) + 1;

	char *buf = (char *)Z_Malloc(len, TAG_CLIPBOARD);
	Q_strncpyz(buf, cbText, len);

	SDL_free(cbText);
	return buf;
#endif
}

/*
=================
Sys_ConsoleInput

Handle new console input
=================
*/
char *Sys_ConsoleInput(void) {
	return CON_Input();
}

void Sys_Print(const char *msg, qboolean extendedColors) {
	// TTimo - prefix for text that shows up in console but not in notify
	// backported from RTCW
	if (!Q_strncmp(msg, "[skipnotify]", 12)) {
		msg += 12;
	}
	if (msg[0] == '*') {
		msg += 1;
	}
	ConsoleLogAppend(msg);
	CON_Print(msg, (extendedColors ? true : false));
}

/*
================
Sys_Init

Called after the common systems (cvars, files, etc)
are initialized
================
*/
void Sys_Init(void) {
	Cmd_AddCommand("in_restart", IN_Restart);
	Cvar_Get("arch", ARCH_STRING, CVAR_ROM);
	Cvar_Get("username", Sys_GetCurrentUser(), CVAR_ROM);

	com_unfocused = Cvar_Get("com_unfocused", "0", CVAR_ROM);
	com_minimized = Cvar_Get("com_minimized", "0", CVAR_ROM);
	com_maxfps = Cvar_Get("com_maxfps", "125", CVAR_ARCHIVE | CVAR_GLOBAL);
	com_maxfpsUnfocused = Cvar_Get("com_maxfpsUnfocused", "0", CVAR_ARCHIVE | CVAR_GLOBAL);
	com_maxfpsMinimized = Cvar_Get("com_maxfpsMinimized", "50", CVAR_ARCHIVE | CVAR_GLOBAL);
}

static void Q_NORETURN Sys_Exit(int ex) {
	IN_Shutdown();
#ifndef DEDICATED
	SDL_Quit();
#endif

	NET_Shutdown();

	Sys_PlatformExit();

	Com_ShutdownZoneMemory();

	CON_Shutdown();

	exit(ex);
}

#if !defined(DEDICATED)
static void Sys_ErrorDialog(const char *error) {
	time_t rawtime;
	char timeStr[32] = {}; // should really only reach ~19 chars
	char crashLogPath[MAX_OSPATH];

	time(&rawtime);
	strftime(timeStr, sizeof(timeStr), "%Y-%m-%d_%H-%M-%S", localtime(&rawtime)); // or gmtime
	Com_sprintf(crashLogPath, sizeof(crashLogPath),
		"%s%cerrorlog-%s.txt",
		Sys_DefaultHomePath(), PATH_SEP, timeStr);

	Sys_Mkdir(Sys_DefaultHomePath());

	FILE *fp = fopen(crashLogPath, "w");
	if (fp) {
		ConsoleLogWriteOut(fp);
		fclose(fp);

		const char *errorMessage = va("%s\n\nThe error log was written to %s", error, crashLogPath);
		if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", errorMessage, NULL) < 0) {
			fprintf(stderr, "%s", errorMessage);
		}
	} else {
		// Getting pretty desperate now
		ConsoleLogWriteOut(stderr);
		fflush(stderr);

		const char *errorMessage = va("%s\nCould not write the error log file, but we printed it to stderr.\n"
			"Try running the game using a command line interface.", error);
		if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", errorMessage, NULL) < 0) {
			// We really have hit rock bottom here :(
			fprintf(stderr, "%s", errorMessage);
		}
	}
}
#endif

void Q_NORETURN QDECL Sys_Error(const char *error, ...) {
	va_list argptr;
	char    string[1024];

	va_start(argptr, error);
	Q_vsnprintf(string, sizeof(string), error, argptr);
	va_end(argptr);

	Sys_Print(string,qfalse);
	Sys_PrintBacktrace();

	// Only print Sys_ErrorDialog for client binary. The dedicated
	// server binary is meant to be a command line program so you would
	// expect to see the error printed.
#if !defined(DEDICATED)
	Sys_ErrorDialog(string);
#endif

	Sys_Exit(3);
}

void Q_NORETURN Sys_Quit(void) {
	Sys_Exit(0);
}

/*
============
Sys_FileTime

returns -1 if not present
============
*/
time_t Sys_FileTime(const char *path) {
	struct stat buf;

	if (stat(path, &buf) == -1)
		return -1;

	return buf.st_mtime;
}

/*
=================
Sys_SigHandler
=================
*/
void Sys_SigHandler(int signal) {
	sys_signal = signal;
}

#if defined(_MSC_VER) && !defined(_DEBUG)
LONG WINAPI Sys_NoteException(EXCEPTION_POINTERS* pExp, DWORD dwExpCode);
void Sys_WriteCrashlog();
#endif

int main(int argc, char* argv[]) {
	int		i;
	char	commandLine[MAX_STRING_CHARS] = { 0 };
	int		missingFuncs = Sys_FindFunctions();

#if defined(_MSC_VER) && !defined(_DEBUG)
	__try {
#endif

	Sys_PlatformInit(argc, argv);

#if defined(_DEBUG) && !defined(DEDICATED)
	CON_CreateConsoleWindow();
#endif
	CON_Init();

	// get the initial time base
	Sys_Milliseconds();

#ifdef MACOS_X
	// This is passed if we are launched by double-clicking
	if (argc >= 2 && Q_strncmp(argv[1], "-psn", 4) == 0)
		argc = 1;
#endif

#if defined(DEBUG_SDL) && !defined(DEDICATED)
	SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);
#endif

	// Concatenate the command line for passing to Com_Init
	for (i = 1; i < argc; i++) {
		const bool containsSpaces = (strchr(argv[i], ' ') != NULL);
		if (containsSpaces)
			Q_strcat(commandLine, sizeof(commandLine), "\"");

		Q_strcat(commandLine, sizeof(commandLine), argv[i]);

		if (containsSpaces)
			Q_strcat(commandLine, sizeof(commandLine), "\"");

		Q_strcat(commandLine, sizeof(commandLine), " ");
	}

	Com_Init(commandLine);

	if ( missingFuncs ) {
		static const char *missingFuncsError =
			"Your system is missing functions this application relies on.\n"
			"\n"
			"Some features may be unavailable or their behavior may be incorrect.";

		// Set the error cvar (the main menu should pick this up and display an error box to the user)
		Cvar_Get( "com_errorMessage", missingFuncsError, CVAR_ROM );
		Cvar_Set( "com_errorMessage", missingFuncsError );

		// Print the error into the console, because we can't always display the main menu (dedicated servers, ...)
		Com_Printf( "********************\n" );
		Com_Printf( "ERROR: %s\n", missingFuncsError );
		Com_Printf( "********************\n" );
	}

	NET_Init();

	// main game loop
	while (!sys_signal) {
		if (com_busyWait->integer) {
			bool shouldSleep = false;

			if (com_dedicated->integer) {
				shouldSleep = true;
			}

			if (com_minimized->integer) {
				shouldSleep = true;
			}

			if (shouldSleep) {
				Sys_Sleep(5);
			}
		}

		// run the game
		Com_Frame();
	}

	Com_Quit(sys_signal);

#if defined(_MSC_VER) && !defined(_DEBUG)
	} __except(Sys_NoteException(GetExceptionInformation(), GetExceptionCode())) {
		Sys_WriteCrashlog();
		return 1;
	}
#endif

	// never gets here
	return 0;
}
