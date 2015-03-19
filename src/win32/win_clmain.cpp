// win_main.c

#include "../client/client.h"
#include "../qcommon/qcommon.h"
#include "win_local.h"
#include "resource.h"
#include <errno.h>
#include <float.h>
#include <fcntl.h>
#include <stdio.h>
#include <vector>
#include <direct.h>
#include <io.h>
#include <conio.h>
#include "../qcommon/strip.h"

#define	CD_BASEDIR	"gamedata\\gamedata"
#define	CD_EXE		"jk2sp.exe"
#define	CD_BASEDIR_LINUX	"bin\\x86\\glibc-2.1"
#define	CD_EXE_LINUX "jk2sp"
#define MEM_THRESHOLD 96*1024*1024

static char		sys_cmdline[MAX_STRING_CHARS];

static int	sys_monkeySpank;
static int	sys_checksum;


static unsigned	busyCount = 0;
static bool		otherTasksRunning = false;
unsigned		otherTaskTime = 0;

#pragma optimize("", off)

void busyFunction(void)
{
	float	a = MEM_THRESHOLD;
	float	b = 9343;
	short	c = 4;

	while ((int)b > (float)(b - 1))
	{
		a = a + b / c;
		busyCount++;
	}
}

void CheckProcessTime(void)
{
	HANDLE		threadHandle;
	int			threadId;
	FILETIME	creationTime;		// thread creation time
	FILETIME	exitTime;			// thread exit time
	FILETIME	kernelTime;			// thread kernel-mode time
	FILETIME	userTime;			// thread user-mode time
	//	char		temp[1024];

	busyCount = 0;
	threadHandle = CreateThread(
		NULL,	// LPSECURITY_ATTRIBUTES lpsa,
		0,		// DWORD cbStack,
		(LPTHREAD_START_ROUTINE)busyFunction,	// LPTHREAD_START_ROUTINE lpStartAddr,
		0,			// LPVOID lpvThreadParm,
		CREATE_SUSPENDED,			//   DWORD fdwCreate,
		(unsigned long *)&threadId);

	SetThreadPriority(threadHandle, THREAD_PRIORITY_IDLE);
	ResumeThread(threadHandle);
	while (busyCount < 10)
	{
		Sleep(100);
	}
	Sleep(1000);

	TerminateThread(threadHandle, 0);
	GetThreadTimes(threadHandle, &creationTime, &exitTime, &kernelTime, &userTime);
	CloseHandle(threadHandle);

	//	sprintf(temp, "Time = %u\n", userTime.dwLowDateTime);
	//	OutputDebugString(temp);

	otherTaskTime = userTime.dwLowDateTime;
	if (userTime.dwLowDateTime < 7500000)
	{
		otherTasksRunning = true;
		//		OutputDebugString("WARNING: possibly running on a system with another task\n");
	}
}

#pragma optimize("", on)



/*
==================
Sys_LowPhysicalMemory()
==================
*/

qboolean Sys_LowPhysicalMemory() {
	MEMORYSTATUS stat;
	GlobalMemoryStatus(&stat);
	return (stat.dwTotalPhys <= MEM_THRESHOLD) ? qtrue : qfalse;
}

/*
==================
Sys_BeginProfiling
==================
*/
void Sys_BeginProfiling(void) {
	// this is just used on the mac build
}

