#include <winsock2.h>
#include <windows.h>
#include <float.h>
#include <io.h>
#include <shlobj.h>
#include <Shobjidl.h>
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

char *Sys_GetCurrentUser( void )
{
	static char s_userName[1024];
	DWORD size = sizeof( s_userName );


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

static void Sys_ListFilteredFiles(const char *basedir, char *subdirs, char *filter, const char **psList, int *numfiles) {
	char		search[MAX_OSPATH], newsubdirs[MAX_OSPATH];
	char		filename[MAX_OSPATH];
	HANDLE		findhandle;
	WIN32_FIND_DATAA findinfo;

	if (*numfiles >= MAX_FOUND_FILES - 1) {
		return;
	}

	if (strlen(subdirs)) {
		Com_sprintf(search, sizeof(search), "%s\\%s\\*", basedir, subdirs);
	} else {
		Com_sprintf(search, sizeof(search), "%s\\*", basedir);
	}

	findhandle = FindFirstFileA(search, &findinfo);
	if (findhandle == INVALID_HANDLE_VALUE) {
		return;
	}

	do {
		if (findinfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (Q_stricmp(findinfo.cFileName, ".") && Q_stricmp(findinfo.cFileName, "..")) {
				if (strlen(subdirs)) {
					Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s\\%s", subdirs, findinfo.cFileName);
				} else {
					Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s", findinfo.cFileName);
				}
				Sys_ListFilteredFiles(basedir, newsubdirs, filter, psList, numfiles);
			}
		}
		if (*numfiles >= MAX_FOUND_FILES - 1) {
			break;
		}
		Com_sprintf(filename, sizeof(filename), "%s\\%s", subdirs, findinfo.cFileName);
		if (!Com_FilterPath(filter, filename, qfalse))
			continue;
		psList[*numfiles] = CopyString(filename);
		(*numfiles)++;
	} while (FindNextFileA(findhandle, &findinfo) != 0);

	FindClose(findhandle);
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

const char **Sys_ListFiles(const char *directory, const char *extension, char *filter, int *numfiles, qboolean wantsubs) {
	char		search[MAX_OSPATH];
	int			nfiles;
	const char	**listCopy;
	const char	*list[MAX_FOUND_FILES];
	HANDLE		findhandle;
	WIN32_FIND_DATAA findinfo;
	int			flag;
	int			i;

	if (filter) {

		nfiles = 0;
		Sys_ListFilteredFiles(directory, "", filter, list, &nfiles);

		list[nfiles] = 0;
		*numfiles = nfiles;

		if (!nfiles)
			return NULL;

		listCopy = (const char **)Z_Malloc((nfiles + 1) * sizeof(*listCopy), TAG_LISTFILES);
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
		flag = FILE_ATTRIBUTE_DIRECTORY;
	}

	Com_sprintf(search, sizeof(search), "%s\\*%s", directory, extension);

	// search
	nfiles = 0;

	findhandle = FindFirstFileA(search, &findinfo);
	if (findhandle == INVALID_HANDLE_VALUE) {
		*numfiles = 0;
		return NULL;
	}

	do {
		if ((!wantsubs && flag ^ (findinfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) || (wantsubs && findinfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			if (nfiles == MAX_FOUND_FILES - 1) {
				break;
			}
			list[nfiles] = CopyString(findinfo.cFileName);
			nfiles++;
		}
	} while (FindNextFileA(findhandle, &findinfo) != 0);

	list[nfiles] = 0;

	FindClose(findhandle);

	// return a copy of the list
	*numfiles = nfiles;

	if (!nfiles) {
		return NULL;
	}

	listCopy = (const char **)Z_Malloc((nfiles + 1) * sizeof(*listCopy), TAG_LISTFILES);
	for (i = 0; i < nfiles; i++) {
		listCopy[i] = list[i];
	}
	listCopy[i] = NULL;

	do {
		flag = 0;
		for (i = 1; i<nfiles; i++) {
			if (strgtr(listCopy[i - 1], listCopy[i])) {
				const char *temp = listCopy[i];
				listCopy[i] = listCopy[i - 1];
				listCopy[i - 1] = temp;
				flag = 1;
			}
		}
	} while (flag);

	return listCopy;
}

void	Sys_FreeFileList(const char **psList) {
	int		i;

	if (!psList) {
		return;
	}

	for (i = 0; psList[i]; i++) {
		Z_Free((void *)psList[i]);
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
	GetCurrentDirectoryA(sizeof(cwd), cwd);
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
	const char	*path, *filePath;
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

ITaskbarList3 *win_taskbar;

void Sys_PlatformInit(int argc, char *argv[]) {
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

#ifndef DEDICATED
	// Win7+ Taskbar features
	CoInitialize(NULL);
	CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, IID_ITaskbarList3, (void **)&win_taskbar);
#endif
}

void Sys_SetTaskbarState(void *win_handle, tbstate_t state, uint64_t current, uint64_t total) {
	if (!win_taskbar) return;

	HWND hwnd = (HWND)win_handle;

	switch (state) {
	case TBS_NORMAL:
		win_taskbar->SetProgressState(hwnd, TBPF_NOPROGRESS);
		break;
	case TBS_ERROR:
		win_taskbar->SetProgressValue(hwnd, 100, 100);
		win_taskbar->SetProgressState(hwnd, TBPF_ERROR);
		break;
	case TBS_INDETERMINATE:
		win_taskbar->SetProgressState(hwnd, TBPF_INDETERMINATE);
		break;
	case TBS_PROGRESS:
		win_taskbar->SetProgressState(hwnd, TBPF_NORMAL);
		win_taskbar->SetProgressValue(hwnd, current, total);
		break;
	case TBS_NOTIFY:
		FlashWindow(hwnd, FALSE);
		break;
	}
}

int Sys_FLock(int fd, flockCmd_t cmd, qboolean nb) {
	HANDLE h = (HANDLE) _get_osfhandle(fd);
	OVERLAPPED ovlp = { 0 };
	DWORD lower = 1;
	DWORD upper = 0;
	DWORD flags = 0;
	int res;

	if (h == INVALID_HANDLE_VALUE) {
		return -1;
	}

	if (nb) {
		flags |= LOCKFILE_FAIL_IMMEDIATELY;
	}

	switch (cmd) {
	case FLOCK_EX:
		flags |= LOCKFILE_EXCLUSIVE_LOCK;
		// fall-through
	case FLOCK_SH:
		res = LockFileEx(h, flags, 0, lower, upper, &ovlp);
		break;
	case FLOCK_UN:
		res = UnlockFile(h, 0, 0, lower, upper);
		break;
	}

	return res ? 0 : -1;
}

void Sys_PrintBacktrace(void) {}

void Sys_PlatformExit(void)
{
	if (timerResolution)
		timeEndPeriod(timerResolution);
}


// from ioq3 requires sse
// i do not care about processors without sse
#ifdef __GNUC__
#if idx64
  #define EAX "%%rax"
  #define EBX "%%rbx"
  #define ESP "%%rsp"
  #define EDI "%%rdi"
#else
  #define EAX "%%eax"
  #define EBX "%%ebx"
  #define ESP "%%esp"
  #define EDI "%%edi"
#endif

static unsigned char ssemask[16] __attribute__((aligned(16))) = {
	"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\x00\x00"
};

void Sys_SnapVector(vec3_t vec) {
	__asm__ volatile
	(
		"movaps (%0), %%xmm1\n"
		"movups (%1), %%xmm0\n"
		"movaps %%xmm0, %%xmm2\n"
		"andps %%xmm1, %%xmm0\n"
		"andnps %%xmm2, %%xmm1\n"
		"cvtps2dq %%xmm0, %%xmm0\n"
		"cvtdq2ps %%xmm0, %%xmm0\n"
		"orps %%xmm1, %%xmm0\n"
		"movups %%xmm0, (%1)\n"
		:
		: "r" (ssemask), "r" (vec)
		: "memory", "%xmm0", "%xmm1", "%xmm2"
	);

}
#endif
