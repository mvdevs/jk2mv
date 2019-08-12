// qcommon.h -- definitions common between client and server, but not game.or ref modules
#ifndef _QCOMMON_H_
#define _QCOMMON_H_

#include "../qcommon/q_shared.h"
#include "../api/mvapi.h"
#include "../api/mvmenu.h"
#include "../sys/sys_public.h"

//============================================================================

// for auto-complete (copied from OpenJK)
#define CONSOLE_PROMPT_CHAR ']'
#define	MAX_EDIT_LINE		256
#define COMMAND_HISTORY		128 // increased in jk2mv
#define FIELD_HISTORY_SIZE	32
#define KILL_RING_SIZE		16

//For determining whether to allow 1.02 color codes:
#define MV_USE102COLOR ((qboolean)(MV_GetCurrentGameversion() == VERSION_1_02 || MV_GetCurrentGameversion() == VERSION_1_03))

typedef struct {
	int		cursor;
	int		scroll;
	int		widthInChars;
	char	*buffer;

	char	bufferHistory[FIELD_HISTORY_SIZE][MAX_EDIT_LINE];
	int		cursorHistory[FIELD_HISTORY_SIZE];
	int		scrollHistory[FIELD_HISTORY_SIZE];
	int		historyTail;
	int		historyHead;
	int		currentTail;

	qboolean	typing;
	qboolean	mod;
} field_t;

typedef void ( *callbackFunc_t )( const char *s );
typedef void (*completionFunc_t)( char *args, int argNum );

// common.cpp
void Field_CheckRep( field_t *edit );
void Field_Clear( field_t *edit );
void Field_AutoComplete( field_t *edit );
void Field_AutoComplete2( field_t *field, qboolean doCommands, qboolean doCvars, qboolean doArguments );
void Field_CompleteKeyname( void );
void Field_CompleteFilename( const char *dir, const char *ext, qboolean stripExt );
void Field_CompleteModelname( void );
void Field_CompleteCommand( char *cmd, qboolean doCommands, qboolean doCvars, qboolean doArguments );
int Field_GetLastMatchCount();
qboolean Field_WasComplete();
int FloatAsInt( float f );

extern qboolean com_demoplaying;

// cl_keys.cpp
void Key_KeynameCompletion ( void(*callback)( const char *s ) );

// files.cpp
void FS_FilenameCompletion( const char *dir, const char *ext, qboolean stripExt, callbackFunc_t callback );

// cmd.cpp
void Cmd_TokenizeString( const char *text_in );
void Cmd_TokenizeStringIgnoreQuotes( const char *text_in );
void Cmd_CompleteArgument( const char *command, char *args, int argNum );
void Cmd_CompleteCfgName( char *args, int argNum );
void Cmd_CompleteTxtName( char *args, int argNum );
void Cmd_SetCommandCompletionFunc( const char *command, completionFunc_t complete );

// cvar.cpp
void Cvar_CompleteCvarName( char *args, int argNum );




//
// msg.c
//
typedef struct {
	qboolean	allowoverflow;	// if false, do a Com_Error
	qboolean	overflowed;		// set to true if the buffer size failed (with allowoverflow set)
	qboolean	oob;			// set to true if the buffer size failed (with allowoverflow set)
	byte	*data;
	int		maxsize;
	int		cursize;
	int		readcount;
	int		bit;				// for bitwise reads and writes
} msg_t;

void MSG_Init (msg_t *buf, byte *data, int length);
void MSG_InitOOB( msg_t *buf, byte *data, int length );
void MSG_Clear (msg_t *buf);
void MSG_WriteData (msg_t *buf, const void *data, int length);
void MSG_Bitstream( msg_t *buf );


struct usercmd_s;
struct entityState_s;
struct playerState_s;

void MSG_WriteBits( msg_t *msg, int value, int bits );

void MSG_WriteChar (msg_t *sb, int c);
void MSG_WriteByte (msg_t *sb, int c);
void MSG_WriteShort (msg_t *sb, int c);
void MSG_WriteLong (msg_t *sb, int c);
void MSG_WriteFloat (msg_t *sb, float f);
void MSG_WriteString (msg_t *sb, const char *s);
void MSG_WriteBigString (msg_t *sb, const char *s);
void MSG_WriteAngle16 (msg_t *sb, float f);

void	MSG_BeginReading (msg_t *sb);
void	MSG_BeginReadingOOB(msg_t *sb);

int		MSG_ReadBits( msg_t *msg, int bits );

int		MSG_ReadChar (msg_t *sb);
int		MSG_ReadByte (msg_t *sb);
int		MSG_ReadShort (msg_t *sb);
int		MSG_ReadLong (msg_t *sb);
float	MSG_ReadFloat (msg_t *sb);
char	*MSG_ReadString (msg_t *sb);
char	*MSG_ReadBigString (msg_t *sb);
char	*MSG_ReadStringLine (msg_t *sb);
float	MSG_ReadAngle16 (msg_t *sb);
void	MSG_ReadData (msg_t *sb, void *buffer, int size);


