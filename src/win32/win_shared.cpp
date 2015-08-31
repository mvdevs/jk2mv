#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "win_local.h"
#include <lmerr.h>
#include <lmcons.h>
#include <lmwksta.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <direct.h>
#include <io.h>
#include <conio.h>
#include <xmmintrin.h>
#include <Shlobj.h>

/*
================
Sys_Milliseconds
================
*/
int			sys_timeBase;
int Sys_Milliseconds (void)
{
	int			sys_curtime;
	static qboolean	initialized = qfalse;

	if (!initialized) {
		sys_timeBase = timeGetTime();
		initialized = qtrue;
	}
	sys_curtime = timeGetTime() - sys_timeBase;

	return sys_curtime;
}

//============================================

char *Sys_GetCurrentUser( void )
{
	static char s_userName[1024];
	unsigned long size = sizeof( s_userName );


	if ( !GetUserNameA( s_userName, &size ) )
		strcpy( s_userName, "player" );

	if ( !s_userName[0] )
	{
		strcpy( s_userName, "player" );
	}

	return s_userName;
}

char *Sys_DefaultHomePath(void) {
#if !defined(DEDICATED) && !defined(PORTABLE)
	char szPath[MAX_PATH];
	static char homePath[MAX_OSPATH];

	if (!SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, szPath))) {
		Com_Printf("Unable to detect CSIDL_PERSONAL\n");
		return NULL;
	}

	Com_sprintf(homePath, sizeof(homePath), "%s%cjk2mv", szPath, PATH_SEP);
	return homePath;
#else
	return NULL;
#endif
}

// read the path from the registry on windows... steam also sets it, but with "InstallPath" instead of "Install Path"
char *Sys_DefaultAssetsPath() {
#ifndef DEDICATED
	HKEY hKey;
	static char installPath[MAX_OSPATH];
	DWORD installPathSize;

	// force 32bit registry
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\LucasArts Entertainment Company LLC\\Star Wars JK II Jedi Outcast\\1.0",
		0, KEY_WOW64_32KEY|KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
		return NULL;
	}

	installPathSize = sizeof(installPath);
	if (RegQueryValueExA(hKey, "Install Path", NULL, NULL, (LPBYTE)installPath, &installPathSize) != ERROR_SUCCESS) {
		if (RegQueryValueExA(hKey, "InstallPath", NULL, NULL, (LPBYTE)installPath, &installPathSize) != ERROR_SUCCESS) {
			RegCloseKey(hKey);
			return NULL;
		}
	}

	RegCloseKey(hKey);
	Q_strcat(installPath, sizeof(installPath), "\\GameData");
	return installPath;
#else
	return NULL;
#endif
}

char *Sys_DefaultInstallPath(void)
{
	return Sys_Cwd();
}

int Sys_GetPhysicalMemory( void )
{
   MEMORYSTATUS	MemoryStatus;

   memset( &MemoryStatus, sizeof(MEMORYSTATUS), 0 );
   MemoryStatus.dwLength = sizeof(MEMORYSTATUS);

   GlobalMemoryStatus( &MemoryStatus );

   return( (int)(MemoryStatus.dwTotalPhys / (1024 * 1024)) + 1 );

}

mvmutex_t MV_CreateMutex() {
	return (mvmutex_t)CreateMutex(NULL, FALSE, NULL);
}

void MV_DestroyMutex(mvmutex_t mutex) {
	if (!mutex) {
		return;
	}

	CloseHandle((HANDLE)mutex);
}

void MV_LockMutex(mvmutex_t mutex) {
	if (!mutex) {
		return;
	}

	WaitForSingleObject((HANDLE)mutex, INFINITE);
}

void MV_ReleaseMutex(mvmutex_t mutex) {
	if (!mutex) {
		return;
	}

	ReleaseMutex((HANDLE)mutex);
}

void MV_StartThread(void *addr) {
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)addr, NULL, 0, NULL);
}

void MV_MSleep(unsigned int msec) {
	Sleep((DWORD)msec);
}

#define FPUCWMASK1 (_MCW_RC | _MCW_EM)
#define FPUCW (_RC_NEAR | _MCW_EM | _PC_53)

#if idx64
#define FPUCWMASK	(FPUCWMASK1)
#else
#define FPUCWMASK	(FPUCWMASK1 | _MCW_PC)
#endif

void Sys_SetFloatEnv(void) {
	_controlfp(FPUCW, FPUCWMASK);
}

void Sys_Sleep(int msec)
{
	if (msec == 0)
		return;

#ifdef DEDICATED
	if (msec < 0)
		WaitForSingleObject(GetStdHandle(STD_INPUT_HANDLE), INFINITE);
	else
		WaitForSingleObject(GetStdHandle(STD_INPUT_HANDLE), msec);
#else
	// Client Sys_Sleep doesn't support waiting on stdin
	if (msec < 0)
		return;

	Sleep(msec);
#endif
}

/*
==============================================================

DIRECTORY SCANNING

==============================================================
*/

#define	MAX_FOUND_FILES	0x1000