/*
=============
Sys_Error

Show the early console as an error dialog
=============
*/
void QDECL Sys_Error(const char *error, ...) {
	va_list		argptr;
	char		text[4096];
	MSG        msg;

	va_start(argptr, error);
	vsprintf(text, error, argptr);
	va_end(argptr);

	Conbuf_AppendText(text);
	Conbuf_AppendText("\n");

	Sys_SetErrorText(text);
	Sys_ShowConsole(1, qtrue);

	timeEndPeriod(1);

	IN_Shutdown();

	// wait for the user to quit
	while (1) {
		if (!GetMessage(&msg, NULL, 0, 0))
			Com_Quit_f();
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	Sys_DestroyConsole();
	Com_ShutdownZoneMemory();
	Com_ShutdownHunkMemory();

	exit(1);
}

/*
==============
Sys_Quit
==============
*/
void Sys_Quit(void) {
	timeEndPeriod(1);
	IN_Shutdown();
	Sys_DestroyConsole();
	Com_ShutdownZoneMemory();
	Com_ShutdownHunkMemory();

	exit(0);
}

/*
==============
Sys_Print
==============
*/
void Sys_Print(const char *msg) {
	Conbuf_AppendText(msg);
}

/*
==============
Sys_DefaultBasePath
==============
*/
char *Sys_DefaultBasePath(void) {
	return Sys_Cwd();
}


/*
================
Sys_GetClipboardData

================
*/
char *Sys_GetClipboardData(void) {
	char *data = NULL;
	char *cliptext;

	if (OpenClipboard(NULL) != 0) {
		HANDLE hClipboardData;

		if ((hClipboardData = GetClipboardData(CF_TEXT)) != 0) {
			if ((cliptext = (char *)GlobalLock(hClipboardData)) != 0) {
				data = (char *)Z_Malloc(GlobalSize(hClipboardData) + 1, TAG_CLIPBOARD);
				Q_strncpyz(data, cliptext, GlobalSize(hClipboardData) + 1);
				GlobalUnlock(hClipboardData);

				strtok(data, "\n\r\b");
			}
		}
		CloseClipboard();
	}
	return data;
}


/*
========================================================================

LOAD/UNLOAD DLL

========================================================================
*/




/*
========================================================================

BACKGROUND FILE STREAMING

========================================================================
*/

#if 1




#else

typedef struct {
	fileHandle_t	file;
	byte	*buffer;
	qboolean	eof;
	qboolean	active;
	int		bufferSize;
	int		streamPosition;	// next byte to be returned by Sys_StreamRead
	int		threadPosition;	// next byte to be read from file
} streamsIO_t;

typedef struct {
	HANDLE				threadHandle;
	int					threadId;
	CRITICAL_SECTION	crit;
	streamsIO_t			sIO[MAX_FILE_HANDLES];
} streamState_t;

streamState_t	stream;

/*
===============
Sys_StreamThread

A thread will be sitting in this loop forever
================
*/
void Sys_StreamThread(void) {
	int		buffer;
	int		count;
	int		readCount;
	int		bufferPoint;
	int		r, i;

	while (1) {
		Sleep(10);
		//		EnterCriticalSection (&stream.crit);

		for (i = 1; i<MAX_FILE_HANDLES; i++) {
			// if there is any space left in the buffer, fill it up
			if (stream.sIO[i].active  && !stream.sIO[i].eof) {
				count = stream.sIO[i].bufferSize - (stream.sIO[i].threadPosition - stream.sIO[i].streamPosition);
				if (!count) {
					continue;
				}

				bufferPoint = stream.sIO[i].threadPosition % stream.sIO[i].bufferSize;
				buffer = stream.sIO[i].bufferSize - bufferPoint;
				readCount = buffer < count ? buffer : count;

				r = FS_Read(stream.sIO[i].buffer + bufferPoint, readCount, stream.sIO[i].file);
				stream.sIO[i].threadPosition += r;

				if (r != readCount) {
					stream.sIO[i].eof = qtrue;
				}
			}
		}
		//		LeaveCriticalSection (&stream.crit);
	}
}

/*
===============
Sys_InitStreamThread

================
*/
void Sys_InitStreamThread(void) {
	int i;

	InitializeCriticalSection(&stream.crit);

	// don't leave the critical section until there is a
	// valid file to stream, which will cause the StreamThread
	// to sleep without any overhead
	//	EnterCriticalSection( &stream.crit );

	stream.threadHandle = CreateThread(
		NULL,	// LPSECURITY_ATTRIBUTES lpsa,
		0,		// DWORD cbStack,
		(LPTHREAD_START_ROUTINE)Sys_StreamThread,	// LPTHREAD_START_ROUTINE lpStartAddr,
		0,			// LPVOID lpvThreadParm,
		0,			//   DWORD fdwCreate,
		&stream.threadId);
	for (i = 0; i<MAX_FILE_HANDLES; i++) {
		stream.sIO[i].active = qfalse;
	}
}

/*
===============
Sys_ShutdownStreamThread

================
*/
void Sys_ShutdownStreamThread(void) {
}


/*
===============
Sys_BeginStreamedFile

================
*/
void Sys_BeginStreamedFile(fileHandle_t f, int readAhead) {
	if (stream.sIO[f].file) {
		Sys_EndStreamedFile(stream.sIO[f].file);
	}

	stream.sIO[f].file = f;
	stream.sIO[f].buffer = Z_Malloc(readAhead);
	stream.sIO[f].bufferSize = readAhead;
	stream.sIO[f].streamPosition = 0;
	stream.sIO[f].threadPosition = 0;
	stream.sIO[f].eof = qfalse;
	stream.sIO[f].active = qtrue;

	// let the thread start running
	//	LeaveCriticalSection( &stream.crit );
}

/*
===============
Sys_EndStreamedFile

================
*/
void Sys_EndStreamedFile(fileHandle_t f) {
	if (f != stream.sIO[f].file) {
		Com_Error(ERR_FATAL, "Sys_EndStreamedFile: wrong file");
	}
	// don't leave critical section until another stream is started
	EnterCriticalSection(&stream.crit);

	stream.sIO[f].file = 0;
	stream.sIO[f].active = qfalse;

	Z_Free(stream.sIO[f].buffer);

	LeaveCriticalSection(&stream.crit);
}


/*
===============
Sys_StreamedRead

================
*/
int Sys_StreamedRead(void *buffer, int size, int count, fileHandle_t f) {
	int		available;
	int		remaining;
	int		sleepCount;
	int		copy;
	int		bufferCount;
	int		bufferPoint;
	byte	*dest;

	if (stream.sIO[f].active == qfalse) {
		Com_Error(ERR_FATAL, "Streamed read with non-streaming file");
	}

	dest = (byte *)buffer;
	remaining = size * count;

	if (remaining <= 0) {
		Com_Error(ERR_FATAL, "Streamed read with non-positive size");
	}

	sleepCount = 0;
	while (remaining > 0) {
		available = stream.sIO[f].threadPosition - stream.sIO[f].streamPosition;
		if (!available) {
			if (stream.sIO[f].eof) {
				break;
			}
			if (sleepCount == 1) {
				Com_DPrintf("Sys_StreamedRead: waiting\n");
			}
			if (++sleepCount > 100) {
				Com_Error(ERR_FATAL, "Sys_StreamedRead: thread has died");
			}
			Sleep(10);
			continue;
		}

		EnterCriticalSection(&stream.crit);

		bufferPoint = stream.sIO[f].streamPosition % stream.sIO[f].bufferSize;
		bufferCount = stream.sIO[f].bufferSize - bufferPoint;

		copy = available < bufferCount ? available : bufferCount;
		if (copy > remaining) {
			copy = remaining;
		}
		memcpy(dest, stream.sIO[f].buffer + bufferPoint, copy);
		stream.sIO[f].streamPosition += copy;
		dest += copy;
		remaining -= copy;

		LeaveCriticalSection(&stream.crit);
	}

	return (count * size - remaining) / size;
}

/*
===============
Sys_StreamSeek

================
*/
void Sys_StreamSeek(fileHandle_t f, int offset, int origin) {

	// halt the thread
	EnterCriticalSection(&stream.crit);

	// clear to that point
	FS_Seek(f, offset, origin);
	stream.sIO[f].streamPosition = 0;
	stream.sIO[f].threadPosition = 0;
	stream.sIO[f].eof = qfalse;

	// let the thread start running at the new position
	LeaveCriticalSection(&stream.crit);
}

#endif

/*
========================================================================

EVENT LOOP

========================================================================
*/

#define	MAX_QUED_EVENTS		256
#define	MASK_QUED_EVENTS	( MAX_QUED_EVENTS - 1 )

sysEvent_t	eventQue[MAX_QUED_EVENTS];
int			eventHead, eventTail;
byte		sys_packetReceived[MAX_MSGLEN];

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
		Com_Printf("Sys_QueEvent: overflow\n");
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
================
Sys_GetEvent

================
*/
sysEvent_t Sys_GetEvent(void) {
	MSG			msg;
	sysEvent_t	ev;
	char		*s;
	msg_t		netmsg;
	netadr_t	adr;

	// return if we have data
	if (eventHead > eventTail) {
		eventTail++;
		return eventQue[(eventTail - 1) & MASK_QUED_EVENTS];
	}

	// pump the message loop
	while (PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE)) {
		if (!GetMessage(&msg, NULL, 0, 0)) {
			Com_Quit_f();
		}

		// save the msg time, because wndprocs don't have access to the timestamp
		g_wv.sysMsgTime = msg.time;

		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	// check for console commands
	s = Sys_ConsoleInput();
	if (s) {
		char	*b;
		int		len;

		len = (int)strlen(s) + 1;
		b = (char *)Z_Malloc(len, TAG_EVENT);
		Q_strncpyz(b, s, len);
		Sys_QueEvent(0, SE_CONSOLE, 0, 0, len, b);
	}

	// check for network packets
	MSG_Init(&netmsg, sys_packetReceived, sizeof(sys_packetReceived));
	if (Sys_GetPacket(&adr, &netmsg)) {
		netadr_t		*buf;
		int				len;

		// copy out to a seperate buffer for qeueing
		// the readcount stepahead is for SOCKS support
		len = sizeof(netadr_t) + netmsg.cursize - netmsg.readcount;
		buf = (netadr_t *)Z_Malloc(len, TAG_EVENT, qtrue);
		*buf = adr;
		memcpy(buf + 1, &netmsg.data[netmsg.readcount], netmsg.cursize - netmsg.readcount);
		Sys_QueEvent(0, SE_PACKET, 0, 0, len, buf);
	}

	// return if we have data
	if (eventHead > eventTail) {
		eventTail++;
		return eventQue[(eventTail - 1) & MASK_QUED_EVENTS];
	}

	// create an empty event to return

	memset(&ev, 0, sizeof(ev));
	ev.evTime = timeGetTime();

	return ev;
}

//================================================================

/*
=================
Sys_In_Restart_f

Restart the input subsystem
=================
*/
void Sys_In_Restart_f(void) {
	IN_Shutdown();
	IN_Init();
}


/*
=================
Sys_Net_Restart_f

Restart the network subsystem
=================
*/
void Sys_Net_Restart_f(void) {
	NET_Restart();
}


/*
================
Sys_Init

Called after the common systems (cvars, files, etc)
are initialized
================
*/
#define OSR2_BUILD_NUMBER 1111
#define WIN98_BUILD_NUMBER 1998

void Sys_Init(void) {
	Sys_PlatformInit();

	Cmd_AddCommand("in_restart", Sys_In_Restart_f);
	Cmd_AddCommand("net_restart", Sys_Net_Restart_f);

	g_wv.osversion.dwOSVersionInfoSize = sizeof(g_wv.osversion);

	if (!GetVersionEx(&g_wv.osversion))
		Sys_Error("Couldn't get OS info");

	if (g_wv.osversion.dwMajorVersion < 4)
		Sys_Error("This game requires Windows version 4 or greater");
	if (g_wv.osversion.dwPlatformId == VER_PLATFORM_WIN32s)
		Sys_Error("This game doesn't run on Win32s");

	if (g_wv.osversion.dwPlatformId == VER_PLATFORM_WIN32_NT)
	{
		Cvar_Set("arch", "winnt");
	} else if (g_wv.osversion.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
	{
		if (LOWORD(g_wv.osversion.dwBuildNumber) >= WIN98_BUILD_NUMBER)
		{
			Cvar_Set("arch", "win98");
		} else if (LOWORD(g_wv.osversion.dwBuildNumber) >= OSR2_BUILD_NUMBER)
		{
			Cvar_Set("arch", "win95 osr2.x");
		} else
		{
			Cvar_Set("arch", "win95");
		}
	} else
	{
		Cvar_Set("arch", "unknown Windows variant");
	}

	// save out a couple things in rom cvars for the renderer to access
	Cvar_Get("win_hinstance", va("%Iu", (size_t)g_wv.hInstance), CVAR_ROM);
	Cvar_Get("win_wndproc", va("%Iu", (size_t)MainWndProc), CVAR_ROM);

	Cvar_Set("username", Sys_GetCurrentUser());
	Cvar_SetValue("sys_memory", Sys_GetPhysicalMemory());

	IN_Init();		// FIXME: not in dedicated?
}


//=======================================================================
//int	totalMsec, countMsec;

/*
==================
WinMain

==================
*/

string utf16ToUTF8(const wstring &s)
{
	const int size = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, NULL, 0, 0, NULL);

	vector<char> buf(size);
	::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &buf[0], size, 0, NULL);

	return string(&buf[0]);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
	char		cwd[MAX_OSPATH];
	//	int			startTime, endTime;

	// should never get a previous instance in Win32
	if (hPrevInstance) {
		return 0;
	}

	g_wv.hInstance = hInstance;
	wstring a(lpCmdLine);
	string b = utf16ToUTF8(a);
	Q_strncpyz(sys_cmdline, b.c_str(), sizeof(sys_cmdline));

	// done before Com/Sys_Init since we need this for error output
	Sys_CreateConsole();

	// no abort/retry/fail errors
	SetErrorMode(SEM_FAILCRITICALERRORS);

	// get the initial time base
	Sys_Milliseconds();

#if 0
	// if we find the CD, add a +set cddir xxx command line
	Sys_ScanForCD();
#endif

#if 0
	CheckProcessTime();
#endif 

	Com_Init(sys_cmdline);
#if 0
	Cvar_Set("com_othertasks", (otherTasksRunning ? "1" : "0"));
	Cvar_Set("com_othertaskstime", va("%u", otherTaskTime));
#else
	Cvar_Set("com_othertasks", "0");
#endif 

	NET_Init();

	_getcwd(cwd, sizeof(cwd));
	Com_Printf("Working directory: %s\n", cwd);

	// hide the early console since we've reached the point where we
	// have a working graphics subsystems
	if (!com_dedicated->integer && !com_viewlog->integer) {
		Sys_ShowConsole(0, qfalse);
	}

#ifdef _DEBUG
	if (sys_monkeySpank) {
		Cvar_Set("cl_trn", "666");
	}
#endif

	// main game loop
	while (1) {
		// if not running as a game client, sleep a bit
		if (g_wv.isMinimized || (com_dedicated && com_dedicated->integer)) {
			Sleep(5);
		}

		// set low precision every frame, because some system calls
		// reset it arbitrarily
		//		_controlfp( _PC_24, _MCW_PC );

		//		startTime = Sys_Milliseconds();

		// make sure mouse and joystick are only called once a frame
		IN_Frame();

		// run the game
		Com_Frame();

		//		endTime = Sys_Milliseconds();
		//		totalMsec += endTime - startTime;
		//		countMsec++;
	}

	// never gets here
}