void MSG_WriteDeltaUsercmd( msg_t *msg, struct usercmd_s *from, struct usercmd_s *to );
void MSG_ReadDeltaUsercmd( msg_t *msg, struct usercmd_s *from, struct usercmd_s *to );

void MSG_WriteDeltaUsercmdKey( msg_t *msg, int key, usercmd_t *from, usercmd_t *to );
void MSG_ReadDeltaUsercmdKey( msg_t *msg, int key, usercmd_t *from, usercmd_t *to );

void MSG_WriteDeltaEntity( msg_t *msg, struct entityState_s *from, struct entityState_s *to
						   , qboolean force );
void MSG_ReadDeltaEntity( msg_t *msg, entityState_t *from, entityState_t *to,
						 int number );

void MSG_WriteDeltaPlayerstate( msg_t *msg, struct playerState_s *from, struct playerState_s *to );
void MSG_ReadDeltaPlayerstate( msg_t *msg, struct playerState_s *from, struct playerState_s *to );


void MSG_ReportChangeVectors_f( void );

//============================================================================

/*
==============================================================

NET

==============================================================
*/

#define NET_ENABLEV4		0x01

#define	PACKET_BACKUP	32	// number of old messages that must be kept on client and
							// server for delta comrpession and ping estimation
#define	PACKET_MASK		(PACKET_BACKUP-1)

#define	MAX_PACKET_USERCMDS		32		// max number of usercmd_t in a packet

#define	PORT_ANY			-1

#define	MAX_RELIABLE_COMMANDS	128			// max string commands buffered for restransmit

typedef enum {
	NS_CLIENT,
	NS_SERVER
} netsrc_t;

void		NET_Init( void );
void		NET_Shutdown( void );
void		NET_Config( qboolean enableNetworking );
void		NET_Restart_f(void);

typedef int dlHandle_t;
typedef void(*dl_ended_callback)(dlHandle_t handle, qboolean success, const char *err_msg);
typedef void(*dl_status_callback)(size_t total_bytes, size_t downloaded_bytes);

void		NET_HTTP_Shutdown();
void		NET_HTTP_ProcessEvents();
int			NET_HTTP_StartServer(int port);
void		NET_HTTP_StopServer();
dlHandle_t	NET_HTTP_StartDownload(const char *url, const char *toPath, dl_ended_callback ended_callback, dl_status_callback status_callback, const char *userAgent, const char *referer);
void		NET_HTTP_StopDownload(dlHandle_t handle);

void		NET_SendPacket (netsrc_t sock, int length, const void *data, netadr_t to);
void		QDECL NET_OutOfBandPrint( netsrc_t net_socket, netadr_t adr, const char *format, ...) __attribute__ ((format (printf, 3, 4)));
void		QDECL NET_OutOfBandData( netsrc_t sock, netadr_t adr, byte *format, int len );

qboolean	NET_CompareAdr (netadr_t a, netadr_t b);
qboolean	NET_CompareBaseAdr (netadr_t a, netadr_t b);
qboolean	NET_IsLocalAddress (netadr_t adr);
const char	*NET_AdrToString (netadr_t a);
qboolean	NET_StringToAdr ( const char *s, netadr_t *a);
qboolean	NET_GetLoopPacket (netsrc_t sock, netadr_t *net_from, msg_t *net_message);
void		NET_Sleep(int msec);

#define	MAX_MSGLEN				16384		// max length of a message, which may
											// be fragmented into multiple packets

#define MAX_DOWNLOAD_WINDOW			8		// max of eight download frames
#define MAX_DOWNLOAD_BLKSIZE		2048	// 2048 byte block chunks


/*
Netchan handles packet fragmentation and out of order / duplicate suppression
*/

typedef struct {
	netsrc_t	sock;

	int			dropped;			// between last packet and previous

	netadr_t	remoteAddress;
	int			qport;				// qport value to write when transmitting

	// sequencing variables
	int			incomingSequence;
	int			outgoingSequence;

	// incoming fragment assembly buffer
	int			fragmentSequence;
	int			fragmentLength;
	byte		fragmentBuffer[MAX_MSGLEN];

	// outgoing fragment buffer
	// we need to space out the sending of large fragmented messages
	qboolean	unsentFragments;
	int			unsentFragmentStart;
	int			unsentLength;
	byte		unsentBuffer[MAX_MSGLEN];
} netchan_t;

void Netchan_Init( int qport );
void Netchan_Setup( netsrc_t sock, netchan_t *chan, netadr_t adr, int qport );

void Netchan_Transmit( netchan_t *chan, int length, const byte *data );
void Netchan_TransmitNextFragment( netchan_t *chan );

qboolean Netchan_Process( netchan_t *chan, msg_t *msg );


/*
==============================================================

PROTOCOL

==============================================================
*/

#define	MAX_MASTER_SERVERS	8

#define	UPDATE_SERVER_NAME		"update.jk2mv.org"