void Sys_ListFilteredFiles(const char *basedir, char *subdirs, char *filter, char **psList, int *numfiles) {
	char		search[MAX_OSPATH], newsubdirs[MAX_OSPATH];
	char		filename[MAX_OSPATH];
	intptr_t	findhandle;
	struct _finddata_t findinfo;

	if (*numfiles >= MAX_FOUND_FILES - 1) {
		return;
	}

	if (strlen(subdirs)) {
		Com_sprintf(search, sizeof(search), "%s\\%s\\*", basedir, subdirs);
	} else {
		Com_sprintf(search, sizeof(search), "%s\\*", basedir);
	}

	findhandle = _findfirst(search, &findinfo);
	if (findhandle == -1) {
		return;
	}

	do {
		if (findinfo.attrib & _A_SUBDIR) {
			if (Q_stricmp(findinfo.name, ".") && Q_stricmp(findinfo.name, "..")) {
				if (strlen(subdirs)) {
					Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s\\%s", subdirs, findinfo.name);
				} else {
					Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s", findinfo.name);
				}
				Sys_ListFilteredFiles(basedir, newsubdirs, filter, psList, numfiles);
			}
		}
		if (*numfiles >= MAX_FOUND_FILES - 1) {
			break;
		}
		Com_sprintf(filename, sizeof(filename), "%s\\%s", subdirs, findinfo.name);
		if (!Com_FilterPath(filter, filename, qfalse))
			continue;
		psList[*numfiles] = CopyString(filename);
		(*numfiles)++;
	} while (_findnext(findhandle, &findinfo) != -1);

	_findclose(findhandle);
}

static qboolean strgtr(const char *s0, const char *s1) {
	int l0, l1, i;

	l0 = (int)strlen(s0);
	l1 = (int)strlen(s1);

	if (l1<l0) {
		l0 = l1;
	}

	for (i = 0; i<l0; i++) {
		if (s1[i] > s0[i]) {
			return qtrue;
		}
		if (s1[i] < s0[i]) {
			return qfalse;
		}
	}
	return qfalse;
}

char **Sys_ListFiles(const char *directory, const char *extension, char *filter, int *numfiles, qboolean wantsubs) {
	char		search[MAX_OSPATH];
	int			nfiles;
	char		**listCopy;
	char		*list[MAX_FOUND_FILES];
	struct _finddata_t findinfo;
	intptr_t	findhandle;
	int			flag;
	int			i;

	if (filter) {

		nfiles = 0;
		Sys_ListFilteredFiles(directory, "", filter, list, &nfiles);

		list[nfiles] = 0;
		*numfiles = nfiles;

		if (!nfiles)
			return NULL;

		listCopy = (char **)Z_Malloc((nfiles + 1) * sizeof(*listCopy), TAG_LISTFILES);
		for (i = 0; i < nfiles; i++) {
			listCopy[i] = list[i];
		}
		listCopy[i] = NULL;

		return listCopy;
	}

	if (!extension) {
		extension = "";
	}

	// passing a slash as extension will find directories
	if (extension[0] == '/' && extension[1] == 0) {
		extension = "";
		flag = 0;
	} else {
		flag = _A_SUBDIR;
	}

	Com_sprintf(search, sizeof(search), "%s\\*%s", directory, extension);

	// search
	nfiles = 0;

	findhandle = _findfirst(search, &findinfo);
	if (findhandle == -1) {
		*numfiles = 0;
		return NULL;
	}

	do {
		if ((!wantsubs && flag ^ (findinfo.attrib & _A_SUBDIR)) || (wantsubs && findinfo.attrib & _A_SUBDIR)) {
			if (nfiles == MAX_FOUND_FILES - 1) {
				break;
			}
			list[nfiles] = CopyString(findinfo.name);
			nfiles++;
		}
	} while (_findnext(findhandle, &findinfo) != -1);

	list[nfiles] = 0;

	_findclose(findhandle);

	// return a copy of the list
	*numfiles = nfiles;

	if (!nfiles) {
		return NULL;
	}

	listCopy = (char **)Z_Malloc((nfiles + 1) * sizeof(*listCopy), TAG_LISTFILES);
	for (i = 0; i < nfiles; i++) {
		listCopy[i] = list[i];
	}
	listCopy[i] = NULL;

	do {
		flag = 0;
		for (i = 1; i<nfiles; i++) {
			if (strgtr(listCopy[i - 1], listCopy[i])) {
				char *temp = listCopy[i];
				listCopy[i] = listCopy[i - 1];
				listCopy[i - 1] = temp;
				flag = 1;
			}
		}
	} while (flag);

	return listCopy;
}

void	Sys_FreeFileList(char **psList) {
	int		i;

	if (!psList) {
		return;
	}

	for (i = 0; psList[i]; i++) {
		Z_Free(psList[i]);
	}

	Z_Free(psList);
}

/*
==============
Sys_Cwd
==============
*/
char *Sys_Cwd(void) {
	static char cwd[MAX_OSPATH];

	_getcwd(cwd, sizeof(cwd) - 1);
	cwd[MAX_OSPATH - 1] = 0;

	return cwd;
}

