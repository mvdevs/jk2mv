#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <pwd.h>
#include <pthread.h>
#include <fenv.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <signal.h>

#ifdef MACOS_X
#include <mach-o/dyld.h>
#endif

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"

#include "mv_setup.h"

//=============================================================================

// Used to determine CD Path
static char cdPath[MAX_OSPATH];

// Used to determine local installation path
static char installPath[MAX_OSPATH];

// Used to determine where to store user-specific files
static char homePath[MAX_OSPATH];

/*
================
Sys_Milliseconds
================
*/
int curtime;
int	sys_timeBase;
int Sys_Milliseconds (void)
{
	struct timeval tp;
	struct timezone tzp;

	gettimeofday(&tp, &tzp);

	if (!sys_timeBase)
	{
		sys_timeBase = tp.tv_sec;
		return tp.tv_usec/1000;
	}

	curtime = (tp.tv_sec - sys_timeBase)*1000 + tp.tv_usec/1000;

	return curtime;
}

void	Sys_Mkdir( const char *path )
{
	mkdir (path, 0777);
}

char *strlwr (char *s) {
  if ( s==NULL ) { // bk001204 - paranoia
	assert(0);
	return s;
  }
  while (*s) {
	*s = tolower(*s);
	s++;
  }
  return s; // bk001204 - duh
}

//============================================

#define	MAX_FOUND_FILES	0x1000

// bk001129 - new in 1.26
void Sys_ListFilteredFiles( const char *basedir, char *subdirs, char *filter, char **list, int *numfiles ) {
	char		search[MAX_OSPATH], newsubdirs[MAX_OSPATH];
	char		filename[MAX_OSPATH];
	DIR			*fdir;
	struct dirent *d;
	struct stat st;

	if ( *numfiles >= MAX_FOUND_FILES - 1 ) {
		return;
	}

	if (strlen(subdirs)) {
		Com_sprintf( search, sizeof(search), "%s/%s", basedir, subdirs );
	}
	else {
		Com_sprintf( search, sizeof(search), "%s", basedir );
	}

	if ((fdir = opendir(search)) == NULL) {
		return;
	}

	while ((d = readdir(fdir)) != NULL) {
		Com_sprintf(filename, sizeof(filename), "%s/%s", search, d->d_name);
		if (stat(filename, &st) == -1)
			continue;

		if (st.st_mode & S_IFDIR) {
			if (Q_stricmp(d->d_name, ".") && Q_stricmp(d->d_name, "..")) {
				if (strlen(subdirs)) {
					Com_sprintf( newsubdirs, sizeof(newsubdirs), "%s/%s", subdirs, d->d_name);
				}
				else {
					Com_sprintf( newsubdirs, sizeof(newsubdirs), "%s", d->d_name);
				}
				Sys_ListFilteredFiles( basedir, newsubdirs, filter, list, numfiles );
			}
		}
		if ( *numfiles >= MAX_FOUND_FILES - 1 ) {
			break;
		}
		Com_sprintf( filename, sizeof(filename), "%s/%s", subdirs, d->d_name );
		if (!Com_FilterPath( filter, filename, qfalse ))
			continue;
		list[ *numfiles ] = CopyString( filename );
		(*numfiles)++;
	}

	closedir(fdir);
}