#define	PORT_MASTER			28060
#define	PORT_UPDATE			28061
#define	PORT_SERVER			28070	//...+9 more for multiple servers
#define	NUM_SERVER_PORTS	4		// broadcast scan this many ports after
									// PORT_SERVER so a single machine can
									// run multiple servers


// the svc_strings[] array in cl_parse.c should mirror this
//
// server to client
//
enum svc_ops_e {
	svc_bad,
	svc_nop,
	svc_gamestate,
	svc_configstring,			// [short] [string] only in gamestate messages
	svc_baseline,				// only in gamestate messages
	svc_serverCommand,			// [string] to be executed by client game module
	svc_download,				// [short] size [size bytes]
	svc_snapshot,
	svc_mapchange,

	svc_EOF
};


//
// client to server
//
enum clc_ops_e {
	clc_bad,
	clc_nop,
	clc_move,				// [[usercmd_t]
	clc_moveNoDelta,		// [[usercmd_t]
	clc_clientCommand,		// [string] message
	clc_EOF
};

/*
==============================================================

VIRTUAL MACHINE

==============================================================
*/

typedef struct vm_s vm_t;

typedef enum {
	VMI_NATIVE,
	VMI_BYTECODE,
	VMI_COMPILED
} vmInterpret_t;

typedef enum {
	TRAP_MEMSET = 100,
	TRAP_MEMCPY,
	TRAP_STRNCPY,
	TRAP_SIN,
	TRAP_COS,
	TRAP_ATAN2,
	TRAP_SQRT,
	TRAP_MATRIXMULTIPLY,
	TRAP_ANGLEVECTORS,
	TRAP_PERPENDICULARVECTOR,
	TRAP_FLOOR,
	TRAP_CEIL,

	TRAP_TESTPRINTINT,
	TRAP_TESTPRINTFLOAT
} sharedTraps_t;

#define	MAX_VM		3

void	VM_Init( void );
vm_t	*VM_Create(const char *module, qboolean mvOverride, intptr_t(*systemCalls)(intptr_t *), vmInterpret_t interpret);
// module should be bare: "cgame", not "cgame.dll" or "vm/cgame.qvm"

void	VM_Free( vm_t *vm );
void	VM_Clear(void);
vm_t	*VM_Restart( vm_t *vm );

intptr_t QDECL VM_Call(vm_t *vm, int callnum, ...);

void	VM_Debug( int level );

void	*VM_ArgPtr( int syscall, intptr_t intValue, int32_t size );
void	*VM_ArgArray( int syscall, intptr_t intValue, uint32_t size, int32_t num );
char	*VM_ArgString( int syscall, intptr_t intValue );
intptr_t	VM_strncpy( intptr_t dest, intptr_t src, intptr_t size );
void	VM_LocateGameDataCheck( const void *data, int entitySize, int num_entities );


ID_INLINE float _vmf(intptr_t x)
{
	floatint_t fi;
	fi.i = (int)x;
	return fi.f;
}

// macros for vm-safe translation of SysCall arguments

// float
#define	VMF(x)				_vmf(args[x])
// single variable of type "type"
#define VMAV(x, type)		((type *) VM_ArgPtr(args[0], args[x], sizeof(type)))
// single variable of incomplete "type"
#define VMAIV(x, type, size)((type *) VM_ArgPtr(args[0], args[x], size))
// static-length array of "type" variables
#define VMAP(x, type, num)	((type *) VM_ArgPtr(args[0], args[x], sizeof(type) * num))
// dynamic-length array of "type" variables
#define VMAA(x, type, num)	((type *) VM_ArgArray(args[0], args[x], sizeof(type), num))
// NULL-terminated string (first char is always safe to use)
#define VMAS(x)				VM_ArgString(args[0], args[x])

char	*VM_ExplicitArgString(vm_t *vm, intptr_t intValue);

void	VM_Forced_Unload_Start(void);
void	VM_Forced_Unload_Done(void);

int	VM_MVAPILevel(const vm_t *vm);
void VM_SetMVAPILevel(vm_t *vm, int level);

void VM_SetMVMenuLevel(vm_t *vm, int level);
int VM_MVMenuLevel(const vm_t *vm);

mvversion_t VM_GetGameversion(const vm_t *vm);
void VM_SetGameversion(vm_t *vm, mvversion_t gameversion);

/*
==============================================================

CMD

Command text buffering and command execution

==============================================================
*/

/*

Any number of commands can be added in a frame, from several different sources.
Most commands come from either keybindings or console line input, but entire text
files can be execed.

*/

void Cbuf_Init (void);
// allocates an initial text buffer that will grow as needed

void Cbuf_AddText( const char *text );
// Adds command text at the end of the buffer, does NOT add a final \n

void Cbuf_ExecuteText( cbufExec_t exec_when, const char *text );
// this can be used in place of either Cbuf_AddText or Cbuf_InsertText

void Cbuf_Execute (void);
// Pulls off \n terminated lines of text from the command buffer and sends
// them through Cmd_ExecuteString.  Stops when the buffer is empty.
// Normally called once per frame, but may be explicitly invoked.
// Do not call inside a command function, or current args will be destroyed.

