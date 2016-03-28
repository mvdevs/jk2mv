#include <windows.h>
#include <float.h>
#include <io.h>
#include <direct.h>
#include <shlobj.h>
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

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
#if !defined(PORTABLE)
	char szPath[MAX_PATH];
	static char homePath[MAX_OSPATH];

	if (!SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, szPath))) {
		Com_Printf("Unable to detect CSIDL_PERSONAL\n");
		return NULL;
	}

	Com_sprintf(homePath, sizeof(homePath), "%s%cjk2mv", szPath, PATH_SEP);
	return homePath;
#else
	return Sys_Cwd();
#endif
}

// read the path from the registry on windows... steam also sets it, but with "InstallPath" instead of "Install Path"
char *Sys_DefaultAssetsPath() {
#ifdef INSTALLED
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
	return Sys_Cwd();
#endif
}

char *Sys_DefaultInstallPath(void)
{
	return Sys_Cwd();
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

void Sys_Sleep(int msec) {
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
=================
Sys_UnloadModuleLibrary
=================
*/
void Sys_UnloadModuleLibrary(void *dllHandle) {
	if (!dllHandle) {
		return;
	}

	if (!FreeLibrary((HMODULE)dllHandle)) {
		Com_Error(ERR_FATAL, "Sys_UnloadDll FreeLibrary failed");
	}
}

/*
=================
Sys_LoadModuleLibrary

Used to load a module (jk2mpgame, cgame, ui) dll
=================
*/
void *Sys_LoadModuleLibrary(const char *name, qboolean mvOverride, intptr_t(QDECL **entryPoint)(int, ...), intptr_t(QDECL *systemcalls)(intptr_t, ...)) {
	HMODULE	libHandle;
	void	(QDECL *dllEntry)(intptr_t(QDECL *syscallptr)(intptr_t, ...));
	char	*path, *filePath;
	char	filename[MAX_QPATH];

	Com_sprintf(filename, sizeof(filename), "%s_" ARCH_STRING "." LIBRARY_EXTENSION, name);

	if (!mvOverride) {
		path = Cvar_VariableString("fs_basepath");
		filePath = FS_BuildOSPath(path, NULL, filename);

		Com_DPrintf("Loading module: %s...", filePath);
		libHandle = LoadLibraryA(filePath);
		if (!libHandle) {
			Com_DPrintf(" failed!\n");
			path = Cvar_VariableString("fs_homepath");
			filePath = FS_BuildOSPath(path, NULL, filename);

			Com_DPrintf("Loading module: %s...", filePath);
			libHandle = LoadLibraryA(filePath);
			if (!libHandle) {
				Com_DPrintf(" failed!\n");
				return NULL;
			} else {
				Com_DPrintf(" success!\n");
			}
		} else {
			Com_DPrintf(" success!\n");
		}
	} else {
		char dllPath[MAX_PATH];
		path = Cvar_VariableString("fs_basepath");
		Com_sprintf(dllPath, sizeof(dllPath), "%s\\%s", path, filename);

		Com_DPrintf("Loading module: %s...", dllPath);
		libHandle = LoadLibraryA(dllPath);
		if (!libHandle) {
			Com_DPrintf(" failed!\n");
			return NULL;
		} else {
			Com_DPrintf(" success!\n");
		}
	}

	dllEntry = (void (QDECL *)(intptr_t(QDECL *)(intptr_t, ...)))GetProcAddress(libHandle, "dllEntry");
	*entryPoint = (intptr_t(QDECL *)(int, ...))GetProcAddress(libHandle, "vmMain");
	if (!*entryPoint) {
		Com_DPrintf("Could not find vmMain in %s\n", filename);
		FreeLibrary(libHandle);
		return NULL;
	}

	if (!dllEntry) {
		Com_DPrintf("Could not find dllEntry in %s\n", filename);
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
qboolean Sys_Mkdir(const char *path) {
	if (!CreateDirectoryA(path, NULL)) {
		if (GetLastError() != ERROR_ALREADY_EXISTS)
			return qfalse;
	}
	return qtrue;
}

/*
================
Sys_Milliseconds
================
*/
int Sys_Milliseconds(bool baseTime) {
	static int sys_timeBase = timeGetTime();
	int			sys_curtime;

	sys_curtime = timeGetTime();
	if (!baseTime) {
		sys_curtime -= sys_timeBase;
	}

	return sys_curtime;
}

int Sys_Milliseconds2(void) {
	return Sys_Milliseconds(false);
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