// bk001129 - in 1.17 this used to be
// char **Sys_ListFiles( const char *directory, const char *extension, int *numfiles, qboolean wantsubs )
char **Sys_ListFiles( const char *directory, const char *extension, char *filter, int *numfiles, qboolean wantsubs )
{
	struct dirent *d;
	// char *p; // bk001204 - unused
	DIR		*fdir;
	qboolean dironly = wantsubs;
	char		search[MAX_OSPATH];
	int			nfiles;
	char		**listCopy;
	char		*list[MAX_FOUND_FILES];
	//int			flag; // bk001204 - unused
	int			i;
	struct stat st;

	int			extLen;

	if (filter) {

		nfiles = 0;
		Sys_ListFilteredFiles( directory, "", filter, list, &nfiles );

		list[ nfiles ] = 0;
		*numfiles = nfiles;

		if (!nfiles)
			return NULL;

		listCopy = (char **)Z_Malloc( ( nfiles + 1 ) * sizeof( *listCopy ),TAG_FILESYS,qfalse );
		for ( i = 0 ; i < nfiles ; i++ ) {
			listCopy[i] = list[i];
		}
		listCopy[i] = NULL;

		return listCopy;
	}

	if ( !extension)
		extension = "";

	if ( extension[0] == '/' && extension[1] == 0 ) {
		extension = "";
		dironly = qtrue;
	}

	extLen = strlen( extension );

	// search
	nfiles = 0;

	if ((fdir = opendir(directory)) == NULL) {
		*numfiles = 0;
		return NULL;
	}

	while ((d = readdir(fdir)) != NULL) {
		Com_sprintf(search, sizeof(search), "%s/%s", directory, d->d_name);
		if (stat(search, &st) == -1)
			continue;
		if ((dironly && !(st.st_mode & S_IFDIR)) ||
			(!dironly && (st.st_mode & S_IFDIR)))
			continue;

		if (*extension) {
			if ( strlen( d->d_name ) < strlen( extension ) ||
				Q_stricmp(
					d->d_name + strlen( d->d_name ) - strlen( extension ),
					extension ) ) {
				continue; // didn't match
			}
		}

		if ( nfiles == MAX_FOUND_FILES - 1 )
			break;
		list[ nfiles ] = CopyString( d->d_name );
		nfiles++;
	}

	list[ nfiles ] = 0;

	closedir(fdir);

	// return a copy of the list
	*numfiles = nfiles;

	if ( !nfiles ) {
		return NULL;
	}

	listCopy = (char **)Z_Malloc( ( nfiles + 1 ) * sizeof( *listCopy ),TAG_FILESYS,qfalse );
	for ( i = 0 ; i < nfiles ; i++ ) {
		listCopy[i] = list[i];
	}
	listCopy[i] = NULL;

	return listCopy;
}

void	Sys_FreeFileList( char **list ) {
	int		i;

	if ( !list ) {
		return;
	}

	for ( i = 0 ; list[i] ; i++ ) {
		Z_Free( list[i] );
	}

	Z_Free( list );
}

char *Sys_Cwd( void )
{
	static char cwd[MAX_OSPATH];

	if ( !getcwd( cwd, sizeof( cwd ) - 1 ) ) {
        return "";
	}

	cwd[MAX_OSPATH-1] = 0;

	return cwd;
}

void Sys_SetDefaultCDPath(const char *path)
{
	Q_strncpyz(cdPath, path, sizeof(cdPath));
}

char *Sys_DefaultCDPath(void)
{
        return cdPath;
}

void Sys_SetDefaultInstallPath(const char *path)
{
	Q_strncpyz(installPath, path, sizeof(installPath));
}

static char *last_strstr(const char *haystack, const char *needle)
{
    if (*needle == '\0')
        return (char *) haystack;

    char *result = NULL;
    for (;;) {
        char *p = (char *)strstr(haystack, needle);
        if (p == NULL)
            break;
        result = p;
        haystack = p + 1;
    }

    return result;
}

#if !defined(MACOS_X) && !defined(DEDICATED) && defined(INSTALLED)
char *Sys_LinuxGetInstallPrefix() {
    static char path[MAX_OSPATH];
    int i;

    readlink("/proc/self/exe", path, sizeof(path));

    // from: /usr/local/bin/jk2mvmp
    // to: /usr/local
    for (i = 0; i < 2; i++) {
        char *l = strrchr(path, '/');
        if (!l) {
            break;
        }

        *l = 0;
    }

    return path;
}
#endif

char *Sys_DefaultInstallPath(void)
{
    if (*installPath) {
        return installPath;
    } else {
#if defined(MACOS_X) && !defined(DEDICATED) && defined(INSTALLED)
        // Inside the app...
        static char path[MAX_OSPATH];
        char *override;

        uint32_t size = sizeof(path);
        if (_NSGetExecutablePath(path, &size)) {
            return Sys_Cwd();
        }

        override = last_strstr(path, "MacOS");
        if (!override) {
            return Sys_Cwd();
        }

        override[5] = 0;
        return path;
#elif !defined(MACOS_X) && !defined(DEDICATED) && defined(INSTALLED)
        static char path[MAX_OSPATH];

        Com_sprintf(path, sizeof(path), "%s/share/jk2mv", Sys_LinuxGetInstallPrefix());
        return path;
#else
		return Sys_Cwd();
#endif
    }
}