//===========================================================================

/*

Command execution takes a null terminated string, breaks it into tokens,
then searches for a command or variable that matches the first token.

*/

typedef void (*xcommand_t) (void);

void	Cmd_Init (void);

void	Cmd_AddCommand( const char *cmd_name, xcommand_t function );
// called by the init functions of other parts of the program to
// register commands and functions to call for them.
// The cmd_name is referenced later, so it should not be in temp memory
// if function is NULL, the command will be forwarded to the server
// as a clc_clientCommand instead of executed locally

void	Cmd_RemoveCommand( const char *cmd_name );

void	Cmd_CommandCompletion( void(*callback)(const char *s) );
// callback with each valid string

int		Cmd_Argc (void);
char	*Cmd_Argv (int arg);
void	Cmd_ArgvBuffer( int arg, char *buffer, int bufferLength );
char	*Cmd_Args (void);
char	*Cmd_ArgsFrom( int arg );
const char	*Cmd_Cmd( void );
void	Cmd_ArgsBuffer( char *buffer, int bufferLength );
void	Cmd_DropArg( int arg );
// The functions that execute commands get their parameters with these
// functions. Cmd_Argv () will return an empty string, not a NULL
// if arg > argc, so string operations are allways safe.

void	Cmd_TokenizeString( const char *text );
// Takes a null terminated string.  Does not need to be /n terminated.
// breaks the string up into arg tokens.

void	Cmd_Execute( void );
void	Cmd_ExecuteString( const char *text );
// Parses a single line of text into arguments and tries to execute it
// as if it was typed at the console


/*
==============================================================

CVAR

==============================================================
*/

/*

cvar_t variables are used to hold scalar or string variables that can be changed
or displayed at the console or prog code as well as accessed directly
in C code.

The user can access cvars from the console in three ways:
r_draworder			prints the current value
r_draworder 0		sets the current value to 0
set r_draworder 0	as above, but creates the cvar if not present

Cvars are restricted from having the same names as commands to keep this
interface from being ambiguous.

The are also occasionally used to communicated information between different
modules of the program.

*/

cvar_t *Cvar_Get( const char *var_name, const char *var_value, int flags );
cvar_t *Cvar_Get( const char *var_name, const char *var_value, int flags, qboolean isVmCall );
// creates the variable if it doesn't exist, or returns the existing one
// if it exists, the value will not be changed, but flags will be ORed in
// that allows variables to be unarchived without needing bitflags
// if value is "", the value will not override a previously set value.

void	Cvar_Register( vmCvar_t *vmCvar, const char *varName, const char *defaultValue, int flags );
// basically a slightly modified Cvar_Get for the interpreted modules

void	Cvar_Update( vmCvar_t *vmCvar );
// updates an interpreted modules' version of a cvar

void 	Cvar_Set( const char *var_name, const char *value );
cvar_t *Cvar_Set2( const char *var_name, const char *value, qboolean force );
cvar_t *Cvar_Set2( const char *var_name, const char *value, qboolean force, qboolean isVmCall );
// will create the variable with no flags if it doesn't exist

void Cvar_SetLatched( const char *var_name, const char *value);
// don't set the cvar immediately

void	Cvar_SetValue( const char *var_name, float value );
void	Cvar_SetValue( const char *var_name, float value, qboolean isVmCall );
// expands value to a string and calls Cvar_Set

cvar_t *Cvar_FindVar(const char *var_name);

float	Cvar_VariableValue( const char *var_name );
float	Cvar_VariableValue( const char *var_name, qboolean isVmCall );
int		Cvar_VariableIntegerValue( const char *var_name );
int		Cvar_VariableIntegerValue( const char *var_name, qboolean isVmCall );
// returns 0 if not defined or non numeric

const char *Cvar_VariableString( const char *var_name );
void	Cvar_VariableStringBuffer( const char *var_name, char *buffer, int bufsize );
void	Cvar_VariableStringBuffer( const char *var_name, char *buffer, int bufsize, qboolean isVmCall );
// returns an empty string if not defined

void	Cvar_CommandCompletion( void(*callback)(const char *s) );
// callback with each valid string

void 	Cvar_Reset( const char *var_name );
void	Cvar_Reset( const char *var_name, qboolean isVmCall );

void	Cvar_SetCheatState( void );
// reset all testing vars to a safe value

qboolean Cvar_Command( void );
// called by Cmd_ExecuteString when Cmd_Argv(0) doesn't match a known
// command.  Returns true if the command was a variable reference that
// was handled. (print or change)

void Cvar_WriteVariables(fileHandle_t f, qboolean locals);
// writes lines containing "set variable value" for all variables
// with the archive flag set to true.

void	Cvar_Init( void );