/*
==============
Sys_DefaultCDPath
==============
*/
char *Sys_DefaultCDPath(void) {
	return "";
}

void Sys_InitStreamThread(void) {
}

void Sys_ShutdownStreamThread(void) {
}

void Sys_BeginStreamedFile(fileHandle_t f, int readAhead) {
}

void Sys_EndStreamedFile(fileHandle_t f) {
}

int Sys_StreamedRead(void *buffer, int size, int count, fileHandle_t f) {
	return FS_Read(buffer, size * count, f);
}

void Sys_StreamSeek(fileHandle_t f, int offset, int origin) {
	FS_Seek(f, offset, origin);
}

/*
=================
Sys_UnloadDll

=================
*/
void Sys_UnloadDll(void *dllHandle) {
	if (!dllHandle) {
		return;
	}
	if (!FreeLibrary((struct HINSTANCE__ *)dllHandle)) {
		Com_Error(ERR_FATAL, "Sys_UnloadDll FreeLibrary failed");
	}
}

/*
=================
Sys_LoadDll

Used to load a development dll instead of a virtual machine
=================
*/
extern char		*FS_BuildOSPath(const char *base, const char *game, const char *qpath);

void * QDECL Sys_LoadDll(const char *name, intptr_t(QDECL **entryPoint)(int, ...), intptr_t(QDECL *systemcalls)(intptr_t, ...)) {
	static int	lastWarning = 0;
	HINSTANCE	libHandle;
	void	(QDECL *dllEntry)(intptr_t(QDECL *syscallptr)(intptr_t, ...));
	char	*basepath;
	char	*cdpath;
	char	*gamedir;
	char	*fn;
#ifdef NDEBUG
	int		timestamp;
	int   ret;
#endif
	char	filename[MAX_QPATH];

#ifndef _WIN64
	Com_sprintf(filename, sizeof(filename), "%s_x86.dll", name);
#else
	Com_sprintf(filename, sizeof(filename), "%s_x64.dll", name);
#endif

#ifdef INSTALLED
	if (!strcmp(name, "jk2mvmenu")) {
		Com_sprintf(filename, sizeof(filename), "%s.dll", name);
	}
#endif

#ifdef NDEBUG
	timestamp = Sys_Milliseconds();
	if (((timestamp - lastWarning) > (5 * 60000)) && !Cvar_VariableIntegerValue("dedicated")
		&& !Cvar_VariableIntegerValue("com_blindlyLoadDLLs")) {
		if (FS_FileExists(filename)) {
			lastWarning = timestamp;
			ret = MessageBoxExA(NULL, "You are about to load a .DLL executable that\n"
				"has not been verified for use with Quake III Arena.\n"
				"This type of file can compromise the security of\n"
				"your computer.\n\n"
				"Select 'OK' if you choose to load it anyway.",
				"Security Warning", MB_OKCANCEL | MB_ICONEXCLAMATION | MB_DEFBUTTON2 | MB_TOPMOST | MB_SETFOREGROUND,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT));
			if (ret != IDOK) {
				return NULL;
			}
		}
	}
#endif


	// rjr disable for final release #ifndef NDEBUG
	libHandle = LoadLibraryA(filename);
	if (!libHandle) {
		//#endif
		basepath = Cvar_VariableString("fs_basepath");
		cdpath = Cvar_VariableString("fs_cdpath");
		gamedir = Cvar_VariableString("fs_game");

		fn = FS_BuildOSPath(basepath, gamedir, filename);
		libHandle = LoadLibraryA(fn);

		if (!libHandle) {
			if (cdpath[0]) {
				fn = FS_BuildOSPath(cdpath, gamedir, filename);
				libHandle = LoadLibraryA(fn);
			}

			if (!libHandle) {
				return NULL;
			}
		}
		//#ifndef NDEBUG
	}
	//#endif

	dllEntry = (void (QDECL *)(intptr_t(QDECL *)(intptr_t, ...)))GetProcAddress(libHandle, "dllEntry");
	*entryPoint = (intptr_t(QDECL *)(int, ...))GetProcAddress(libHandle, "vmMain");
	if (!*entryPoint || !dllEntry) {
		FreeLibrary(libHandle);
		return NULL;
	}
	dllEntry(systemcalls);

	return libHandle;
}

/*
==============
Sys_Mkdir
==============
*/
void Sys_Mkdir(const char *path) {
	_mkdir(path);
}

static UINT timerResolution = 0;

void Sys_PlatformInit(void) {
	TIMECAPS ptc;
	if (timeGetDevCaps(&ptc, sizeof(ptc)) == MMSYSERR_NOERROR)
	{
		timerResolution = ptc.wPeriodMin;

		if (timerResolution > 1)
		{
			Com_Printf("Warning: Minimum supported timer resolution is %ums "
				"on this system, recommended resolution 1ms\n", timerResolution);
		}

		timeBeginPeriod(timerResolution);
	} else
		timerResolution = 0;
}

void Sys_PlatformExit(void)
{
	if (timerResolution)
		timeEndPeriod(timerResolution);
}