void Sys_SetDefaultHomePath(const char *path)
{
	Q_strncpyz(homePath, path, sizeof(homePath));
}

// if dedicated, i think it's a good idea to default fs_homepath to fs_basepath on unix, too
// remember it's only the default value
char *Sys_DefaultHomePath(void)
{
#if !defined(DEDICATED) && defined(INSTALLED)
	char *p;

        if (*homePath)
            return homePath;

	if ((p = getenv("HOME")) != NULL) {
		Q_strncpyz(homePath, p, sizeof(homePath));
#ifdef MACOS_X
		Q_strcat(homePath, sizeof(homePath), "/Library/Application Support/jk2mv");
#else
		Q_strcat(homePath, sizeof(homePath), "/jk2mv");
#endif
		if (mkdir(homePath, 0777)) {
			if (errno != EEXIST)
				Sys_Error("Unable to create directory \"%s\", error is %s(%d)\n", homePath, strerror(errno), errno);
		}
		return homePath;
	}
	return ""; // assume current dir
#else
	return NULL;
#endif
}

// try to find assets from /Applications (Appstore JK2) or Steam
// if not found try to find it in the same directory this app is
char *Sys_DefaultAssetsPath() {
#if defined(MACOS_X) && !defined(DEDICATED)
    static char path[MAX_OSPATH];
    char *override;

    // AppStore version
    if (access("/Applications/Jedi Knight II.app/Contents/base/assets0.pk3", F_OK) != -1) {
        return "/Applications/Jedi Knight II.app/Contents";
    }

    // Steam version
    if (access(va("%s/Library/Application Support/Steam/steamapps/common/Jedi Outcast/Jedi Knight II.app/Contents/base/assets0.pk3", getenv("HOME")), F_OK) != -1) {
        Q_strncpyz(path, va("%s/Library/Application Support/Steam/steamapps/common/Jedi Outcast/Jedi Knight II.app/Contents", getenv("HOME")), sizeof(path));
        return path;
    }

    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size)) {
        return NULL;
    }

    override = last_strstr(path, ".app/");
    if (!override) {
        return NULL;
    }
    *override = 0;

    override = last_strstr(path, "/");
    if (!override) {
        return NULL;
    }

    strcpy(override, "/Jedi Knight II.app/Contents");
    if (access(va("%s/base/assets0.pk3", path), F_OK) == -1) {
        return NULL;
    }

    return path;
#else
    return NULL;
#endif
}

qboolean stdin_active = qtrue;
qboolean stdinIsATTY = qfalse;
extern void		Sys_SigHandler( int signal );

void Sys_PlatformInit( void )
{
	const char* term = getenv( "TERM" );

	signal( SIGHUP, Sys_SigHandler );
	signal( SIGQUIT, Sys_SigHandler );
	signal( SIGTRAP, Sys_SigHandler );
	signal( SIGIOT, Sys_SigHandler );
	signal( SIGBUS, Sys_SigHandler );

    if (isatty( STDIN_FILENO ) && !( term && ( !strcmp( term, "raw" ) || !strcmp( term, "dumb" ) ) ))
		stdinIsATTY = qtrue;
    else
        stdinIsATTY = qfalse;
}

void Sys_PlatformExit( void )
{
}

//============================================

int Sys_GetProcessorId( void )
{
	return CPUID_GENERIC;
}

char *Sys_GetCurrentUser( void )
{
	struct passwd *p;

	if ( (p = getpwuid( getuid() )) == NULL ) {
		return "player";
	}
	return p->pw_name;
}

typedef struct {
    pthread_mutex_t mutex;
} mv_unix_mutex;

mvmutex_t MV_CreateMutex() {
    mv_unix_mutex *mut = (mv_unix_mutex *)malloc(sizeof(mv_unix_mutex));
    pthread_mutex_init(&mut->mutex, NULL);

    return (mvmutex_t)mut;
}

void MV_DestroyMutex(mvmutex_t mutex) {
    if (!mutex) {
        return;
    }

    pthread_mutex_destroy(&((mv_unix_mutex *)mutex)->mutex);
	free(mutex);
}

void MV_LockMutex(mvmutex_t mutex) {
    if (!mutex) {
        return;
    }

	pthread_mutex_lock(&((mv_unix_mutex *)mutex)->mutex);
}