char	*Cvar_InfoString( int bit );
char	*Cvar_InfoString( int bit, qboolean isVmCall );
char	*Cvar_InfoString_Big( int bit );
// returns an info string containing all the cvars that have the given bit set
// in their flags ( CVAR_USERINFO, CVAR_SERVERINFO, CVAR_SYSTEMINFO, etc )
void	Cvar_InfoStringBuffer( int bit, char *buff, int buffsize );
void	Cvar_InfoStringBuffer( int bit, char* buff, int buffsize, qboolean isVmCall );

void	Cvar_Restart_f( void );

extern	int			cvar_modifiedFlags;
// whenever a cvar is modifed, its flags will be OR'd into this, so
// a single check can determine if any CVAR_USERINFO, CVAR_SERVERINFO,
// etc, variables have been modified since the last check.  The bit
// can then be cleared to allow another change detection.

/*
==============================================================

FILESYSTEM

No stdio calls should be used by any part of the game, because
we need to deal with all sorts of directory and seperator char
issues.
==============================================================
*/

#define	BASEGAME			"base"

// referenced flags
// these are in loop specific order so don't change the order
#define FS_GENERAL_REF	0x01
#define FS_UI_REF		0x02
#define FS_CGAME_REF	0x04
// number of id paks that will never be autodownloaded from base
#define NUM_ID_PAKS		9

#define	MAX_FILE_HANDLES	256 // increased from 64 in jk2mv

typedef enum {
	MODULE_MAIN,
	MODULE_RENDERER,
	MODULE_FX,
	MODULE_BOTLIB,
	MODULE_GAME,
	MODULE_CGAME,
	MODULE_UI,
	MODULE_MAX
} module_t;

qboolean FS_CopyFile( char *fromOSPath, char *toOSPath, char *newOSPath = NULL, const int newSize = 0 );

qboolean FS_Initialized();

void	FS_InitFilesystem (void);
void	FS_Shutdown( qboolean closemfp );

qboolean	FS_ConditionalRestart( int checksumFeed );
void	FS_Restart( int checksumFeed );
// shutdown and restart the filesystem so changes to fs_gamedir can take effect

const char	**FS_ListFiles( const char *directory, const char *extension, int *numfiles );
// directory should not have either a leading or trailing /
// if extension is "/", only subdirectories will be returned
// the returned files will not include any directories or /
const char	**FS_ListFiles2( const char *directory, const char *extension, int *numfiles );
// directory argument is required to be an actual directory, not a
// path prefix. If directory argument has a trailing / then files one
// level below are listed too.

void	FS_FreeFileList( const char **list );

qboolean FS_FileExists( const char *file );
qboolean FS_Base_FileExists(const char *file);
qboolean FS_AllPath_Base_FileExists(const char *file);

int		FS_LoadStack();

int		FS_GetFileList(  const char *path, const char *extension, char *listbuf, int bufsize );
int		FS_GetModList(  char *listbuf, int bufsize );

fileHandle_t	FS_FOpenFileWrite( const char *qpath, module_t module = MODULE_MAIN );
fileHandle_t FS_FOpenBaseFileWrite(const char *filename, module_t module = MODULE_MAIN);
// will properly create any needed paths and deal with seperater character issues

fileHandle_t FS_SV_FOpenFileWrite( const char *filename, module_t module = MODULE_MAIN );
fileHandle_t FS_SV_FOpenFileAppend( const char *filename, module_t module = MODULE_MAIN );
int		FS_SV_FOpenFileRead( const char *filename, fileHandle_t *fp, module_t module = MODULE_MAIN );
void	FS_SV_Rename( const char *from, const char *to );
int		FS_FOpenFileRead( const char *qpath, fileHandle_t *file, qboolean uniqueFILE, module_t module = MODULE_MAIN );
int		FS_FOpenFileReadHash( const char *filename, fileHandle_t *file, qboolean uniqueFILE, unsigned long *filehash, module_t module = MODULE_MAIN );
// if uniqueFILE is true, then a new FILE will be fopened even if the file
// is found in an already open pak file.  If uniqueFILE is false, you must call
// FS_FCloseFile instead of fclose, otherwise the pak FILE would be improperly closed
// It is generally safe to always set uniqueFILE to true, because the majority of
// file IO goes through FS_ReadFile, which Does The Right Thing already.

int		FS_FileIsInPAK(const char *filename, int *pChecksum );
// returns 1 if a file is in the PAK file, otherwise -1

int		FS_Write( const void *buffer, int len, fileHandle_t f, module_t module = MODULE_MAIN );

int		FS_Read2( void *buffer, int len, fileHandle_t f, module_t module = MODULE_MAIN );
int		FS_Read( void *buffer, int len, fileHandle_t f, module_t module = MODULE_MAIN );
// properly handles partial reads and reads from other dlls

void	FS_FCloseFile( fileHandle_t f, module_t module = MODULE_MAIN );
// note: you can't just fclose from another DLL, due to MS libc issues

int		FS_ReadFile( const char *qpath, void **buffer );
// returns the length of the file
// a null buffer will just return the file length without loading
// as a quick check for existance. -1 length == not present
// A 0 byte will always be appended at the end, so string ops are safe.
// the buffer should be considered read-only, because it may be cached
// for other uses.

