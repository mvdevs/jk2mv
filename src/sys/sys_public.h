#pragma once

#include "../cgame/tr_types.h"
#include "../qcommon/q_shared.h"

#define MAXPRINTMSG 4096

typedef enum netadrtype_s
{
	NA_BAD = 0,					// an address lookup failed
	NA_BOT,
	NA_LOOPBACK,
	NA_BROADCAST,
	NA_IP
} netadrtype_t;

typedef struct netadr_s
{
	netadrtype_t	type;

	union {
		byte			ip[4];
		int32_t			ipi;
	};
	uint16_t		port;
} netadr_t;

/*
==============================================================

NON-PORTABLE SYSTEM SERVICES

==============================================================
*/

typedef enum {
	AXIS_SIDE,
	AXIS_FORWARD,
	AXIS_UP,
	AXIS_ROLL,
	AXIS_YAW,
	AXIS_PITCH,
	MAX_JOYSTICK_AXIS
} joystickAxis_t;

typedef enum {
  // bk001129 - make sure SE_NONE is zero
	SE_NONE = 0,	// evTime is still valid
	SE_KEY,		// evValue is a key code, evValue2 is the down flag
	SE_CHAR,	// evValue is an ascii char
	SE_MOUSE,	// evValue and evValue2 are reletive signed x / y moves
	SE_JOYSTICK_AXIS,	// evValue is an axis number and evValue2 is the current state (-127 to 127)
	SE_CONSOLE	// evPtr is a char*
} sysEventType_t;

typedef struct sysEvent_s {
	int				evTime;
	sysEventType_t	evType;
	int				evValue, evValue2;
	int				evPtrLength;	// bytes of data pointed to by evPtr, for journaling
	void			*evPtr;			// this must be manually freed if not NULL
} sysEvent_t;

extern cvar_t *com_minimized;
extern cvar_t *com_unfocused;
extern cvar_t *com_maxfps;
extern cvar_t *com_maxfpsMinimized;
extern cvar_t *com_maxfpsUnfocused;

sysEvent_t	Sys_GetEvent( void );

void	Sys_Init (void);

void	*Sys_LoadModuleLibrary(const char *name, qboolean mvOverride, intptr_t(QDECL **entryPoint)(int, ...), intptr_t(QDECL *systemcalls)(intptr_t, ...));
void	Sys_UnloadModuleLibrary(void *dllHandle);

char	*Sys_GetCurrentUser( void );

Q_NORETURN void Sys_Error( const char *error, ... ) __attribute__ ((format (printf, 1, 2)));
Q_NORETURN void Sys_Quit (void);
char	*Sys_GetClipboardData( void );	// note that this isn't journaled...

void	Sys_Print( const char *msg, qboolean extendedColors );

void CON_CreateConsoleWindow(void);
void CON_DeleteConsoleWindow(void);

// Sys_Milliseconds should only be used for profiling purposes,
// any game related timing information should come from event timestamps
int		Sys_Milliseconds (bool baseTime = false);
int		Sys_Milliseconds2(void);
void	Sys_Sleep( int msec );

extern "C" void	Sys_SnapVector( float *v );

bool Sys_RandomBytes( byte *string, int len );

void	Sys_SetErrorText( const char *text );

void	Sys_SendPacket( int length, const void *data, netadr_t to );

qboolean	Sys_StringToAdr( const char *s, netadr_t *a );
//Does NOT parse port numbers, only base addresses.

qboolean	Sys_IsLANAddress (netadr_t adr);
void		Sys_ShowIP(void);

qboolean	Sys_Mkdir( const char *path );
char	*Sys_Cwd( void );
int			Sys_PID( void );

char *Sys_DefaultInstallPath(void);
char *Sys_DefaultAssetsPath();
char *Sys_DefaultHomePath(void);

#ifdef MACOS_X
char    *Sys_DefaultAppPath(void);
#endif

const char *Sys_Dirname( char *path );
const char *Sys_Basename( char *path );

bool Sys_PathCmp( const char *path1, const char *path2 );

const char **Sys_ListFiles( const char *directory, const char *extension, char *filter, int *numfiles, qboolean wantsubs );
void	Sys_FreeFileList( const char **fileList );
//rwwRMG - changed to fileList to not conflict with list type

time_t Sys_FileTime( const char *path );

qboolean Sys_LowPhysicalMemory();

void Sys_SetProcessorAffinity( void );

int Sys_FLock(int fd, flockCmd_t cmd, qboolean nb);
void Sys_PrintBacktrace(void);

char *Sys_ResolvePath( char *path );
char *Sys_RealPath( char *path );
int Sys_FindFunctions( void );

typedef enum graphicsApi_e
{
	GRAPHICS_API_GENERIC,

	// Only OpenGL needs special treatment..
	GRAPHICS_API_OPENGL,
} graphicsApi_t;

// Graphics API
typedef struct window_s
{
	void *handle; // OS-dependent window handle
	graphicsApi_t api;
} window_t;

typedef enum glProfile_e
{
	GLPROFILE_COMPATIBILITY,
	GLPROFILE_CORE,
	GLPROFILE_ES,
} glProfile_t;

typedef enum glContextFlag_e
{
	GLCONTEXT_DEBUG = (1 << 1),
} glContextFlag_t;

typedef struct windowDesc_s
{
	graphicsApi_t api;

	// Only used if api == GRAPHICS_API_OPENGL
	struct gl_
	{
		int majorVersion;
		int minorVersion;
		glProfile_t profile;
		uint32_t contextFlags;
	} gl;
} windowDesc_t;

typedef enum {
	TBS_NORMAL,
	TBS_ERROR,
	TBS_NOTIFY,
	TBS_INDETERMINATE,
	TBS_PROGRESS
} tbstate_t;

window_t	WIN_Init( const windowDesc_t *desc, glconfig_t *glConfig );
void		WIN_InitGammaMethod(glconfig_t *glConfig);
void		WIN_Present( window_t *window );
void		WIN_SetGamma( glconfig_t *glConfig, byte red[256], byte green[256], byte blue[256] );
void		WIN_SetTaskbarState(tbstate_t state, uint64_t current, uint64_t total);
void		WIN_Shutdown( void );
void *		WIN_GL_GetProcAddress( const char *proc );
void		GLimp_Minimize(void);

uint8_t ConvertUTF32ToExpectedCharset( uint32_t utf32 );