void MV_ReleaseMutex(mvmutex_t mutex) {
    if (!mutex) {
        return;
    }

	pthread_mutex_unlock(&((mv_unix_mutex *)mutex)->mutex);
}

void MV_StartThread(void *addr) {
    pthread_t th;
    pthread_create(&th, NULL, (void*(*)(void*))addr, NULL);
}

void MV_MSleep(unsigned int msec) {
	usleep(msec);
}

// from ioq3 requires sse
// i do not care about processors without sse

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

long Q_ftol(float f) {
    long retval;

    __asm__ volatile (
        "cvttss2si %1, %0\n"
        : "=r" (retval)
        : "x" (f)
    );

    return retval;
}

int Q_VMftol() {
    int retval;

    __asm__ volatile (
        "movss (" EDI ", " EBX ", 4), %%xmm0\n"
        "cvttss2si %%xmm0, %0\n"
        : "=r" (retval)
        :
        : "%xmm0"
    );

    return retval;
}

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

void Sys_SetFloatEnv(void) {
	fesetround(FE_TONEAREST);
}

/*
=================
Sys_UnloadDll

=================
*/
void Sys_UnloadDll( void *dllHandle ) {
  // bk001206 - verbose error reporting
  const char* err; // rb010123 - now const
  if ( !dllHandle ) {
    Com_Printf("Sys_UnloadDll(NULL)\n");
    return;
  }
  dlclose( dllHandle );
  err = dlerror();
  if ( err != NULL )
    Com_Printf ( "Sys_UnloadGame failed on dlclose: \"%s\"!\n", err );
}


/*
=================
Sys_LoadDll

Used to load a development dll instead of a virtual machine
=================
*/
extern char		*FS_BuildOSPath( const char *base, const char *game, const char *qpath );