void	FS_ForceFlush( fileHandle_t f, module_t module = MODULE_MAIN );
// forces flush on files we're writing to.

void	FS_FreeFile( void *buffer );
// frees the memory returned by FS_ReadFile

void	FS_WriteFile( const char *qpath, const void *buffer, int size );
// writes a complete file, creating any subdirectories needed

int		FS_filelength( fileHandle_t f, module_t module = MODULE_MAIN );
// doesn't work for files that are opened from a pack file

char	*FS_BuildOSPath(const char *base, const char *game, const char *qpath);
char	*FS_BuildOSPath(const char *base, const char *path);
qboolean FS_CreatePath (char *OSPath);

int		FS_FTell( fileHandle_t f, module_t module = MODULE_MAIN );
// where are we?

void	FS_Flush( fileHandle_t f, module_t module = MODULE_MAIN );

void 	QDECL FS_Printf( fileHandle_t f, const char *fmt, ... ) __attribute__ ((format (printf, 2, 3)));
// like fprintf

int		FS_FOpenFileByMode( const char *qpath, fileHandle_t *f, fsMode_t mode, module_t module = MODULE_MAIN );
int		FS_FOpenFileByModeHash( const char *qpath, fileHandle_t *f, fsMode_t mode, unsigned long *hash, module_t module = MODULE_MAIN );
// opens a file for reading, writing, or appending depending on the value of mode

int		FS_Seek( fileHandle_t f, int offset, int origin, module_t module = MODULE_MAIN );
// seek on a file (doesn't work for zip files!!!!!!!!)

int		FS_FilenameCompare( const char *s1, const char *s2 );

const char *FS_LoadedPakNames( void );
const char *FS_LoadedPakChecksums( void );
const char *FS_LoadedPakPureChecksums( void );
// Returns a space separated string containing the checksums of all loaded pk3 files.
// Servers with sv_pure set will get this string and pass it to clients.

const char *FS_ReferencedPakNames( void );
const char *FS_ReferencedPakChecksums( void );
const char *FS_ReferencedPakPureChecksums( void );
// Returns a space separated string containing the checksums of all loaded
// AND referenced pk3 files. Servers with sv_pure set will get this string
// back from clients for pure validation

void FS_ClearPakReferences( int flags );
// clears referenced booleans on loaded pk3s

void FS_PureServerSetReferencedPaks( const char *pakSums, const char *pakNames );
void FS_PureServerSetLoadedPaks( const char *pakSums, const char *pakNames );
// If the string is empty, all data sources will be allowed.
// If not empty, only pk3 files that match one of the space
// separated checksums will be checked for files, with the
// sole exception of .cfg files.

qboolean FS_CheckDirTraversal(const char *checkdir);
qboolean FS_ComparePaks(char *neededpaks, int len, int *chksums, size_t maxchksums, qboolean dlstring);
void FS_Rename( const char *from, const char *to );

const char *FS_MV_VerifyDownloadPath(const char *pk3file);

int FS_GetDLList(dlfile_t *files, int maxfiles);
qboolean FS_RMDLPrefix(const char *qpath);
qboolean FS_DeleteDLFile(const char *qpath);

void FS_HomeRemove( const char *homePath );
qboolean FS_IsFifo( const char *filename );
int FS_FLock( fileHandle_t h, flockCmd_t cmd, qboolean nb, module_t module = MODULE_MAIN );

/*
==============================================================

MISC

==============================================================
*/

// NOTE NOTE NOTE!!!!!!!!!!!!!
//
// Any CPUID_XXXX defined as higher than CPUID_INTEL_MMX *must* have MMX support (eg like CPUID_AMD_3DNOW (0x30) has),
//	this allows convenient MMX capability checking. If you for some reason want to support some new processor that does
//	*NOT* have MMX (yeah, right), then define it as a lower number. -slc
//
// ( These values are returned by Sys_GetProcessorId )
//
#define CPUID_GENERIC			0			// any unrecognized processor

#define CPUID_AXP				0x10

#define CPUID_INTEL_UNSUPPORTED	0x20			// Intel 386/486
#define CPUID_INTEL_PENTIUM		0x21			// Intel Pentium or PPro
#define CPUID_INTEL_MMX			0x22			// Intel Pentium/MMX or P2/MMX
#define CPUID_INTEL_KATMAI		0x23			// Intel Katmai
#define CPUID_INTEL_WILLIAMETTE	0x24			// Intel Williamette

#define CPUID_AMD_3DNOW			0x30			// AMD K6 3DNOW!
//
//==========================================================


const char	*CopyString( const char *in );
void		Info_Print( const char *s );

void		Com_BeginRedirect (char *buffer, size_t buffersize, void (*flush)(char *), qboolean silent);
void		Com_EndRedirect( void );
void 		QDECL Com_Printf( const char *fmt, ... ) __attribute__ ((format (printf, 1, 2)));
void		QDECL Com_Printf_Ext( qboolean extendedColors, const char *msg, ... ) __attribute__ ((format (printf, 2, 3)));
void 		QDECL Com_DPrintf( const char *fmt, ... ) __attribute__ ((format (printf, 1, 2)));
void		QDECL Com_OPrintf( const char *fmt, ...) __attribute__ ((format (printf, 1, 2))); // Outputs to the VC / Windows Debug window (only in debug compile)
Q_NORETURN void QDECL  Com_Error( errorParm_t code, const char *fmt, ... ) __attribute__ ((format (printf, 2, 3)));
Q_NORETURN void Com_Quit( int signal );
void 		Com_Quit_f( void );
int			Com_EventLoop( void );
int			Com_Milliseconds( void );	// will be journaled properly
unsigned	Com_BlockChecksum( const void *buffer, int length );
unsigned	Com_BlockChecksumKey (void *buffer, int length, int key);
int			Com_HashKey(const char *string, int maxlen);
int			Com_Filter(const char *filter, const char *name, int casesensitive);
int			Com_FilterPath(char *filter, char *name, int casesensitive);
int			Com_RealTime(qtime_t *qtime);
qboolean	Com_SafeMode( void );
void Com_RunAndTimeServerPacket(netadr_t *evFrom, msg_t *buf);

void		Com_StartupVariable( const char *match );
// checks for and removes command line "+set var arg" constructs
// if match is NULL, all set commands will be executed, otherwise
// only a set with the exact name.  Only used during startup.


extern	cvar_t	*com_developer;
extern	cvar_t	*com_dedicated;
extern	cvar_t	*com_speeds;
extern	cvar_t	*com_timescale;
extern	cvar_t	*com_sv_running;
extern	cvar_t	*com_cl_running;
extern	cvar_t	*com_viewlog;			// 0 = hidden, 1 = visible, 2 = minimized
extern	cvar_t	*com_version;
extern	cvar_t	*com_blood;
extern	cvar_t	*com_buildScript;		// for building release pak files
extern	cvar_t	*com_journal;
extern	cvar_t	*com_cameraMode;
extern	cvar_t	*com_busyWait;

extern	cvar_t	*mv_apienabled;
extern	cvar_t	*com_debugMessage;

// both client and server must agree to pause
extern	cvar_t	*cl_paused;
extern	cvar_t	*sv_paused;

// com_speeds times
extern	int		time_game;
extern	int		time_frontend;
extern	int		time_backend;		// renderer backend time

extern	int		com_frameTime;
extern	int		com_frameMsec;

extern	qboolean	com_errorEntered;

extern	fileHandle_t	com_journalFile;
extern	fileHandle_t	com_journalDataFile;

/*
typedef enum {
	TAG_FREE,
	TAG_GENERAL,
	TAG_BOTLIB,
	TAG_RENDERER,
	TAG_SMALL,
	TAG_STATIC
} memtag_t;
*/

/*

--- low memory ----
server vm
server clipmap
---mark---
renderer initialization (shaders, etc)
UI vm
cgame vm
renderer map
renderer models

---free---

temp file loading
--- high memory ---

*/

#if defined(_DEBUG) && !defined(BSPC)
	#define ZONE_DEBUG
#endif

/*
#ifdef ZONE_DEBUG
	#define Z_TagMalloc(size, tag)			Z_TagMallocDebug(size, tag, #size, __FILE__, __LINE__)
	#define Z_Malloc(size)					Z_MallocDebug(size, #size, __FILE__, __LINE__)
	#define S_Malloc(size)					S_MallocDebug(size, #size, __FILE__, __LINE__)
	void *Z_TagMallocDebug( int size, int tag, char *label, char *file, int line );	// NOT 0 filled memory
	void *Z_MallocDebug( int size, char *label, char *file, int line );			// returns 0 filled memory
	void *S_MallocDebug( int size, char *label, char *file, int line );			// returns 0 filled memory
#else
	void *Z_TagMalloc( int size, int tag );	// NOT 0 filled memory
	void *Z_Malloc( int size );			// returns 0 filled memory
	void *S_Malloc( int size );			// NOT 0 filled memory only for small allocations
#endif
void Z_Free( void *ptr );
void Z_FreeTags( int tag );
int Z_AvailableMemory( void );
void Z_LogHeap( void );
*/

// later on I'll re-implement __FILE__, __LINE__ etc, but for now...
//
#ifdef ZONE_DEBUG
void *Z_Malloc  ( int iSize, memtag_t eTag, qboolean bZeroit = qfalse);	// return memory NOT zero-filled by default
void *S_Malloc	( int iSize );					// NOT 0 filled memory only for small allocations
#else
void *Z_Malloc  ( int iSize, memtag_t eTag, qboolean bZeroit = qfalse );	// return memory NOT zero-filled by default
void *S_Malloc	( int iSize );					// NOT 0 filled memory only for small allocations
#endif
void  Z_Validate( void );
int   Z_MemSize	( memtag_t eTag );
void  Z_TagFree	( memtag_t eTag );
void  Z_Free	( void *ptr );
int	  Z_Size	( void *pvAddress);
void Com_ShutdownZoneMemory(void);