void	* QDECL Sys_LoadDll(const char *name, intptr_t(QDECL **entryPoint)(int, ...), intptr_t(QDECL *systemcalls)(intptr_t, ...))
{
  void *libHandle;
  void	(*dllEntry)( intptr_t(*syscallptr)(intptr_t, ...) );
  char	curpath[MAX_OSPATH];
  char	fname[MAX_OSPATH];
  //char	loadname[MAX_OSPATH];
  char	*basepath;
  char	*cdpath;
  char	*gamedir;
  char	*fn;
  const char*  err = NULL; // bk001206 // rb0101023 - now const
  char mvmenu[MAX_OSPATH];

  // bk001206 - let's have some paranoia
  assert( name );

  if ( !getcwd(curpath, sizeof(curpath)) ) {
	return NULL;
  }

#ifdef MACOS_X
	snprintf (fname, sizeof(fname), "%s.dylib", name);
#else
#if defined __i386__
  snprintf (fname, sizeof(fname), "%s_i386.so", name);
#elif defined __powerpc__   //rcg010207 - PPC support.
  snprintf (fname, sizeof(fname), "%s_ppc.so", name);
#elif defined __axp__
  snprintf (fname, sizeof(fname), "%s_axp.so", name);
#elif defined __mips__
  snprintf (fname, sizeof(fname), "%s_mips.so", name);
#elif defined __amd64__
  snprintf (fname, sizeof(fname), "%s_amd64.so", name);
#else
#error Unknown arch
#endif
#endif

// bk001129 - was RTLD_LAZY
#define Q_RTLD	RTLD_NOW

#if 0 // bk010205 - was NDEBUG // bk001129 - FIXME: what is this good for?
  // bk001206 - do not have different behavior in builds
  Q_strncpyz(loadname, curpath, sizeof(loadname));
  // bk001129 - from cvs1.17 (mkv)
  Q_strcat(loadname, sizeof(loadname), "/");

  Q_strcat(loadname, sizeof(loadname), fname);
  Com_Printf( "Sys_LoadDll(%s)... \n", loadname );
  libHandle = dlopen( loadname, Q_RTLD );
  //if ( !libHandle ) {
  // bk001206 - report any problem
  //Com_Printf( "Sys_LoadDll(%s) failed: \"%s\"\n", loadname, dlerror() );
#endif // bk010205 - do not load from installdir

  basepath = Cvar_VariableString( "fs_basepath" );
  cdpath = Cvar_VariableString( "fs_cdpath" );
  gamedir = Cvar_VariableString( "fs_game" );

#ifndef DEDICATED
  if (!strcmp(name, "jk2mvmenu")) {
#if !defined(MACOS_X) && defined(INSTALLED)
	sprintf(mvmenu, "%s/lib/%s.so", Sys_LinuxGetInstallPrefix(), name);
	fn = mvmenu;
#else
	sprintf(mvmenu, "%s/%s", basepath, fname);
	fn = mvmenu;
#endif
  } else
#endif
  {
	fn = FS_BuildOSPath( basepath, gamedir, fname );
  }
  // bk001206 - verbose
  Com_Printf( "Sys_LoadDll(%s)... \n", fn );

  // bk001129 - from cvs1.17 (mkv), was fname not fn
  libHandle = dlopen( fn, Q_RTLD );

  if ( !libHandle )
  {
	// bk001206 - report any problem
	Com_Printf( "Sys_LoadDll(%s) failed: \"%s\"\n", fn, dlerror() );

	if( cdpath[0] )
	{
	  fn = FS_BuildOSPath( cdpath, gamedir, fname );
	  libHandle = dlopen( fn, Q_RTLD );

	  if ( !libHandle )
	  {
		// bk001206 - report any problem
		Com_Printf( "Sys_LoadDll(%s) failed: \"%s\"\n", fn, dlerror() );
	  }
	  else
	  {
		Com_Printf ( "Sys_LoadDll(%s): succeeded from cdpath ...\n", fn );
	  }
	}
	else
	{
	  Com_Printf ( "Sys_LoadDll(%s): no cdpath, giving up ...\n", fn );
	}
  }
  else
  {
    Com_Printf( "Sys_LoadDll(%s): suceeded ..\n", fn );
  }

  if ( !libHandle )
  {
#ifdef NDEBUG // bk001206 - in debug abort on failure
    Com_Error ( ERR_FATAL, "Sys_LoadDll(%s) failed dlopen() completely!\n", name  );
#else
    Com_Printf ( "Sys_LoadDll(%s) failed dlopen() completely!\n", name );
#endif
    return NULL;
  }

  // bk001206 - no different behavior
  //#ifndef NDEBUG }
  //else Com_Printf ( "Sys_LoadDll(%s): succeeded ...\n", loadname );
  //#endif

  dllEntry = (void (*)(intptr_t (*)(intptr_t,...))) dlsym( libHandle, "dllEntry" );
  *entryPoint = (intptr_t(*)(int,...))dlsym( libHandle, "vmMain" );
  if ( !*entryPoint || !dllEntry ) {
	err = dlerror();
#ifdef NDEBUG // bk001206 - in debug abort on failure
	Com_Error ( ERR_FATAL, "Sys_LoadDll(%s) failed dlsym(vmMain): \"%s\" !\n", name, err );
#else
	Com_Printf ( "Sys_LoadDll(%s) failed dlsym(vmMain): \"%s\" !\n", name, err );
#endif
	dlclose( libHandle );
	err = dlerror();
	if ( err != NULL )
	  Com_Printf ( "Sys_LoadDll(%s) failed dlcose: \"%s\"\n", name, err );
	return NULL;
  }
  Com_Printf ( "Sys_LoadDll(%s) found **vmMain** at  %p  \n", name, *entryPoint ); // bk001212
  dllEntry( systemcalls );
  Com_Printf ( "Sys_LoadDll(%s) succeeded!\n", name );
  return libHandle;
}

/*
==================
Sys_Sleep

Block execution for msec or until input is recieved.
==================
*/
void Sys_Sleep( int msec )
{
	if( msec == 0 )
		return;

	if( stdinIsATTY )
	{
		fd_set fdset;

		FD_ZERO(&fdset);
		FD_SET(STDIN_FILENO, &fdset);
		if( msec < 0 )
		{
			select(STDIN_FILENO + 1, &fdset, NULL, NULL, NULL);
		}
		else
		{
			struct timeval timeout;

			timeout.tv_sec = msec/1000;
			timeout.tv_usec = (msec%1000)*1000;
			select(STDIN_FILENO + 1, &fdset, NULL, NULL, &timeout);
		}
	}
	else
	{
		// With nothing to select() on, we can't wait indefinitely
		if( msec < 0 )
			msec = 10;

		usleep( msec * 1000 );
	}
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