void Hunk_Clear( void );
void Hunk_ClearToMark( void );
void Hunk_SetMark( void );
qboolean Hunk_CheckMark( void );
void Hunk_ClearTempMemory( void );
void *Hunk_AllocateTempMemory( int size );
void Hunk_FreeTempMemory( void *buf );
int	Hunk_MemoryRemaining( void );
void Hunk_Log( void);
void Hunk_Trash( void );

void Com_TouchMemory( void );

// commandLine should not include the executable name (argv[0])
void Com_Init( char *commandLine );
void Com_Frame( void );
void Com_Shutdown( void );


/*
==============================================================

CLIENT / SERVER SYSTEMS

==============================================================
*/

//
// client interface
//
void CL_InitKeyCommands( void );
// the keyboard binding interface must be setup before execing
// config files, but the rest of client startup will happen later

void CL_Init( void );
void CL_Disconnect( qboolean showMainMenu );
void CL_Shutdown( void );
void CL_Frame( int msec );
qboolean CL_GameCommand( void );
void CL_KeyEvent (int key, qboolean down, int time);

void CL_CharEvent( int key );
// char events are for field typing, not game control

void CL_MouseEvent( int dx, int dy, int time );

void CL_JoystickEvent( int axis, int value, int time );

void CL_PacketEvent( netadr_t from, msg_t *msg );

void CL_ConsolePrint( const char *text, qboolean extendedColors );

void CL_MapLoading( void );
// do a screen update before starting to load a map
// when the server is going to load a new map, the entire hunk
// will be cleared, so the client must shutdown cgame, ui, and
// the renderer

void	CL_ForwardCommandToServer( const char *string );
// adds the current command line as a clc_clientCommand to the client message.
// things like godmode, noclip, etc, are commands directed to the server,
// so when they are typed in at the console, they will need to be forwarded.

void CL_ShutdownAll( void );
// shutdown all the client stuff

void CL_FlushMemory( qboolean disconnecting );
// dump all memory on an error

void CL_StartHunkUsers( void );
// start all the client stuff using the hunk

void Key_WriteBindings( fileHandle_t f );
// for writing the config files

void S_ClearSoundBuffer( void );
// call before filesystem access

void SCR_DebugGraph (float value, int color);	// FIXME: move logging to common?

// AVI files have the start of pixel lines 4 byte-aligned
#define AVI_LINE_PADDING 4

//
// server interface
//
void SV_Init( void );
void SV_Shutdown( char *finalmsg );
int SV_FrameMsec();
void SV_Frame( int msec );
void SV_PacketEvent( netadr_t from, msg_t *msg );
qboolean SV_GameCommand( void );


//
// UI interface
//
qboolean UI_GameCommand( void );
qboolean UI_usesUniqueCDKey();

/* This is based on the Adaptive Huffman algorithm described in Sayood's Data
 * Compression book.  The ranks are not actually stored, but implicitly defined
 * by the location of a node within a doubly-linked list */

#define NYT HMAX					/* NYT = Not Yet Transmitted */
#define INTERNAL_NODE (HMAX+1)

typedef struct nodetype {
	struct	nodetype *left, *right, *parent; /* tree structure */
	struct	nodetype *next, *prev; /* doubly-linked list */
	struct	nodetype **head; /* highest ranked node in block */
	int		weight;
	int		symbol;
} node_t;

#define HMAX 256 /* Maximum symbol */

typedef struct {
	int			blocNode;
	int			blocPtrs;

	node_t*		tree;
	node_t*		lhead;
	node_t*		ltail;
	node_t*		loc[HMAX+1];
	node_t**	freelist;

	node_t		nodeList[768];
	node_t*		nodePtrs[768];
} huff_t;

typedef struct {
	huff_t		compressor;
	huff_t		decompressor;
} huffman_t;

void	Huff_Compress(msg_t *buf, int offset);
void	Huff_Decompress(msg_t *buf, int offset);
void	Huff_Init(huffman_t *huff);
void	Huff_addRef(huff_t* huff, byte ch);
int		Huff_Receive (node_t *node, int *ch, byte *fin);
void	Huff_transmit (huff_t *huff, int ch, byte *fout);
void	Huff_offsetReceive (node_t *node, int *ch, byte *fin, int *offset);
void	Huff_offsetTransmit (huff_t *huff, int ch, byte *fout, int *offset);
void	Huff_putBit( int bit, byte *fout, int *offset);
int		Huff_getBit( byte *fout, int *offset);

extern huffman_t clientHuffTables;

#define	SV_ENCODE_START		4
#define SV_DECODE_START		12
#define	CL_ENCODE_START		12
#define CL_DECODE_START		4

void MV_SetCurrentGameversion(mvversion_t version);
mvversion_t MV_GetCurrentGameversion();
mvprotocol_t MV_GetCurrentProtocol();

#endif // _QCOMMON_H_
