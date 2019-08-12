/*****************************************************************************
 * name:		files.c
 *
 * desc:		handle based filesystem for Quake III Arena
 *
 * $Archive: /MissionPack/code/qcommon/files.c $
 * $Author: Zaphod $
 * $Revision: 81 $
 * $Modtime: 5/30/01 2:31p $
 * $Date: 5/30/01 2:31p $
 *
 *****************************************************************************/


#include "../qcommon/q_shared.h"
#include "qcommon.h"
#include <unzip.h>	// minizip
#include <mv_setup.h>

#if !defined(DEDICATED) && !defined(FINAL_BUILD)
#include "../client/client.h"
#endif

/*
=============================================================================

QUAKE3 FILESYSTEM

All of Quake's data access is through a hierarchical file system, but the contents of
the file system can be transparently merged from several sources.

A "qpath" is a reference to game file data.  MAX_ZPATH is 256 characters, which must include
a terminating zero. "..", "\\", and ":" are explicitly illegal in qpaths to prevent any
references outside the quake directory system.

The "base path" is the path to the directory holding all the game directories and usually
the executable.  It defaults to ".", but can be overridden with a "+set fs_basepath c:\quake3"
command line to allow code debugging in a different directory.  Basepath cannot
be modified at all after startup.  Any files that are created (demos, screenshots,
etc) will be created reletive to the base path, so base path should usually be writable.

The "cd path" is the path to an alternate hierarchy that will be searched if a file
is not located in the base path.  A user can do a partial install that copies some
data to a base path created on their hard drive and leave the rest on the cd.  Files
are never writen to the cd path.  It defaults to a value set by the installer, like
"e:\quake3", but it can be overridden with "+set ds_cdpath g:\quake3".

If a user runs the game directly from a CD, the base path would be on the CD.  This
should still function correctly, but all file writes will fail (harmlessly).

The "home path" is the path used for all write access. On win32 systems we have "base path"
== "home path", but on *nix systems the base installation is usually readonly, and
"home path" points to ~/.q3a or similar

The user can also install custom mods and content in "home path", so it should be searched
along with "home path" and "cd path" for game content.


The "base game" is the directory under the paths where data comes from by default, and
can be either "base" or "demo".

The "current game" may be the same as the base game, or it may be the name of another
directory under the paths that should be searched for files before looking in the base game.
This is the basis for addons.

Clients automatically set the game directory after receiving a gamestate from a server,
so only servers need to worry about +set fs_game.

No other directories outside of the base game and current game will ever be referenced by
filesystem functions.

To save disk space and speed loading, directory trees can be collapsed into zip files.
The files use a ".pk3" extension to prevent users from unzipping them accidentally, but
otherwise the are simply normal uncompressed zip files.  A game directory can have multiple
zip files of the form "pak0.pk3", "pak1.pk3", etc.  Zip files are searched in decending order
from the highest number to the lowest, and will always take precedence over the filesystem.
This allows a pk3 distributed as a patch to override all existing data.

Because we will have updated executables freely available online, there is no point to
trying to restrict demo / oem versions of the game with code changes.  Demo / oem versions
should be exactly the same executables as release versions, but with different data that
automatically restricts where game media can come from to prevent add-ons from working.

After the paths are initialized, quake will look for the product.txt file.  If not
found and verified, the game will run in restricted mode.  In restricted mode, only
files contained in demoq3/pak0.pk3 will be available for loading, and only if the zip header is
verified to not have been modified.  A single exception is made for jk2mpconfig.cfg.  Files
can still be written out in restricted mode, so screenshots and demos are allowed.
Restricted mode can be tested by setting "+set fs_restrict 1" on the command line, even
if there is a valid product.txt under the basepath or cdpath.

If not running in restricted mode, and a file is not found in any local filesystem,
an attempt will be made to download it and save it under the base path.

If the "fs_copyfiles" cvar is set to 1, then every time a file is sourced from the cd
path, it will be copied over to the base path.  This is a development aid to help build
test releases and to copy working sets over slow network links.

File search order: when FS_FOpenFileRead gets called it will go through the fs_searchpaths
structure and stop on the first successful hit. fs_searchpaths is built with successive
calls to FS_AddGameDirectory

Additionaly, we search in several subdirectories:
current game is the current mode
base game is a variable to allow mods based on other mods
(such as base + missionpack content combination in a mod for instance)
BASEGAME is the hardcoded base game ("base")

e.g. the qpath "sound/newstuff/test.wav" would be searched for in the following places:

home path + current game's zip files
home path + current game's directory
base path + current game's zip files
base path + current game's directory
cd path + current game's zip files
cd path + current game's directory

home path + base game's zip file
home path + base game's directory
base path + base game's zip file
base path + base game's directory
cd path + base game's zip file
cd path + base game's directory

home path + BASEGAME's zip file
home path + BASEGAME's directory
base path + BASEGAME's zip file
base path + BASEGAME's directory
cd path + BASEGAME's zip file
cd path + BASEGAME's directory

server download, to be written to home path + current game's directory


The filesystem can be safely shutdown and reinitialized with different
basedir / cddir / game combinations, but all other subsystems that rely on it
(sound, video) must also be forced to restart.

Because the same files are loaded by both the clip model (CM_) and renderer (TR_)
subsystems, a simple single-file caching scheme is used.  The CM_ subsystems will
load the file with a request to cache.  Only one file will be kept cached at a time,
so any models that are going to be referenced by both subsystems should alternate
between the CM_ load function and the ref load function.

TODO: A qpath that starts with a leading slash will always refer to the base game, even if another
game is currently active.  This allows character models, skins, and sounds to be downloaded
to a common directory no matter which game is active.

How to prevent downloading zip files?
Pass pk3 file names in systeminfo, and download before FS_Restart()?

Aborting a download disconnects the client from the server.

How to mark files as downloadable?  Commercial add-ons won't be downloadable.

Non-commercial downloads will want to download the entire zip file.
the game would have to be reset to actually read the zip in

Auto-update information

Path separators

Casing

  separate server gamedir and client gamedir, so if the user starts
  a local game after having connected to a network game, it won't stick
  with the network game.

  allow menu options for game selection?

Read / write config to floppy option.

Different version coexistance?

When building a pak file, make sure a jk2mpconfig.cfg isn't present in it,
or configs will never get loaded from disk!

  todo:

  downloading (outside fs?)
  game directory passing and restarting

=============================================================================

*/

#define MAX_ZPATH			256
#define	MAX_SEARCH_PATHS	4096
#define MAX_FILEHASH_SIZE	1024

typedef struct fileInPack_s {
	char					*name;		// name of the file
	unsigned int			pos;		// file info position in zip
	unsigned int			len;		// uncompress file size
	struct	fileInPack_s*	next;		// next file in the hash
} fileInPack_t;

enum {
	PACKGVC_UNKNOWN = 0,
	PACKGVC_1_02 = 1,
	PACKGVC_1_03 = 2,
	PACKGVC_1_04 = 4,
};

typedef struct {
	char			pakFilename[MAX_OSPATH];	// c:\quake3\base\pak0.pk3
	char			pakBasename[MAX_OSPATH];	// pak0
	char			pakGamename[MAX_OSPATH];	// base
	unzFile			handle;						// handle to zip file
	int				checksum;					// regular checksum
	int				pure_checksum;				// checksum for pure
	int				numfiles;					// number of files in pk3
	int				referenced;					// referenced file flags
	qboolean		noref;						// file is blacklisted for referencing
	int				hashSize;					// hash table size (power of 2)
	fileInPack_t*	*hashTable;					// hash table
	fileInPack_t*	buildBuffer;				// buffer with the filenames etc.
	int				gvc;						// game-version compatibility
} pack_t;

typedef struct {
	char		path[MAX_OSPATH];		// c:\jk2
	char		gamedir[MAX_OSPATH];	// base
} directory_t;

typedef struct searchpath_s {
	struct searchpath_s *next;

	pack_t		*pack;		// only one of pack / dir will be non NULL
	directory_t	*dir;
} searchpath_t;

static	char		fs_gamedir[MAX_OSPATH];	// this will be a single file name with no separators
static	cvar_t		*fs_debug;
static	cvar_t		*fs_homepath;
static	cvar_t		*fs_basepath;
static	cvar_t		*fs_assetspath;
static	cvar_t		*fs_basegame;
static	cvar_t		*fs_copyfiles;
static	cvar_t		*fs_gamedirvar;
static	searchpath_t	*fs_searchpaths;
static	int			fs_readCount;			// total bytes read
static	int			fs_loadCount;			// total files read
static	int			fs_loadStack;			// total files in memory
static	int			fs_packFiles;			// total number of files in packs

static int fs_fakeChkSum;
static int fs_checksumFeed;

typedef union qfile_gus {
	FILE*		o;
	unzFile		z;
} qfile_gut;

typedef struct qfile_us {
	qfile_gut	file;
	qboolean	unique;
} qfile_ut;

typedef struct {
	qfile_ut	handleFiles;
	qboolean	handleSync;
	int			baseOffset;
	int			fileSize;
	int			zipFilePos;
	int			zipFileLen;
	qboolean	zipFile;
	module_t	module;
	char		name[MAX_ZPATH];
} fileHandleData_t;

static fileHandleData_t	fsh[MAX_FILE_HANDLES];

// never load anything from pk3 files that are not present at the server when pure
static int			fs_numServerPaks;
static int			fs_serverPaks[MAX_SEARCH_PATHS];				// checksums
static const char	*fs_serverPakNames[MAX_SEARCH_PATHS];			// pk3 names

// only used for autodownload, to make sure the client has at least
// all the pk3 files that are referenced at the server side
static int			fs_numServerReferencedPaks;
static int			fs_serverReferencedPaks[MAX_SEARCH_PATHS];			// checksums
static const char	*fs_serverReferencedPakNames[MAX_SEARCH_PATHS];		// pk3 names

// last valid game folder used
char lastValidBase[MAX_OSPATH];
char lastValidGame[MAX_OSPATH];

qboolean FS_idPak(pack_t *pack);

static const char * const moduleName[MODULE_MAX] = {
	"Main",
	"Renderer",
	"FX",
	"BotLib",
	"Game",
	"CGame",
	"UI"
};

static inline qboolean FS_CheckHandle(const char *fname, fileHandle_t f, module_t module) {
	if ( f < 1 || f >= MAX_FILE_HANDLES ) {
		Com_DPrintf( S_COLOR_YELLOW "%s (%s module): handle out of range\n",
			fname, moduleName[module] );
		assert(module == MODULE_GAME || module == MODULE_CGAME || module == MODULE_UI);
		return qfalse;
	}
	if ( fsh[f].module != module ) {
		Com_DPrintf( S_COLOR_YELLOW "%s (%s module): access violation\n",
			fname, moduleName[module] );
		assert(module == MODULE_GAME || module == MODULE_CGAME || module == MODULE_UI);
		return qfalse;
	}
	return qtrue;
}

#define FS_CHECKHANDLE(f, module, retval) if (!FS_CheckHandle(__FUNCTION__, f, module)) return retval;

/*
==============
FS_Initialized
==============
*/

qboolean FS_Initialized() {
	return (qboolean)(fs_searchpaths != NULL);
}

/*
=================
FS_PakIsPure
=================
*/
qboolean FS_PakIsPure( pack_t *pack ) {
	int i;

	// actually, I created a bypass for sv_pure here but since jk2 is opensource I really don't see a point in supporting pure
	if (!Q_stricmp(pack->pakBasename, "assets2") || !Q_stricmp(pack->pakBasename, "assets5") || !Q_stricmp(pack->pakBasename, "assetsmv") || !Q_stricmp(pack->pakBasename, "assetsmv2"))
		return qtrue;

	if ( fs_numServerPaks ) {
    // NOTE TTimo we are matching checksums without checking the pak names
    //   this means you can have the same pk3 as the server under a different name, you will still get through sv_pure validation
    //   (what happens when two pk3's have the same checkums? is it a likely situation?)
	//   also, if there's a wrong checksumed pk3 and autodownload is enabled, the checksum will be appended to the downloaded pk3 name
		for ( i = 0 ; i < fs_numServerPaks ; i++ ) {
			// FIXME: also use hashed file names
			if ( pack->checksum == fs_serverPaks[i] ) {
				return qtrue;		// on the aproved list
			}
		}
		return qfalse;	// not on the pure server pak list
	}
	return qtrue;
}


/*
=================
FS_LoadStack
return load stack
=================
*/
int FS_LoadStack()
{
	return fs_loadStack;
}

/*
================
return a hash value for the filename
================
*/
static int FS_HashFileName( const char *fname, int hashSize ) {
	int		i;
	int	hash;
	char	letter;

	hash = 0;
	i = 0;
	while (fname[i] != '\0') {
		letter = tolower(fname[i]);
		if (letter =='.') break;				// don't include extension
		if (letter =='\\') letter = '/';		// damn path names
		if (letter == PATH_SEP) letter = '/';		// damn path names
		hash+=(int)(letter)*(i+119);
		i++;
	}
	hash = (hash ^ (hash >> 10) ^ (hash >> 20));
	hash &= (hashSize-1);
	return hash;
}

static fileHandle_t	FS_HandleForFile(void) {
	int		i;

	for ( i = 1 ; i < MAX_FILE_HANDLES ; i++ ) {
		if ( fsh[i].handleFiles.file.o == NULL ) {
			Com_Memset(&fsh[i], 0, sizeof(fsh[0]));
			return i;
		}
	}
	Com_Error( ERR_DROP, "FS_HandleForFile: none free" );
	return 0;
}

static FILE	*FS_FileForHandle( fileHandle_t f, module_t module = MODULE_MAIN ) {
	if ( f < 1 || f >= MAX_FILE_HANDLES ) {
		Com_Error( ERR_DROP, "FS_FileForHandle (%s module): out of reange", moduleName[module] );
	}
	if (fsh[f].zipFile == qtrue) {
		Com_Error( ERR_DROP, "FS_FileForHandle (%s module): can't get FILE on zip file", moduleName[module] );
	}
	if ( ! fsh[f].handleFiles.file.o ) {
		Com_Error( ERR_DROP, "FS_FileForHandle (%s module): NULL", moduleName[module] );
	}
	if ( fsh[f].module != module ) {
		Com_Error( ERR_DROP, "FS_FileForHandle (%s module): access violation", moduleName[module] );
	}

	return fsh[f].handleFiles.file.o;
}

void	FS_ForceFlush( fileHandle_t f, module_t module ) {
	FILE *file;

	file = FS_FileForHandle(f, module);
	setvbuf( file, NULL, _IONBF, 0 );
}

/*
================
FS_filelength

If this is called on a non-unique FILE (from a pak file),
it will return the size of the pak file, not the expected
size of the file.
================
*/
int FS_filelength( fileHandle_t f, module_t module ) {
	int		pos;
	int		end;
	FILE*	h;

	h = FS_FileForHandle(f, module);
	pos = ftell (h);
	fseek (h, 0, SEEK_END);
	end = ftell (h);
	fseek (h, pos, SEEK_SET);

	return end;
}

/*
====================
FS_ReplaceSeparators

Fix things up differently for win/unix/mac
====================
*/
static void FS_ReplaceSeparators( char *path ) {
	char	*s;

	for ( s = path ; *s ; s++ ) {
		if ( *s == '/' || *s == '\\' ) {
			*s = PATH_SEP;
		}
	}
}

/*
===================
FS_BuildOSPath

Qpath may have either forward or backwards slashes
===================
*/
char *FS_BuildOSPath( const char *base, const char *game, const char *qpath ) {
	char	temp[MAX_OSPATH];
	static char ospath[2][MAX_OSPATH];
	static int toggle;

	toggle ^= 1;		// flip-flop to allow two returns without clash

	if( !game || !game[0] ) {
		game = fs_gamedir;
	}

	Com_sprintf( temp, sizeof(temp), "/%s/%s", game, qpath );
	FS_ReplaceSeparators( temp );
	Com_sprintf( ospath[toggle], sizeof( ospath[0] ), "%s%s", base, temp );

	return ospath[toggle];
}

char *FS_BuildOSPath(const char *base, const char *path) {
	char	temp[MAX_OSPATH];
	static char ospath[2][MAX_OSPATH];
	static int toggle;

	toggle ^= 1;		// flip-flop to allow two returns without clash

	Com_sprintf(temp, sizeof(temp), "/%s", path);
	FS_ReplaceSeparators(temp);
	Com_sprintf(ospath[toggle], sizeof(ospath[0]), "%s%s", base, temp);

	return ospath[toggle];
}

/*
============
FS_CreatePath

Creates any directories needed to store the given filename
============
*/
qboolean FS_CreatePath (char *OSPath) {
	char	*ofs;

	// make absolutely sure that it can't back up the path
	// FIXME: is c: allowed???
	if ( strstr( OSPath, ".." ) || strstr( OSPath, "::" ) ) {
		Com_Printf( "WARNING: refusing to create relative path \"%s\"\n", OSPath );
		return qtrue;
	}

	for (ofs = OSPath+1 ; *ofs ; ofs++) {
		if (*ofs == PATH_SEP) {
			// create the directory
			*ofs = 0;
			Sys_Mkdir (OSPath);
			*ofs = PATH_SEP;
		}
	}
	return qfalse;
}

/*
=================
FS_CopyFile

Copy a fully specified file from one place to another
=================
*/
qboolean FS_CopyFile( char *fromOSPath, char *toOSPath, char *newOSPath, const int newSize ) {
	FILE	*f;
	long	len;
	byte	*buf;
	int		fileCount = 1;
	char	*lExt, nExt[MAX_OSPATH];
	char	stripped[MAX_OSPATH];

	Com_DPrintf( "copy %s to %s\n", fromOSPath, toOSPath );

	if (strstr(fromOSPath, "journal.dat") || strstr(fromOSPath, "journaldata.dat")) {
		Com_Printf( "Ignoring journal files\n");
		return qfalse;
	}

	f = fopen( fromOSPath, "rb" );
	if ( !f ) {
		char *testpath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, fromOSPath );
		f = fopen( testpath, "rb" );
		if ( !f ) {
			return qfalse;
		}
	}
	fseek (f, 0, SEEK_END);
	len = ftell (f);
	if (len < 0)
		Com_Error( ERR_FATAL, "ftell() error in FS_Copyfiles()" );
	fseek (f, 0, SEEK_SET);

	// we are using direct malloc instead of Z_Malloc here, so it
	// probably won't work on a mac... Its only for developers anyway...
	buf = (byte *)Z_Malloc( len, TAG_FILESYS, qfalse );
	if (fread( buf, 1, len, f ) != (size_t)len)
		Com_Error( ERR_FATAL, "Short read in FS_Copyfiles()" );
	fclose( f );

	if( FS_CreatePath( toOSPath ) ) {
		char *testpath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, toOSPath );
		if( FS_CreatePath( testpath ) ) {
			Z_Free(buf);
			return qfalse;
		}
	}
	
	f = fopen( toOSPath, "rb" );
	if ( !f ) {
		char *testpath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, toOSPath );
		f = fopen( testpath, "rb" );
	}

	// if the file exists then create a new one with (N)
	if (f && newOSPath && newSize > 0) {
		qboolean localFound = qfalse;
		fclose(f);
		lExt = strchr(toOSPath, '.');
		if (!lExt) {
			lExt = "";
		}
		while (strchr(lExt+1, '.')) {
			lExt = strchr(lExt+1, '.');
		}
		Q_strncpyz(nExt, lExt, sizeof(nExt));
		COM_StripExtension(toOSPath, stripped, sizeof(stripped));
		fileCount++;
		while ((f = fopen(va("%s (%i)%s", stripped, fileCount, nExt), "rb")) != NULL) {
			fileCount++;
			fclose(f);
			localFound = qtrue;
		}
		if (!localFound) {
			char *testpath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, toOSPath );
			COM_StripExtension(testpath, stripped, sizeof(stripped));
			while ((f = fopen(va("%s (%i)%s", stripped, fileCount, nExt), "rb")) != NULL) {
				fileCount++;
				fclose(f);
			}
		}
	}
	if (fileCount > 1 && newOSPath && newSize > 0) {
		Q_strncpyz(newOSPath, va("%s (%i)%s", stripped, fileCount, nExt), newSize);
		f = fopen(newOSPath, "wb");
		if ( !f ) {
			char *testpath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, newOSPath );
			f = fopen(testpath, "wb");
		}
	} else {
		if (newOSPath && newSize > 0) {
			Q_strncpyz(newOSPath, "", newSize);
		}
		f = fopen(toOSPath, "wb");
		if ( !f ) {
			char *testpath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, toOSPath );
			f = fopen(testpath, "wb");
		}
	}
	if ( !f ) {
		Z_Free(buf);
		return qfalse;
	}
	if (fwrite( buf, 1, len, f ) != (size_t)len)
		Com_Error( ERR_FATAL, "Short write in FS_Copyfiles()" );
	fclose( f );
	Z_Free( buf );
	return qtrue;
}

/*
===========
FS_Remove

===========
*/
static void FS_Remove( const char *osPath ) {
	remove( osPath );
}

/*
===========
FS_HomeRemove

===========
*/
void FS_HomeRemove( const char *homePath ) {
	remove( FS_BuildOSPath( fs_homepath->string,
				fs_gamedir, homePath ) );
}

/*
================
FS_FileExists

Tests if the file exists in the current gamedir, this DOES NOT
search the paths.  This is to determine if opening a file to write
(which always goes into the current gamedir) will cause any overwrites.
NOTE TTimo: this goes with FS_FOpenFileWrite for opening the file afterwards
================
*/
qboolean FS_FileExists( const char *file )
{
	FILE *f;
	char *testpath;

	testpath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, file );

	f = fopen( testpath, "rb" );
	if (f) {
		fclose( f );
		return qtrue;
	}
	return qfalse;
}

qboolean FS_Base_FileExists(const char *file)
{
	FILE *f;
	char *testpath;

	testpath = FS_BuildOSPath(fs_basepath->string, BASEGAME, file);

	f = fopen(testpath, "rb");
	if (f) {
		fclose(f);
		return qtrue;
	}
	return qfalse;
}

qboolean FS_AllPath_Base_FileExists(const char *file)
{
	FILE *f;
	char *testpath;

	testpath = FS_BuildOSPath(fs_basepath->string, BASEGAME, file);
	f = fopen(testpath, "rb");
	if (f) {
		fclose(f);
		return qtrue;
	}

	testpath = FS_BuildOSPath(fs_homepath->string, BASEGAME, file);
	f = fopen(testpath, "rb");
	if (f) {
		fclose(f);
		return qtrue;
	}

#if !defined(PORTABLE)
	if (strlen(fs_assetspath->string)) {
		testpath = FS_BuildOSPath(fs_assetspath->string, BASEGAME, file);
		f = fopen(testpath, "rb");
		if (f) {
			fclose(f);
			return qtrue;
		}
	}
#endif

	return qfalse;
}

qboolean FS_BaseHome_Base_FileExists(const char *file) {
	FILE *f;
	char *testpath;

	testpath = FS_BuildOSPath(fs_basepath->string, BASEGAME, file);
	f = fopen(testpath, "rb");
	if (f) {
		fclose(f);
		return qtrue;
	}

	testpath = FS_BuildOSPath(fs_homepath->string, BASEGAME, file);
	f = fopen(testpath, "rb");
	if (f) {
		fclose(f);
		return qtrue;
	}

	return qfalse;
}

/*
================
FS_SV_FileExists

Tests if the file exists
================
*/
qboolean FS_SV_FileExists( const char *file )
{
	FILE *f;
	char *testpath;

	testpath = FS_BuildOSPath( fs_homepath->string, file, "");
	testpath[strlen(testpath)-1] = '\0';

	f = fopen( testpath, "rb" );
	if (f) {
		fclose( f );
		return qtrue;
	}
	return qfalse;
}


/*
===========
FS_SV_FOpenFileWrite

===========
*/
fileHandle_t FS_SV_FOpenFileWrite( const char *filename, module_t module ) {
	char *ospath;
	fileHandle_t	f;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	ospath = FS_BuildOSPath( fs_homepath->string, filename, "" );
	ospath[strlen(ospath)-1] = '\0';

	f = FS_HandleForFile();
	fsh[f].module = module;
	fsh[f].zipFile = qfalse;

	if ( fs_debug->integer ) {
		Com_Printf( "FS_SV_FOpenFileWrite: %s\n", ospath );
	}

	if( FS_CreatePath( ospath ) ) {
		return 0;
	}

	Com_DPrintf( "writing to: %s\n", ospath );
	fsh[f].handleFiles.file.o = fopen( ospath, "wb" );

	Q_strncpyz( fsh[f].name, filename, sizeof( fsh[f].name ) );

	fsh[f].handleSync = qfalse;
	if (!fsh[f].handleFiles.file.o) {
		f = 0;
	}
	return f;
}

/*
===========
FS_SV_FOpenFileAppend

===========
*/
fileHandle_t FS_SV_FOpenFileAppend( const char *filename, module_t module ) {
	char			*ospath;
	fileHandle_t	f;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	f = FS_HandleForFile();
	fsh[f].module = module;
	fsh[f].zipFile = qfalse;

	Q_strncpyz( fsh[f].name, filename, sizeof( fsh[f].name ) );

	ospath = FS_BuildOSPath( fs_homepath->string, filename );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_SV_FOpenFileAppend: %s\n", ospath );
	}

	if( FS_CreatePath( ospath ) ) {
		return 0;
	}

	fsh[f].handleFiles.file.o = fopen( ospath, "ab" );
	fsh[f].handleSync = qfalse;

	if (!fsh[f].handleFiles.file.o) {
		return 0;
	}

	return f;
}

/*
===========
FS_SV_FOpenFileRead
search for a file somewhere below the home path, base path or cd path
we search in that order, matching FS_SV_FOpenFileRead order
===========
*/
int FS_SV_FOpenFileRead( const char *filename, fileHandle_t *fp, module_t module ) {
	char *ospath;
	fileHandle_t	f = 0; // bk001129 - from cvs1.17

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	f = FS_HandleForFile();
	fsh[f].module = module;
	fsh[f].zipFile = qfalse;

	Q_strncpyz( fsh[f].name, filename, sizeof( fsh[f].name ) );

	// don't let sound stutter
	S_ClearSoundBuffer();

	// search homepath
	ospath = FS_BuildOSPath( fs_homepath->string, filename, "" );
	// remove trailing slash
	ospath[strlen(ospath)-1] = '\0';

	if ( fs_debug->integer ) {
		Com_Printf( "FS_SV_FOpenFileRead (fs_homepath): %s\n", ospath );
	}

	fsh[f].handleFiles.file.o = fopen( ospath, "rb" );
	fsh[f].handleSync = qfalse;
	if (!fsh[f].handleFiles.file.o)
	{
		// NOTE TTimo on non *nix systems, fs_homepath == fs_basepath, might want to avoid
		if (Q_stricmp(fs_homepath->string, fs_basepath->string))
		{
			// search basepath
			ospath = FS_BuildOSPath( fs_basepath->string, filename, "" );
			ospath[strlen(ospath)-1] = '\0';

			if ( fs_debug->integer )
			{
				Com_Printf( "FS_SV_FOpenFileRead (fs_basepath): %s\n", ospath );
			}

			fsh[f].handleFiles.file.o = fopen( ospath, "rb" );
			fsh[f].handleSync = qfalse;
		}

		if (!fsh[f].handleFiles.file.o) {
			f = 0;
		}
	}

	*fp = f;
	if (f) {
		return FS_filelength(f, module);
	}
	return 0;
}


/*
===========
FS_SV_Rename

===========
*/
void FS_SV_Rename( const char *from, const char *to ) {
	char			*from_ospath, *to_ospath;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	// don't let sound stutter
	S_ClearSoundBuffer();

	from_ospath = FS_BuildOSPath( fs_homepath->string, from, "" );
	to_ospath = FS_BuildOSPath( fs_homepath->string, to, "" );
	from_ospath[strlen(from_ospath)-1] = '\0';
	to_ospath[strlen(to_ospath)-1] = '\0';

	if ( fs_debug->integer ) {
		Com_Printf( "FS_SV_Rename: %s --> %s\n", from_ospath, to_ospath );
	}

	if (rename( from_ospath, to_ospath )) {
		// Failed, try copying it and deleting the original
		FS_CopyFile ( from_ospath, to_ospath );
		FS_Remove ( from_ospath );
	}
}



/*
===========
FS_Rename

===========
*/
void FS_Rename( const char *from, const char *to ) {
	char			*from_ospath, *to_ospath;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	// don't let sound stutter
	S_ClearSoundBuffer();

	from_ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, from );
	to_ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, to );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_Rename: %s --> %s\n", from_ospath, to_ospath );
	}

	if (rename( from_ospath, to_ospath )) {
		// Failed, try copying it and deleting the original
		FS_CopyFile ( from_ospath, to_ospath );
		FS_Remove ( from_ospath );
	}
}

/*
==============
FS_FCloseFile

If the FILE pointer is an open pak file, leave it open.

For some reason, other dll's can't just cal fclose()
on files returned by FS_FOpenFile...
==============
*/
void FS_FCloseFile( fileHandle_t f, module_t module ) {
	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	FS_CHECKHANDLE(f, module, )

	if (fsh[f].zipFile == qtrue) {
		unzCloseCurrentFile( fsh[f].handleFiles.file.z );
		if ( fsh[f].handleFiles.unique ) {
			unzClose( fsh[f].handleFiles.file.z );
		}
		Com_Memset( &fsh[f], 0, sizeof( fsh[f] ) );
		return;
	}

	// we didn't find it as a pak, so close it as a unique file
	if (fsh[f].handleFiles.file.o) {
		fclose (fsh[f].handleFiles.file.o);
	}
	Com_Memset( &fsh[f], 0, sizeof( fsh[f] ) );
}

/*
===========
FS_FOpenFileWrite

===========
*/
fileHandle_t FS_FOpenFileWrite( const char *filename, module_t module ) {
	char			*ospath;
	fileHandle_t	f;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	f = FS_HandleForFile();
	fsh[f].module = module;
	fsh[f].zipFile = qfalse;

	ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, filename );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_FOpenFileWrite: %s\n", ospath );
	}

	if( FS_CreatePath( ospath ) ) {
		return 0;
	}

	// enabling the following line causes a recursive function call loop
	// when running with +set logfile 1 +set developer 1
	//Com_DPrintf( "writing to: %s\n", ospath );
	fsh[f].handleFiles.file.o = fopen( ospath, "wb" );

	Q_strncpyz( fsh[f].name, filename, sizeof( fsh[f].name ) );

	fsh[f].handleSync = qfalse;
	if (!fsh[f].handleFiles.file.o) {
		f = 0;
	}
	return f;
}

fileHandle_t FS_FOpenBaseFileWrite(const char *filename, module_t module) {
	char			*ospath;
	fileHandle_t	f;

	if (!fs_searchpaths) {
		Com_Error(ERR_FATAL, "Filesystem call made without initialization");
	}

	f = FS_HandleForFile();
	fsh[f].module = module;
	fsh[f].zipFile = qfalse;

	ospath = FS_BuildOSPath(fs_homepath->string, "base", filename);

	if (fs_debug->integer) {
		Com_Printf("FS_FOpenFileWrite: %s\n", ospath);
	}

	if (FS_CreatePath(ospath)) {
		return 0;
	}

	// enabling the following line causes a recursive function call loop
	// when running with +set logfile 1 +set developer 1
	//Com_DPrintf( "writing to: %s\n", ospath );
	fsh[f].handleFiles.file.o = fopen(ospath, "wb");

	Q_strncpyz(fsh[f].name, filename, sizeof(fsh[f].name));

	fsh[f].handleSync = qfalse;
	if (!fsh[f].handleFiles.file.o) {
		f = 0;
	}
	return f;
}

/*
===========
FS_FOpenFileAppend

===========
*/
fileHandle_t FS_FOpenFileAppend( const char *filename, module_t module ) {
	char			*ospath;
	fileHandle_t	f;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	f = FS_HandleForFile();
	fsh[f].module = module;
	fsh[f].zipFile = qfalse;

	Q_strncpyz( fsh[f].name, filename, sizeof( fsh[f].name ) );

	// don't let sound stutter
	S_ClearSoundBuffer();

	ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, filename );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_FOpenFileAppend: %s\n", ospath );
	}

	if( FS_CreatePath( ospath ) ) {
		return 0;
	}

	fsh[f].handleFiles.file.o = fopen( ospath, "ab" );
	fsh[f].handleSync = qfalse;
	if (!fsh[f].handleFiles.file.o) {
		f = 0;
	}
	return f;
}

/*
===========
FS_IsFifo

Check if file is a named pipe (FIFO)
===========
*/
qboolean FS_IsFifo( const char *filename ) {
	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

#ifdef _MSC_VER
	// S_ISFIFO macro is missing in msvc
	return qfalse;
#else
	char *ospath;
	struct stat f_stat;

	ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, filename );

	if ( stat(ospath, &f_stat) == -1 ) {
		return qfalse;
	}

	return (qboolean)!!S_ISFIFO(f_stat.st_mode);
#endif
}

/*
===========
FS_FLock

Advisory file locking
===========
*/
int FS_FLock( fileHandle_t h, flockCmd_t cmd, qboolean nb, module_t module ) {
	int fd = fileno( FS_FileForHandle(h, module) );
	return Sys_FLock(fd, cmd, nb);
}


/*
===========
FS_FilenameCompare

Ignore case and seprator char distinctions
===========
*/
int FS_FilenameCompare( const char *s1, const char *s2 ) {
	int		c1, c2;

	do {
		c1 = *s1++;
		c2 = *s2++;

		if (c1 >= 'a' && c1 <= 'z') {
			c1 -= ('a' - 'A');
		}
		if (c2 >= 'a' && c2 <= 'z') {
			c2 -= ('a' - 'A');
		}

		if ( c1 == '\\' || c1 == ':' ) {
			c1 = '/';
		}
		if ( c2 == '\\' || c2 == ':' ) {
			c2 = '/';
		}

		if (c1 != c2) {
			return -1;		// strings not equal
		}
	} while (c1);

	return 0;		// strings are equal
}

const char *get_filename_ext(const char *filename) {
	const char *dot = strrchr(filename, '.');
	if (!dot || dot == filename) return "";
	return dot + 1;
}

/*
===========
FS_PakReadFile

Reads a file from specified pak_t and saves it's data in buffer
===========
*/
int FS_PakReadFile(pack_t *pak, const char *filename, char *buffer, int bufferlen) {
	int hash, len;
	fileInPack_t *pakFile;

	hash = FS_HashFileName(filename, pak->hashSize);
	pakFile = pak->hashTable[hash];
	if (pakFile) {
		do {
			// case and separator insensitive comparisons
			if (!FS_FilenameCompare(pakFile->name, filename)) {
				unzSetOffset(pak->handle, pakFile->pos);
				unzOpenCurrentFile(pak->handle);
				len = unzReadCurrentFile(pak->handle, buffer, bufferlen);
				unzCloseCurrentFile(pak->handle);

				return len;
			}

			pakFile = pakFile->next;
		} while (pakFile != NULL);
	}

	return 0;
}


/*
===========
FS_FOpenFileRead

Finds the file in the search path.
Returns filesize and an open FILE pointer.
Used for streaming data out of either a
separate file or a ZIP file.
===========
*/
extern qboolean		com_fullyInitialized;

int FS_FOpenFileRead(const char *filename, fileHandle_t *file, qboolean uniqueFILE, module_t module) {
	return FS_FOpenFileReadHash(filename, file, uniqueFILE, NULL, module);
}

int FS_FOpenFileReadHash(const char *filename, fileHandle_t *file, qboolean uniqueFILE, unsigned long *filehash, module_t module) {
	bool			isLocalConfig;
	searchpath_t	*search;
	char			*netpath;
	pack_t			*pak;
	fileInPack_t	*pakFile;
	directory_t		*dir;
	int			hash;
	int				l;
	char demoExt[16];

	hash = 0;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( file == NULL ) {
		Com_Error( ERR_FATAL, "FS_FOpenFileRead: NULL 'file' parameter passed" );
	}

	if ( !filename ) {
		Com_Error( ERR_FATAL, "FS_FOpenFileRead: NULL 'filename' parameter passed" );
	}

	Com_sprintf (demoExt, sizeof(demoExt), ".dm_%d", MV_GetCurrentProtocol());
	// qpaths are not supposed to have a leading slash
	if ( filename[0] == '/' || filename[0] == '\\' ) {
		filename++;
	}

	// make absolutely sure that it can't back up the path.
	// The searchpaths do guarantee that something will always
	// be prepended, so we don't need to worry about "c:" or "//limbo" i
	if ( strstr( filename, ".." ) || strstr( filename, "::" ) ) {
		*file = 0;
		return -1;
	}

	isLocalConfig = ( !strcmp( filename, "autoexec.cfg" ) ||
		!strcmp( filename, "jk2mvconfig.cfg" ) ||
		!strcmp( filename, "jk2mvserver.cfg" ) ||
		!strcmp( filename, "jk2mvglobal.cfg" ) );

	//
	// search through the path, one element at a time
	//

	*file = FS_HandleForFile();
	fsh[*file].module = module;
	fsh[*file].handleFiles.unique = uniqueFILE;

	for ( search = fs_searchpaths ; search ; search = search->next ) {
		//
		if ( isLocalConfig && search->pack ) {
			continue;
		}
		//
		if ( search->pack ) {
			hash = FS_HashFileName(filename, search->pack->hashSize);
		}
		// is the element a pak file?
		if ( search->pack && search->pack->hashTable[hash] ) {
			// disregard if it doesn't match one of the allowed pure pak files
			if ( !FS_PakIsPure(search->pack) ) {
				continue;
			}

			// version specific pk3's
			// downloaded files are always okey because they are only loaded on servers currently using them
			if (Q_stricmpn(search->pack->pakBasename, "dl_", 3) &&
				!((search->pack->gvc & PACKGVC_1_02 && MV_GetCurrentGameversion() == VERSION_1_02) ||
				  (search->pack->gvc & PACKGVC_1_03 && MV_GetCurrentGameversion() == VERSION_1_03) ||
				  (search->pack->gvc & PACKGVC_1_04 && MV_GetCurrentGameversion() == VERSION_1_04) ||
				  (Q_stricmp(search->pack->pakGamename, BASEGAME) && search->pack->gvc == PACKGVC_UNKNOWN) ||
				  (MV_GetCurrentGameversion() == VERSION_UNDEF))) {

				// prevent loading unsupported qvm's
				if (!Q_stricmp(filename, "vm/cgame.qvm") || !Q_stricmp(filename, "vm/ui.qvm") || !Q_stricmp(filename, "vm/jk2mpgame.qvm"))
					continue;

				// incompatible pk3
				if (search->pack->gvc != PACKGVC_UNKNOWN && !FS_idPak(search->pack))
					continue;
			}

			// patchfiles are only allowed from within assetsmv.pk3
			if (!Q_stricmp(get_filename_ext(filename), "menu_patch") && Q_stricmp(search->pack->pakBasename, "assetsmv")) {
				continue;
			}

#ifdef NTCLIENT_WORKAROUND
			// this should do the trick for the moment
			if (!Q_stricmpn(search->pack->pakGamename, "nt", 2) && !Q_stricmpn(search->pack->pakBasename, "dl_nt", 5) &&
				!Q_stricmp(filename, "vm/ui.qvm")) {
				continue;
			}
#endif

			// look through all the pak file elements
			pak = search->pack;
			pakFile = pak->hashTable[hash];
			do {
				// case and separator insensitive comparisons
				if ( !FS_FilenameCompare( pakFile->name, filename ) ) {
					// found it!

					// reference lists
					if ( !pak->noref ) {
						// JK2MV automatically references pk3's in three cases:
						// 1. A .bsp file is loaded from it (and thus it is expected to be a map)
						// 2. cgame.qvm or ui.qvm is loaded from it (expected to be a clientside)
						// 3. pk3 is located in fs_game != base (standard jk2 behavior)
						// All others need to be referenced manually by the use of reflists.
						
						if (!Q_stricmp(get_filename_ext(filename), "bsp")) {
							pak->referenced |= FS_GENERAL_REF;
						}

						if (!Q_stricmp(filename, "vm/cgame.qvm")) {
							pak->referenced |= FS_CGAME_REF;
						}

						if (!Q_stricmp(filename, "vm/ui.qvm")) {
							pak->referenced |= FS_UI_REF;
						}
					}

					if (uniqueFILE)
					{
						// open a new file on the pakfile
						fsh[*file].handleFiles.file.z = unzOpen(pak->pakFilename);

						if (fsh[*file].handleFiles.file.z == NULL)
							Com_Error(ERR_FATAL, "Couldn't open %s", pak->pakFilename);
					} else
						fsh[*file].handleFiles.file.z = pak->handle;

					Q_strncpyz(fsh[*file].name, filename, sizeof(fsh[*file].name));
					fsh[*file].zipFile = qtrue;

					// set the file position in the zip file (also sets the current file info)
					unzSetOffset(fsh[*file].handleFiles.file.z, pakFile->pos);

					// open the file in the zip
					unzOpenCurrentFile(fsh[*file].handleFiles.file.z);
					fsh[*file].zipFilePos = pakFile->pos;
					fsh[*file].zipFileLen = pakFile->len;

					if ( fs_debug->integer ) {
						Com_Printf( "FS_FOpenFileRead: %s (found in '%s')\n",
							filename, pak->pakFilename );
					}
#ifndef DEDICATED
#ifndef FINAL_BUILD
					// Check for unprecached files when in game but not in the menus
					if((cls.state == CA_ACTIVE) && !(cls.keyCatchers & KEYCATCH_UI))
					{
						Com_Printf(S_COLOR_YELLOW "WARNING: File %s not precached\n", filename);
					}
#endif
#endif // DEDICATED

					// return the hash of the file
					if (filehash) {
						unz_file_info fi;

						if (!unzGetCurrentFileInfo(fsh[*file].handleFiles.file.z, &fi, NULL, 0, NULL, 0, NULL, 0)) {
							*filehash = fi.crc;
						}
					}

					return pakFile->len;
				}
				pakFile = pakFile->next;
			} while(pakFile != NULL);
		} else if ( search->dir ) {
			// check a file in the directory tree

			// if we are running restricted, the only files we
			// will allow to come from the directory are .cfg files
			l = (int)strlen( filename );
	  // FIXME TTimo I'm not sure about the fs_numServerPaks test
      // if you are using FS_ReadFile to find out if a file exists,
      //   this test can make the search fail although the file is in the directory
      // I had the problem on https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=8
      // turned out I used FS_FileExists instead
			if ( fs_numServerPaks ) {

				if ( Q_stricmp( filename + l - 4, ".cfg" )		// for config files
					&& Q_stricmp( filename + l - 4, ".fcf" )	// force configuration files
					&& Q_stricmp( filename + l - 5, ".menu" )	// menu files
					&& Q_stricmp( filename + l - 5, ".game" )	// menu files
					&& Q_stricmp( filename + l - strlen(demoExt), demoExt )	// menu files
					&& Q_stricmp( filename + l - 4, ".dat" ) ) {	// for journal files
					continue;
				}
			}

			dir = search->dir;

			netpath = FS_BuildOSPath( dir->path, dir->gamedir, filename );
			fsh[*file].handleFiles.file.o = fopen (netpath, "rb");
			if ( !fsh[*file].handleFiles.file.o ) {
				continue;
			}

			if ( Q_stricmp( filename + l - 4, ".cfg" )		// for config files
				&& Q_stricmp( filename + l - 4, ".fcf" )	// force configuration files
				&& Q_stricmp( filename + l - 5, ".menu" )	// menu files
				&& Q_stricmp( filename + l - 5, ".game" )	// menu files
				&& Q_stricmp( filename + l - strlen(demoExt), demoExt )	// menu files
				&& Q_stricmp( filename + l - 4, ".dat" ) ) {	// for journal files
				fs_fakeChkSum = qrandom();
			}

			Q_strncpyz( fsh[*file].name, filename, sizeof( fsh[*file].name ) );
			fsh[*file].zipFile = qfalse;
			if ( fs_debug->integer ) {
				Com_Printf( "FS_FOpenFileRead: %s (found in '%s/%s')\n", filename,
					dir->path, dir->gamedir );
			}

#ifndef DEDICATED
#ifndef FINAL_BUILD
			// Check for unprecached files when in game but not in the menus
			if((cls.state == CA_ACTIVE) && !(cls.keyCatchers & KEYCATCH_UI))
			{
				Com_Printf(S_COLOR_YELLOW "WARNING: File %s not precached\n", filename);
			}
#endif
#endif // dedicated
			return FS_filelength (*file, module);
		}
	}

	if ( fs_debug->integer ) {
		Com_DPrintf ("FS_FOpenFileReadHash: Can't find %s\n", filename);
	}
	*file = 0;
	return -1;
}

/*
=================
FS_Read

Properly handles partial reads
=================
*/
int FS_Read2( void *buffer, int len, fileHandle_t f, module_t module ) {
	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	FS_CHECKHANDLE(f, module, 0);

	return FS_Read( buffer, len, f, module );
}

int FS_Read( void *buffer, int len, fileHandle_t f, module_t module ) {
	size_t		block, remaining;
	size_t		read;
	byte	*buf;
	int		tries;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	FS_CHECKHANDLE(f, module, 0)

	buf = (byte *)buffer;
	fs_readCount += len;

	if (fsh[f].zipFile == qfalse) {
		remaining = len;
		tries = 0;
		while (remaining) {
			block = remaining;
			read = fread (buf, 1, block, fsh[f].handleFiles.file.o);
			if (read == 0) {
				// we might have been trying to read from a CD, which
				// sometimes returns a 0 read on windows
				if (!tries) {
					tries = 1;
				} else {
					return len - (int)remaining;	//Com_Error (ERR_FATAL, "FS_Read: 0 bytes read");
				}
			}

			remaining -= read;
			buf += read;
		}
		return len;
	} else {
		return unzReadCurrentFile(fsh[f].handleFiles.file.z, buffer, len);
	}
}

/*
=================
FS_Write

Properly handles partial writes
=================
*/
int FS_Write( const void *buffer, int len, fileHandle_t h, module_t module ) {
	size_t		block, remaining;
	size_t		written;
	const byte	*buf;
	int		tries;
	FILE	*f;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	FS_CHECKHANDLE(h, module, 0)

	f = FS_FileForHandle(h, module);
	buf = (const byte *)buffer;

	remaining = len;
	tries = 0;
	while (remaining) {
		block = remaining;
		written = fwrite (buf, 1, block, f);
		if (written == 0) {
			if (!tries) {
				tries = 1;
			} else {
				Com_Printf( "FS_Write: 0 bytes written\n" );
				return 0;
			}
		}

		remaining -= written;
		buf += written;
	}
	if ( fsh[h].handleSync ) {
		fflush( f );
	}
	return len;
}

#define	MAXPRINTMSG	4096
void QDECL FS_Printf( fileHandle_t h, const char *fmt, ... ) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start (argptr,fmt);
	Q_vsnprintf (msg,sizeof(msg),fmt,argptr);
	va_end (argptr);

	FS_Write(msg, (int)strlen(msg), h, MODULE_MAIN);
}

/*
=================
FS_Seek

=================
*/
int FS_Seek( fileHandle_t f, int offset, int origin, module_t module ) {
	int		_origin;
	char	foo[65536];

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
		return -1;
	}

	FS_CHECKHANDLE(f, module, -1)

	if (fsh[f].zipFile == qtrue) {
		if (offset == 0 && origin == FS_SEEK_SET) {
			// set the file position in the zip file (also sets the current file info)
			unzSetOffset(fsh[f].handleFiles.file.z, fsh[f].zipFilePos);
			return unzOpenCurrentFile(fsh[f].handleFiles.file.z);
		} else if (offset<65536) {
			// set the file position in the zip file (also sets the current file info)
			unzSetOffset(fsh[f].handleFiles.file.z, fsh[f].zipFilePos);
			unzOpenCurrentFile(fsh[f].handleFiles.file.z);
			return FS_Read(foo, offset, f, module);
		} else {
			Com_Error( ERR_FATAL, "ZIP FILE FSEEK NOT YET IMPLEMENTED" );
			return -1;
		}
	} else {
		FILE *file;
		file = FS_FileForHandle(f, module);
		switch( origin ) {
		case FS_SEEK_CUR:
			_origin = SEEK_CUR;
			break;
		case FS_SEEK_END:
			_origin = SEEK_END;
			break;
		case FS_SEEK_SET:
			_origin = SEEK_SET;
			break;
		default:
			_origin = SEEK_CUR;
			Com_Error( ERR_FATAL, "Bad origin in FS_Seek" );
			break;
		}

		return fseek( file, offset, _origin );
	}
}


/*
======================================================================================

CONVENIENCE FUNCTIONS FOR ENTIRE FILES

======================================================================================
*/

int	FS_FileIsInPAK(const char *filename, int *pChecksum ) {
	searchpath_t	*search;
	pack_t			*pak;
	fileInPack_t	*pakFile;
	int			hash = 0;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !filename ) {
		Com_Error( ERR_FATAL, "FS_FOpenFileRead: NULL 'filename' parameter passed" );
	}

	// qpaths are not supposed to have a leading slash
	if ( filename[0] == '/' || filename[0] == '\\' ) {
		filename++;
	}

	// make absolutely sure that it can't back up the path.
	// The searchpaths do guarantee that something will always
	// be prepended, so we don't need to worry about "c:" or "//limbo"
	if ( strstr( filename, ".." ) || strstr( filename, "::" ) ) {
		return -1;
	}

	//
	// search through the path, one element at a time
	//

	for ( search = fs_searchpaths ; search ; search = search->next ) {
		//
		if (search->pack) {
			hash = FS_HashFileName(filename, search->pack->hashSize);
		}
		// is the element a pak file?
		if ( search->pack && search->pack->hashTable[hash] ) {
			// disregard if it doesn't match one of the allowed pure pak files
			if ( !FS_PakIsPure(search->pack) ) {
				continue;
			}

			// if scanning for cgame, ui or jk2mpgame and we are in 1.02 mode ignore assets5.pk3 and assets2.pk3
			if (MV_GetCurrentGameversion() == VERSION_1_02 &&
				(!Q_stricmp(filename, "vm/cgame.qvm") || !Q_stricmp(filename, "vm/ui.qvm") || !Q_stricmp(filename, "vm/jk2mpgame.qvm")) &&
				(!Q_stricmp(search->pack->pakBasename, "assets2") || !Q_stricmp(search->pack->pakBasename, "assets5"))) {
				continue;
			}

			// look through all the pak file elements
			pak = search->pack;
			pakFile = pak->hashTable[hash];
			do {
				// case and separator insensitive comparisons
				if ( !FS_FilenameCompare( pakFile->name, filename ) ) {
					if (pChecksum) {
						*pChecksum = pak->pure_checksum;
					}
					return 1;
				}
				pakFile = pakFile->next;
			} while(pakFile != NULL);
		}
	}
	return -1;
}

/*
============
FS_ReadFile

Filename are relative to the quake search path
a null buffer will just return the file length without loading
============
*/
int FS_ReadFile( const char *qpath, void **buffer ) {
	fileHandle_t	h;
	byte*			buf;
	qboolean		isConfig;
	int				len;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !qpath || !qpath[0] ) {
		Com_Error( ERR_FATAL, "FS_ReadFile with empty name" );
	}

	buf = NULL;	// quiet compiler warning

	// if this is a .cfg file and we are playing back a journal, read
	// it from the journal file
	if ( strstr( qpath, ".cfg" ) ) {
		isConfig = qtrue;
		if ( com_journal && com_journal->integer == 2 ) {
			int		r;

			Com_DPrintf( "Loading %s from journal file.\n", qpath );
			r = FS_Read( &len, sizeof( len ), com_journalDataFile );
			if ( r != sizeof( len ) ) {
				if (buffer != NULL) *buffer = NULL;
				return -1;
			}
			// if the file didn't exist when the journal was created
			if (!len) {
				if (buffer == NULL) {
					return 1;			// hack for old journal files
				}
				*buffer = NULL;
				return -1;
			}
			if (buffer == NULL) {
				return len;
			}

			buf = (byte*)Z_Malloc( len+1, TAG_FILESYS, qfalse);
			*buffer = buf;

			r = FS_Read( buf, len, com_journalDataFile );
			if ( r != len ) {
				Com_Error( ERR_FATAL, "Read from journalDataFile failed" );
			}

			fs_loadCount++;
			//fs_loadStack++;

			// guarantee that it will have a trailing 0 for string operations
			buf[len] = 0;

			return len;
		}
	} else {
		isConfig = qfalse;
	}

	// look for it in the filesystem or pack files
	len = FS_FOpenFileRead( qpath, &h, qfalse );
	if ( h == 0 ) {
		if ( buffer ) {
			*buffer = NULL;
		}
		// if we are journalling and it is a config file, write a zero to the journal file
		if ( isConfig && com_journal && com_journal->integer == 1 ) {
			Com_DPrintf( "Writing zero for %s to journal file.\n", qpath );
			len = 0;
			FS_Write( &len, sizeof( len ), com_journalDataFile );
			FS_Flush( com_journalDataFile );
		}
		return -1;
	}

	if ( !buffer ) {
		if ( isConfig && com_journal && com_journal->integer == 1 ) {
			Com_DPrintf( "Writing len for %s to journal file.\n", qpath );
			FS_Write( &len, sizeof( len ), com_journalDataFile );
			FS_Flush( com_journalDataFile );
		}
		FS_FCloseFile( h );
		return len;
	}

	fs_loadCount++;
/*	fs_loadStack++;

	buf = (unsigned char *)Hunk_AllocateTempMemory(len+1);
	*buffer = buf;*/

	buf = (byte*)Z_Malloc( len+1, TAG_FILESYS, qfalse);
	buf[len]='\0';	// because we're not calling Z_Malloc with optional trailing 'bZeroIt' bool
	*buffer = buf;

//	Z_Label(buf, qpath);

	FS_Read (buf, len, h);

	// guarantee that it will have a trailing 0 for string operations
	buf[len] = 0;
	FS_FCloseFile( h );

	// if we are journalling and it is a config file, write it to the journal file
	if ( isConfig && com_journal && com_journal->integer == 1 ) {
		Com_DPrintf( "Writing %s to journal file.\n", qpath );
		FS_Write( &len, sizeof( len ), com_journalDataFile );
		FS_Write( buf, len, com_journalDataFile );
		FS_Flush( com_journalDataFile );
	}
	return len;
}

/*
=============
FS_FreeFile
=============
*/
void FS_FreeFile( void *buffer ) {
	/*
	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}
	if ( !buffer ) {
		Com_Error( ERR_FATAL, "FS_FreeFile( NULL )" );
	}
	fs_loadStack--;

	Hunk_FreeTempMemory( buffer );

	// if all of our temp files are free, clear all of our space
	if ( fs_loadStack == 0 ) {
		Hunk_ClearTempMemory();
	}
	*/

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}
	if ( !buffer ) {
		Com_Error( ERR_FATAL, "FS_FreeFile( NULL )" );
	}

	Z_Free( buffer );
}

/*
============
FS_WriteFile

Filename are reletive to the quake search path
============
*/
void FS_WriteFile( const char *qpath, const void *buffer, int size ) {
	fileHandle_t f;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !qpath || !buffer ) {
		Com_Error( ERR_FATAL, "FS_WriteFile: NULL parameter" );
	}

	f = FS_FOpenFileWrite( qpath );
	if ( !f ) {
		Com_Printf( "Failed to open %s\n", qpath );
		return;
	}

	FS_Write( buffer, size, f );

	FS_FCloseFile( f );
}



/*
==========================================================================

ZIP FILE LOADING

==========================================================================
*/

/*
=================
FS_LoadZipFile

Creates a new pak_t in the search chain for the contents
of a zip file.
=================
*/
static pack_t *FS_LoadZipFile( char *zipfile, const char *basename )
{
	fileInPack_t	*buildBuffer;
	pack_t			*pack;
	unzFile			uf;
	int				err;
	unz_global_info gi;
	char			filename_inzip[MAX_ZPATH];
	unz_file_info	file_info;
	ZPOS64_T		i;
	size_t			len;
	int			hash;
	int				fs_numHeaderLongs;
	int				*fs_headerLongs;
	char			*namePtr;

	fs_numHeaderLongs = 0;

	uf = unzOpen(zipfile);
	err = unzGetGlobalInfo (uf,&gi);

	if (err != UNZ_OK)
		return NULL;

	fs_packFiles += gi.number_entry;

	len = 0;
	unzGoToFirstFile(uf);
	for (i = 0; i < gi.number_entry; i++)
	{
		err = unzGetCurrentFileInfo(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0);
		if (err != UNZ_OK) {
			break;
		}
		len += strlen(filename_inzip) + 1;
		unzGoToNextFile(uf);
	}

	buildBuffer = (struct fileInPack_s *)Z_Malloc((int)((gi.number_entry * sizeof(fileInPack_t)) + len), TAG_FILESYS, qtrue);
	namePtr = ((char *) buildBuffer) + gi.number_entry * sizeof( fileInPack_t );
	fs_headerLongs = (int *)Z_Malloc( gi.number_entry * sizeof(int), TAG_FILESYS, qtrue );

	// get the hash table size from the number of files in the zip
	// because lots of custom pk3 files have less than 32 or 64 files
	for (i = 1; i <= MAX_FILEHASH_SIZE; i <<= 1) {
		if (i > gi.number_entry) {
			break;
		}
	}

	pack = (pack_t *)Z_Malloc( sizeof( pack_t ) + i * sizeof(fileInPack_t *), TAG_FILESYS, qtrue );
	pack->hashSize = i;
	pack->hashTable = (fileInPack_t **) (((char *) pack) + sizeof( pack_t ));
	for(int i = 0; i < pack->hashSize; i++) {
		pack->hashTable[i] = NULL;
	}

	Q_strncpyz( pack->pakFilename, zipfile, sizeof( pack->pakFilename ) );
	Q_strncpyz( pack->pakBasename, basename, sizeof( pack->pakBasename ) );

	// strip .pk3 if needed
	if ( strlen( pack->pakBasename ) > 4 && !Q_stricmp( pack->pakBasename + strlen( pack->pakBasename ) - 4, ".pk3" ) ) {
		pack->pakBasename[strlen( pack->pakBasename ) - 4] = 0;
	}

	pack->handle = uf;
	pack->numfiles = gi.number_entry;
	unzGoToFirstFile(uf);

	for (i = 0; i < gi.number_entry; i++)
	{
		err = unzGetCurrentFileInfo(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0);
		if (err != UNZ_OK) {
			break;
		}
		if (file_info.uncompressed_size > 0) {
			fs_headerLongs[fs_numHeaderLongs++] = LittleLong(file_info.crc);
		}
		Q_strlwr( filename_inzip );
		hash = FS_HashFileName(filename_inzip, pack->hashSize);
		buildBuffer[i].name = namePtr;
		strcpy( buildBuffer[i].name, filename_inzip );
		namePtr += strlen(filename_inzip) + 1;
		// store the file position in the zip
		buildBuffer[i].pos = unzGetOffset(uf);
		buildBuffer[i].len = file_info.uncompressed_size;
		buildBuffer[i].next = pack->hashTable[hash];
		pack->hashTable[hash] = &buildBuffer[i];
		unzGoToNextFile(uf);
	}

	pack->checksum = Com_BlockChecksum( fs_headerLongs, 4 * fs_numHeaderLongs );
	pack->pure_checksum = Com_BlockChecksumKey( fs_headerLongs, 4 * fs_numHeaderLongs, LittleLong(fs_checksumFeed) );
	pack->checksum = LittleLong( pack->checksum );
	pack->pure_checksum = LittleLong( pack->pure_checksum );

	Z_Free(fs_headerLongs);

	pack->buildBuffer = buildBuffer;

	// which versions does this pk3 support?

	// filename prefixes
	if (!Q_stricmpn(basename, "o102_", 5)) {
		pack->gvc = PACKGVC_1_02;
	} else if (!Q_stricmpn(basename, "o103_", 5)) {
		pack->gvc = PACKGVC_1_03;
	} else if (!Q_stricmpn(basename, "o104_", 5)) {
		pack->gvc = PACKGVC_1_04;
	}

	// mv.info file in root directory of pk3 file
	char cversion[128];
	int cversionlen = FS_PakReadFile(pack, "mv.info", cversion, sizeof(cversion) - 1);
	if (cversionlen) {
		cversion[cversionlen] = '\0';
		pack->gvc = PACKGVC_UNKNOWN; // mv.info file overwrites version prefixes

		if (Q_stristr(cversion, "compatible 1.02")) {
			pack->gvc |= PACKGVC_1_02;
		}

		if (Q_stristr(cversion, "compatible 1.03")) {
			pack->gvc |= PACKGVC_1_03;
		}

		if (Q_stristr(cversion, "compatible 1.04")) {
			pack->gvc |= PACKGVC_1_04;
		}

		if (Q_stristr(cversion, "compatible all")) {
			pack->gvc = PACKGVC_1_02 | PACKGVC_1_03 | PACKGVC_1_04;
		}
	}

	// assets are hardcoded
	if (!Q_stricmp(pack->pakBasename, "assets0")) {
		pack->gvc = PACKGVC_1_02 | PACKGVC_1_03 | PACKGVC_1_04;
	} else if (!Q_stricmp(pack->pakBasename, "assets1")) {
		pack->gvc = PACKGVC_1_02 | PACKGVC_1_03 | PACKGVC_1_04;
	} else if (!Q_stricmp(pack->pakBasename, "assets2")) {
		pack->gvc = PACKGVC_1_03 | PACKGVC_1_04;
	} else if (!Q_stricmp(pack->pakBasename, "assets5")) {
		pack->gvc = PACKGVC_1_04;
	}

	// never reference assetsmv files
	if (!Q_stricmpn(pack->pakBasename, "assetsmv", 8)) {
		pack->noref = qtrue;
	}

	return pack;
}

/*
=================================================================================

DIRECTORY SCANNING FUNCTIONS

=================================================================================
*/

#define	MAX_FOUND_FILES	0x1000

static int FS_ReturnPath( const char *zname, char *zpath, int *depth ) {
	int len, at, newdep;

	newdep = 0;
	zpath[0] = 0;
	len = 0;
	at = 0;

	while(zname[at] != 0)
	{
		if (zname[at]=='/' || zname[at]=='\\') {
			len = at;
			newdep++;
		}
		at++;
	}
	strcpy(zpath, zname);
	zpath[len] = 0;
	*depth = newdep;

	return len;
}

/*
==================
FS_AddFileToList
==================
*/
static int FS_AddFileToList( const char *name, const char *list[MAX_FOUND_FILES], int nfiles ) {
	int		i;

	if ( nfiles == MAX_FOUND_FILES - 1 ) {
		return nfiles;
	}
	for ( i = 0 ; i < nfiles ; i++ ) {
		if ( !Q_stricmp( name, list[i] ) ) {
			return nfiles;		// allready in list
		}
	}
	list[nfiles] = CopyString( name );
	nfiles++;

	return nfiles;
}

/*
===============
FS_ListFilteredFiles

Returns a uniqued list of files that match the given criteria
from all search paths

id Tech 3 FS_ListFilteredFiles used path argument as a file path
prefix, rather than directory (but only for files in paks!). When
compat is true, this behaviour is reproduced.
===============
*/
static const char **FS_ListFilteredFiles( const char *path, const char *extension, char *filter, int *numfiles, qboolean compat ) {
	int				nfiles;
	const char		**listCopy;
	const char		*list[MAX_FOUND_FILES];
	searchpath_t	*search;
	int				i;
	int				pathLength;
	int				extensionLength;
	int				length, pathDepth, temp;
	pack_t			*pak;
	fileInPack_t	*buildBuffer;
	char			zpath[MAX_ZPATH];

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !path ) {
		*numfiles = 0;
		return NULL;
	}
	if ( !extension ) {
		extension = "";
	}

	pathLength = (int)strlen( path );
	if ( path[pathLength-1] == '\\' || path[pathLength-1] == '/' ) {
		pathLength--;
	}
	extensionLength = (int)strlen( extension );
	nfiles = 0;
	FS_ReturnPath(path, zpath, &pathDepth);

	//
	// search through the path, one element at a time, adding to list
	//
	for (search = fs_searchpaths ; search ; search = search->next) {
		// is the element a pak file?
		if (search->pack) {

			//ZOID:  If we are pure, don't search for files on paks that
			// aren't on the pure list
			if ( !FS_PakIsPure(search->pack) ) {
				continue;
			}

			// version specific pk3's
			// downloaded files are always okey because they are only loaded on servers currently using them
			if (Q_stricmpn(search->pack->pakBasename, "dl_", 3) &&
				!((search->pack->gvc & PACKGVC_1_02 && MV_GetCurrentGameversion() == VERSION_1_02) ||
				 (search->pack->gvc & PACKGVC_1_03 && MV_GetCurrentGameversion() == VERSION_1_03) ||
				 (search->pack->gvc & PACKGVC_1_04 && MV_GetCurrentGameversion() == VERSION_1_04) ||
				 (Q_stricmp(search->pack->pakGamename, BASEGAME) && search->pack->gvc == PACKGVC_UNKNOWN) ||
				 (MV_GetCurrentGameversion() == VERSION_UNDEF))) {

				// incompatible pk3
				if (search->pack->gvc != PACKGVC_UNKNOWN && !FS_idPak(search->pack))
					continue;
			}

			// look through all the pak file elements
			pak = search->pack;
			buildBuffer = pak->buildBuffer;
			for (i = 0; i < pak->numfiles; i++) {
				char	*name;
				int		zpathLen, depth;

				// check for directory match
				name = buildBuffer[i].name;
				//
				if (filter) {
					// case insensitive
					if (!Com_FilterPath( filter, name, qfalse ))
						continue;
					// unique the match
					nfiles = FS_AddFileToList( name, list, nfiles );
				}
				else {
					zpathLen = FS_ReturnPath(name, zpath, &depth);

					if ( (depth-pathDepth)>2 || pathLength > zpathLen || Q_stricmpn( name, path, pathLength ) ) {
						continue;
					}

					// there can be either \ or / path separators in a pk3 file
					if ( !compat && name[pathLength] != '/' && name[pathLength] != '\\' ) {
						continue;
					}

					// check for extension match
					length = (int)strlen( name );
					if ( length < extensionLength ) {
						continue;
					}

					if ( Q_stricmp( name + length - extensionLength, extension ) ) {
						continue;
					}
					// unique the match

					temp = pathLength;
					if (pathLength) {
						temp++;		// include the '/'
					}
					nfiles = FS_AddFileToList( name + temp, list, nfiles );
				}
			}
		} else if (search->dir) { // scan for files in the filesystem
			const char	*netpath;
			int			numSysFiles;
			const char	**sysFiles;
			const char	*name;

			// don't scan directories for files if we are pure or restricted
			if ( (fs_numServerPaks) &&
				 (!extension || Q_stricmp(extension, "fcf")) )
			{
				//rww - allow scanning for fcf files outside of pak even if pure
			    continue;
		    }
			else
			{
				netpath = FS_BuildOSPath( search->dir->path, search->dir->gamedir, path );
				sysFiles = Sys_ListFiles( netpath, extension, filter, &numSysFiles, qfalse );
				for ( i = 0 ; i < numSysFiles ; i++ ) {
					// unique the match
					name = sysFiles[i];
					nfiles = FS_AddFileToList( name, list, nfiles );
				}
				Sys_FreeFileList( sysFiles );
			}
		}
	}

	// return a copy of the list
	*numfiles = nfiles;

	if ( !nfiles ) {
		return NULL;
	}

	listCopy = (const char **)Z_Malloc( ( nfiles + 1 ) * sizeof( *listCopy ), TAG_FILESYS );
	for ( i = 0 ; i < nfiles ; i++ ) {
		listCopy[i] = list[i];
	}
	listCopy[i] = NULL;

	return listCopy;
}

/*
=================
FS_ListFiles
=================
*/
const char **FS_ListFiles( const char *path, const char *extension, int *numfiles ) {
	return FS_ListFilteredFiles( path, extension, NULL, numfiles, qtrue );
}

/*
=================
FS_ListFiles2
=================
*/
const char **FS_ListFiles2( const char *path, const char *extension, int *numfiles ) {
	return FS_ListFilteredFiles( path, extension, NULL, numfiles, qfalse );
}

/*
=================
FS_FreeFileList
=================
*/
void FS_FreeFileList( const char **list ) {
	int		i;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !list ) {
		return;
	}

	for ( i = 0 ; list[i] ; i++ ) {
		Z_Free( (void *)list[i] );
	}

	Z_Free( list );
}


/*
================
FS_GetFileList
================
*/
int	FS_GetFileList(  const char *path, const char *extension, char *listbuf, int bufsize ) {
	int		nFiles, i, nTotal, nLen;
	const char **pFiles = NULL;

	*listbuf = 0;
	nFiles = 0;
	nTotal = 0;

	if (Q_stricmp(path, "$modlist") == 0) {
		return FS_GetModList(listbuf, bufsize);
	}

	pFiles = FS_ListFiles(path, extension, &nFiles);

	for (i =0; i < nFiles; i++) {
		nLen = (int)strlen(pFiles[i]) + 1;
		if (nTotal + nLen + 1 < bufsize) {
			strcpy(listbuf, pFiles[i]);
			listbuf += nLen;
			nTotal += nLen;
		}
		else {
			nFiles = i;
			break;
		}
	}

	FS_FreeFileList(pFiles);

	return nFiles;
}

// NOTE: could prolly turn out useful for the win32 version too, but it's used only by linux and Mac OS X
//#if defined(__linux__) || defined(MACOS_X)
/*
=======================
Sys_ConcatenateFileLists

mkv: Naive implementation. Concatenates three lists into a
     new list, and frees the old lists from the heap.
bk001129 - from cvs1.17 (mkv)

FIXME TTimo those two should move to common.c next to Sys_ListFiles
=======================
 */
static unsigned int Sys_CountFileList(const char **list)
{
  int i = 0;

  if (list)
  {
    while (*list)
    {
      list++;
      i++;
    }
  }
  return i;
}

static const char** Sys_ConcatenateFileLists( const char **list0, const char **list1, const char **list2 )
{
	int totalLength = 0;
	const char** cat = NULL, **dst, **src;

	totalLength += Sys_CountFileList(list0);
	totalLength += Sys_CountFileList(list1);
	totalLength += Sys_CountFileList(list2);

	/* Create new list. */
	dst = cat = (const char **)Z_Malloc( ( totalLength + 1 ) * sizeof( char* ), TAG_FILESYS, qtrue );

	/* Copy over lists. */
        if (list0) {
            for (src = list0; *src; src++, dst++)
                *dst = *src;
        }
        if (list1) {
            for (src = list1; *src; src++, dst++)
                *dst = *src;
        }
        if (list2) {
            for (src = list2; *src; src++, dst++)
                *dst = *src;
        }

        // Terminate the list
	*dst = NULL;

  // Free our old lists.
  // NOTE: not freeing their content, it's been merged in dst and still being used
  if (list0) Z_Free( list0 );
  if (list1) Z_Free( list1 );
  if (list2) Z_Free( list2 );

	return cat;
}
//#endif

/*
================
FS_GetModList

Returns a list of mod directory names
A mod directory is a peer to base with a pk3 in it
The directories are searched in base path, cd path and home path
================
*/
int	FS_GetModList( char *listbuf, int bufsize ) {
  int		nMods, i, j, nTotal, nLen, nPaks, nPotential, nDescLen;
  const char **pFiles = NULL;
  const char **pPaks = NULL;
  const char *name, *path;
  char descPath[MAX_OSPATH];
  fileHandle_t descHandle;

  int dummy;
  const char **pFiles0 = NULL;
  const char **pFiles1 = NULL;
  const char **pFiles2 = NULL;
  qboolean bDrop = qfalse;

  *listbuf = 0;
  nMods = nPotential = nTotal = 0;

  pFiles0 = Sys_ListFiles( fs_homepath->string, NULL, NULL, &dummy, qtrue );
  pFiles1 = Sys_ListFiles( fs_basepath->string, NULL, NULL, &dummy, qtrue );
  // we searched for mods in the three paths
  // it is likely that we have duplicate names now, which we will cleanup below
  pFiles = Sys_ConcatenateFileLists( pFiles0, pFiles1, pFiles2 );
  nPotential = Sys_CountFileList(pFiles);

  for ( i = 0 ; i < nPotential ; i++ ) {
    name = pFiles[i];
    // NOTE: cleaner would involve more changes
    // ignore duplicate mod directories
    if (i!=0) {
      bDrop = qfalse;
      for(j=0; j<i; j++)
      {
        if (Q_stricmp(pFiles[j],name)==0) {
          // this one can be dropped
          bDrop = qtrue;
          break;
        }
      }
    }
    if (bDrop) {
      continue;
    }
    // we drop "base" "." and ".."
    if (Q_stricmp(name, "base") && Q_stricmpn(name, ".", 1)) {
      // now we need to find some .pk3 files to validate the mod
      // NOTE TTimo: (actually I'm not sure why .. what if it's a mod under developement with no .pk3?)
      // we didn't keep the information when we merged the directory names, as to what OS Path it was found under
      //   so it could be in base path, cd path or home path
      //   we will try each three of them here (yes, it's a bit messy)
      path = FS_BuildOSPath( fs_basepath->string, name, "" );
      nPaks = 0;
      pPaks = Sys_ListFiles(path, ".pk3", NULL, &nPaks, qfalse);
      Sys_FreeFileList( pPaks ); // we only use Sys_ListFiles to check wether .pk3 files are present

	  /* try on home path */
      if ( nPaks <= 0 )
      {
        path = FS_BuildOSPath( fs_homepath->string, name, "" );
        nPaks = 0;
        pPaks = Sys_ListFiles( path, ".pk3", NULL, &nPaks, qfalse );
        Sys_FreeFileList( pPaks );
      }

      if (nPaks > 0) {
        nLen = (int)strlen(name) + 1;
        // nLen is the length of the mod path
        // we need to see if there is a description available
        descPath[0] = '\0';
        strcpy(descPath, name);
        strcat(descPath, "/description.txt");
        nDescLen = FS_SV_FOpenFileRead( descPath, &descHandle );
        if ( nDescLen > 0 && descHandle) {
          FILE *file;
          file = FS_FileForHandle(descHandle);
          Com_Memset( descPath, 0, sizeof( descPath ) );
          nDescLen = (int)fread(descPath, 1, 48, file);
          if (nDescLen >= 0) {
            descPath[nDescLen] = '\0';
          }
          FS_FCloseFile(descHandle);
        } else {
          strcpy(descPath, name);
        }
        nDescLen = (int)strlen(descPath) + 1;

        if (nTotal + nLen + 1 + nDescLen + 1 < bufsize) {
          strcpy(listbuf, name);
          listbuf += nLen;
          strcpy(listbuf, descPath);
          listbuf += nDescLen;
          nTotal += nLen + nDescLen;
          nMods++;
        }
        else {
          break;
        }
      }
    }
  }
  Sys_FreeFileList( pFiles );

  return nMods;
}




//============================================================================

/*
================
FS_Dir_f
================
*/
void FS_Dir_f( void ) {
	char	*path;
	char	*extension;
	const char	**dirnames;
	int		ndirs;
	int		i;

	if ( Cmd_Argc() < 2 || Cmd_Argc() > 3 ) {
		Com_Printf( "usage: dir <directory> [extension]\n" );
		return;
	}

	if ( Cmd_Argc() == 2 ) {
		path = Cmd_Argv( 1 );
		extension = "";
	} else {
		path = Cmd_Argv( 1 );
		extension = Cmd_Argv( 2 );
	}

	Com_Printf( "Directory of %s %s\n", path, extension );
	Com_Printf( "---------------\n" );

	dirnames = FS_ListFiles2( path, extension, &ndirs );

	for ( i = 0; i < ndirs; i++ ) {
		Com_Printf( "%s\n", dirnames[i] );
	}
	FS_FreeFileList( dirnames );
}

/*
===========
FS_ConvertPath
===========
*/
static char *FS_ConvertPath( const char *s ) {
	static char path[MAX_QPATH];
	char *o = path;

	while (*s && o < &path[MAX_QPATH - 1]) {
		if ( *s == '\\' || *s == ':' ) {
			*o = '/';
		} else {
			*o = *s;
		}
		s++;
		o++;
	}

	*o = '\0';
	return path;
}

/*
===========
FS_PathCmp

Ignore case and seprator char distinctions
===========
*/
int FS_PathCmp( const char *s1, const char *s2 ) {
	int		c1, c2;

	do {
		c1 = *s1++;
		c2 = *s2++;

		if (c1 >= 'a' && c1 <= 'z') {
			c1 -= ('a' - 'A');
		}
		if (c2 >= 'a' && c2 <= 'z') {
			c2 -= ('a' - 'A');
		}

		if ( c1 == '\\' || c1 == ':' ) {
			c1 = '/';
		}
		if ( c2 == '\\' || c2 == ':' ) {
			c2 = '/';
		}

		if (c1 < c2) {
			return -1;		// strings not equal
		}
		if (c1 > c2) {
			return 1;
		}
	} while (c1);

	return 0;		// strings are equal
}

/*
================
FS_SortFileList
================
*/
void FS_SortFileList(const char **filelist, int numfiles) {
	int i, j, k, numsortedfiles;
	const char **sortedlist;

	if (numfiles <= 0)
		return;

	sortedlist = (const char **)Z_Malloc( ( numfiles + 1 ) * sizeof( *sortedlist ), TAG_FILESYS, qtrue );
	sortedlist[0] = NULL;
	numsortedfiles = 0;
	for (i = 0; i < numfiles; i++) {
		for (j = 0; j < numsortedfiles; j++) {
			if (FS_PathCmp(filelist[i], sortedlist[j]) < 0) {
				break;
			}
		}
		for (k = numsortedfiles; k > j; k--) {
			sortedlist[k] = sortedlist[k-1];
		}
		sortedlist[j] = filelist[i];
		numsortedfiles++;
	}
	Com_Memcpy(filelist, sortedlist, numfiles * sizeof( *filelist ) );
	Z_Free(sortedlist);
}

/*
================
FS_NewDir_f
================
*/
void FS_NewDir_f( void ) {
	char	*filter;
	const char	**dirnames;
	int		ndirs;
	int		i;

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "usage: fdir <filter>\n" );
		Com_Printf( "example: fdir *q3dm*.bsp\n");
		return;
	}

	filter = Cmd_Argv( 1 );

	Com_Printf( "---------------\n" );

	dirnames = FS_ListFilteredFiles( "", "", filter, &ndirs, qtrue );

	FS_SortFileList(dirnames, ndirs);

	for ( i = 0; i < ndirs; i++ ) {
		Com_Printf( "%s\n", FS_ConvertPath(dirnames[i]) );
	}
	Com_Printf( "%d files listed\n", ndirs );
	FS_FreeFileList( dirnames );
}

/*
============
FS_Path_f

============
*/
void FS_Path_f( void ) {
	searchpath_t	*s;
	int				i;

	Com_Printf ("Current search path:\n");
	for (s = fs_searchpaths; s; s = s->next) {
		if (s->pack) {
			Com_Printf ("%s (%i files) [ %s%s%s%s]\n", s->pack->pakFilename, s->pack->numfiles,
				s->pack->gvc == PACKGVC_UNKNOWN ? "unknown " : "",
				s->pack->gvc & PACKGVC_1_02 ? "1.02 " : "",
				s->pack->gvc & PACKGVC_1_03 ? "1.03 " : "",
				s->pack->gvc & PACKGVC_1_04 ? "1.04 " : "");

			if ( fs_numServerPaks ) {
				if ( !FS_PakIsPure(s->pack) ) {
					Com_Printf( "	not on the pure list\n" );
				} else {
					Com_Printf( "	on the pure list\n" );
				}
			}
		} else {
			Com_Printf ("%s/%s\n", s->dir->path, s->dir->gamedir );
		}
	}


	Com_Printf( "\n" );
	for ( i = 1 ; i < MAX_FILE_HANDLES ; i++ ) {
		if ( fsh[i].handleFiles.file.o ) {
			Com_Printf( "handle %i: %s\n", i, fsh[i].name );
		}
	}
}

/*
============
FS_TouchFile_f

The only purpose of this function is to allow game script files to copy
arbitrary files furing an "fs_copyfiles 1" run.
============
*/
void FS_TouchFile_f( void ) {
	fileHandle_t	f;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: touchFile <file>\n" );
		return;
	}

	FS_FOpenFileRead( Cmd_Argv( 1 ), &f, qfalse );
	if ( f ) {
		FS_FCloseFile( f );
	}
}

/*
============
FS_Which_f
============
*/
static void FS_Which_f( void ) {
	fileHandle_t	f;
	searchpath_t	*search;
	char			*filename;

	filename = Cmd_Argv(1);

	if ( !filename[0] ) {
		Com_Printf( "Usage: which <file>\n" );
		return;
	}

	// qpaths are not supposed to have a leading slash
	if ( filename[0] == '/' || filename[0] == '\\' ) {
		filename++;
	}

	// make absolutely sure that it can't back up the path.
	// The searchpaths do guarantee that something will always
	// be prepended, so we don't need to worry about "c:" or "//limbo"
	if ( strstr( filename, ".." ) || strstr( filename, "::" ) ) {
		return;
	}

	FS_FOpenFileRead( filename, &f, qfalse );

	if ( !f ) {
		Com_Printf( "File not found: \"%s\"\n", filename );
		return;
	}

	// find file that would be opened by FS_FOpenFileRead taking all
	// its quirks and special cases into account
	if ( fsh[f].zipFile ) {
		for ( search=fs_searchpaths; search; search=search->next ) {
			if ( search->pack ) {
				pack_t* pak = search->pack;

				if ( fsh[f].handleFiles.file.z == pak->handle ) {
					// found it!
					Com_Printf( "File \"%s\" found in \"%s\"\n", filename, pak->pakFilename );
					FS_FCloseFile( f );
					return;
				}
			}
		}
		assert( 0 );
	}

	FS_FCloseFile( f );

	// if it's not in pack, find any match outside of it
	for ( search=fs_searchpaths; search; search=search->next ) {
		if (search->dir) {
			directory_t* dir = search->dir;

			char* netpath = FS_BuildOSPath( dir->path, dir->gamedir, filename );
			FILE* filep = fopen(netpath, "rb");

			if ( filep ) {
				fclose( filep );

				char buf[MAX_OSPATH];
				Com_sprintf( buf, sizeof( buf ), "%s%c%s", dir->path, PATH_SEP, dir->gamedir );
				FS_ReplaceSeparators( buf );
				Com_Printf( "File \"%s\" found at \"%s\"\n", filename, buf );
				return;
			}
		}
	}

	assert( 0 );
	Com_Printf( "File not found: \"%s\"\n", filename );
}

//===========================================================================


static int QDECL paksort( const void *a, const void *b ) {
	const char	*aa, *bb;

	aa = *(const char * const *)a;
	bb = *(const char * const *)b;

	// downloaded files have priority
	// this is needed because otherwise even if a clientside was downloaded, there is no gurantee it is actually used.
	if (!Q_stricmpn(aa, "dl_", 3) && Q_stricmpn(bb, "dl_", 3)) {
		return 1;
	} else if (Q_stricmpn(aa, "dl_", 3) && !Q_stricmpn(bb, "dl_", 3)) {
		return -1;
	} else {
		return FS_PathCmp(aa, bb);
	}
}

/*
================
FS_AddGameDirectory

Sets fs_gamedir, adds the directory to the head of the path,
then loads the zip headers
================
*/
const char *get_filename(const char *path) {
	const char *slash = strrchr(path, PATH_SEP);
	if (!slash || slash == path) return "";
	return slash + 1;
}

#define	MAX_PAKFILES	1024
static void FS_AddGameDirectory( const char *path, const char *dir, qboolean assetsOnly ) {
	searchpath_t	*sp;
	int				i;
	searchpath_t	*search;
	pack_t			*pak;
	char			*pakfile;
	int				numfiles;
	const char		**pakfiles;
	const char		*sorted[MAX_PAKFILES];
	const char		*filename;

	// this fixes the case where fs_basepath is the same as fs_cdpath
	// which happens on full installs
	for ( sp = fs_searchpaths ; sp ; sp = sp->next ) {
		if ( sp->dir && !Q_stricmp(sp->dir->path, path) && !Q_stricmp(sp->dir->gamedir, dir)) {
			return;			// we've already got this one
		}
	}

	if ( FS_CheckDirTraversal(dir) )
	{
		Com_Printf("FS_AddGameDirectory: dir %s contains invalid patterns\n", dir);
		return;
	}

	Q_strncpyz( fs_gamedir, dir, sizeof( fs_gamedir ) );

	//
	// add the directory to the search path
	//
	if (!assetsOnly) {
		search = (struct searchpath_s *)Z_Malloc(sizeof(searchpath_t), TAG_FILESYS, qtrue);
		search->dir = (directory_t *)Z_Malloc(sizeof(*search->dir), TAG_FILESYS, qtrue);

		Q_strncpyz(search->dir->path, path, sizeof(search->dir->path));
		Q_strncpyz(search->dir->gamedir, dir, sizeof(search->dir->gamedir));
		search->next = fs_searchpaths;
		fs_searchpaths = search;
	}

	// find all pak files in this directory
	pakfile = FS_BuildOSPath( path, dir, "" );
	pakfile[ strlen(pakfile) - 1 ] = 0;	// strip the trailing slash

	pakfiles = Sys_ListFiles( pakfile, ".pk3", NULL, &numfiles, qfalse );

	// sort them so that later alphabetic matches override
	// earlier ones.  This makes pak1.pk3 override pak0.pk3
	if ( numfiles > MAX_PAKFILES ) {
		numfiles = MAX_PAKFILES;
	}
	for ( i = 0 ; i < numfiles ; i++ ) {
		sorted[i] = pakfiles[i];
	}

	qsort( sorted, numfiles, sizeof(size_t), paksort );

	for ( i = 0 ; i < numfiles ; i++ ) {
		pakfile = FS_BuildOSPath( path, dir, sorted[i] );
		filename = get_filename(pakfile);

		if (assetsOnly) {
			if (strcmp(filename, "assets0.pk3") && strcmp(filename, "assets1.pk3") &&
				strcmp(filename, "assets2.pk3") && strcmp(filename, "assets5.pk3")) {
				continue;
			}
		}

		if ( ( pak = FS_LoadZipFile( pakfile, sorted[i] ) ) == 0 )
			continue;

#ifndef DEDICATED
		// files beginning with "dl_" are only loaded when referenced by the server
		if (!Q_stricmpn(filename, "dl_", 3)) {
			int j;
			qboolean found = qfalse;

			for (j = 0; j < fs_numServerReferencedPaks; j++) {
				if (pak->checksum == fs_serverReferencedPaks[j]) {
					// server wants it!
					found = qtrue;
					break;
				}
			}

			if (!found) {
				// server has no interest in the file
				unzClose(pak->handle);
				Z_Free(pak->buildBuffer);
				Z_Free(pak);
				continue;
			}
		}
#endif

		// store the game name for downloading
		Q_strncpyz(pak->pakGamename, dir, sizeof(pak->pakGamename));

		// if the pk3 is not in base, always reference it (standard jk2 behaviour)
		if (Q_stricmpn(pak->pakGamename, BASEGAME, (int)strlen(BASEGAME))) {
			pak->referenced |= FS_GENERAL_REF;
		}

		search = (searchpath_s *)Z_Malloc (sizeof(searchpath_t), TAG_FILESYS, qtrue);
		search->pack = pak;
		search->next = fs_searchpaths;
		fs_searchpaths = search;
	}

	// done
	Sys_FreeFileList( pakfiles );
}

/*
================
FS_idPak
================
*/
qboolean FS_idPakPath(const char *pak, const char *base) {
	int i;
	char path[MAX_OSPATH];

	for (i = 0; i < NUM_ID_PAKS; i++) {
		Com_sprintf(path, sizeof(path), "%s/assets%d", base, i);

		if (!FS_FilenameCompare(pak, path)) {
			break;
		}
	}
	if (i < NUM_ID_PAKS) {
		return qtrue;
	}

	Com_sprintf(path, sizeof(path), "%s/assetsmv", base);
	if (!FS_FilenameCompare(pak, path)) {
		return qtrue;
	}

	Com_sprintf(path, sizeof(path), "%s/assetsmv2", base);
	if (!FS_FilenameCompare(pak, path)) {
		return qtrue;
	}

	return qfalse;
}

qboolean FS_idPak(pack_t *pak) {
	char path[MAX_OSPATH];
	Com_sprintf(path, sizeof(path), "%s/%s", pak->pakGamename, pak->pakBasename);
	
	return FS_idPakPath(path, BASEGAME);
}

/*
================
FS_CheckDirTraversal

Check whether the string contains stuff like "../" to prevent directory traversal bugs
and return qtrue if it does.
================
*/

qboolean FS_CheckDirTraversal(const char *checkdir) {
	if (strstr(checkdir, "..")) return qtrue;
	return qfalse;
}

/*
================
FS_ComparePaks

if dlstring == qtrue

Returns a list of pak files that we should download from the server. They all get stored
in the current gamedir and an FS_Restart will be fired up after we download them all.

The string is the format:

@remotename@localname [repeat]

static int		fs_numServerReferencedPaks;
static int		fs_serverReferencedPaks[MAX_SEARCH_PATHS];
static char		*fs_serverReferencedPakNames[MAX_SEARCH_PATHS];

----------------
dlstring == qfalse

we are not interested in a download string format, we want something human-readable
(this is used for diagnostics while connecting to a pure server)
================
*/
qboolean FS_ComparePaks( char *neededpaks, int len, int *chksums, size_t maxchksums, qboolean dlstring ) {
	searchpath_t	*sp;
	qboolean havepak, badchecksum, badname;
	int i;

	if ( !fs_numServerReferencedPaks ) {
		return qfalse; // Server didn't send any pack information along
	}

	*neededpaks = 0;
	badname = qfalse;

	for ( i = 0 ; i < fs_numServerReferencedPaks ; i++ ) {
		// Ok, see if we have this pak file
		badchecksum = qfalse;
		havepak = qfalse;

		// never autodownload any of the id paks
		if ( FS_idPakPath(fs_serverReferencedPakNames[i], BASEGAME) ) {
			continue;
		}

		// Make sure the server cannot make us write to non-quake3 directories.
		if (FS_CheckDirTraversal(fs_serverReferencedPakNames[i])) {
			Com_Printf("WARNING: Invalid download name %s\n", fs_serverReferencedPakNames[i]);
			continue;
		}

		for ( sp = fs_searchpaths ; sp ; sp = sp->next ) {
			if ( sp->pack && sp->pack->checksum == fs_serverReferencedPaks[i] ) {
				havepak = qtrue; // This is it!
				break;
			}
		}

		if ( !havepak && fs_serverReferencedPakNames[i] && *fs_serverReferencedPakNames[i] ) {
			// Don't got it
			char moddir[MAX_QPATH], filename[MAX_QPATH];
			int read = sscanf(fs_serverReferencedPakNames[i], "%63[^'/']/%63s", moddir, filename);

			if ( read != 2 ) {
				Com_Printf( "WARNING: Unable to parse pak name: %s\n", fs_serverReferencedPakNames[i] );
				badname = qtrue;
				continue;
			}

			if (dlstring) {
				// Remote name
				Q_strcat( neededpaks, len, "@");
				Q_strcat( neededpaks, len, fs_serverReferencedPakNames[i] );
				Q_strcat( neededpaks, len, ".pk3" );

				// Local name
				Q_strcat( neededpaks, len, "@");
				// Do we have one with the same name?
				if (FS_SV_FileExists(va("%s/dl_%s.pk3", moddir, filename))) {
					char st[MAX_ZPATH];
					// We already have one called this, we need to download it to another name
					// Make something up with the checksum in it
					Com_sprintf(st, sizeof(st), "%s/dl_%s.%08x.pk3", moddir, filename, fs_serverReferencedPaks[i]);
					Q_strcat( neededpaks, len, st );
				} else {
					char st[MAX_ZPATH];

					Com_sprintf(st, sizeof(st), "%s/dl_%s.pk3", moddir, filename);
					Q_strcat(neededpaks, len, st);
				}

				if (chksums && i < (int)maxchksums) {
					chksums[i] = fs_serverReferencedPaks[i];
				}
			} else {
				char st[MAX_ZPATH];

				Com_sprintf(st, sizeof(st), "%s/dl_%s.pk3", moddir, filename);
				Q_strcat(neededpaks, len, st);

				// Do we have one with the same name?
				if ( FS_SV_FileExists(va("%s/dl_%s.pk3", moddir, filename)) ) {
					Q_strcat( neededpaks, len, " (local file exists with wrong checksum)");
				}
				Q_strcat( neededpaks, len, "\n");
			}
		}
	}
	if ( *neededpaks || badname ) {
		return qtrue;
	}

	return qfalse; // We have them all
}

/*
================
FS_Shutdown

Frees all resources and closes all files
================
*/
void FS_Shutdown( qboolean closemfp ) {
	searchpath_t	*p, *next;
	int	i;

	for(i = 1; i < MAX_FILE_HANDLES; i++) {
		switch (fsh[i].module) {
		case MODULE_GAME:
		case MODULE_CGAME:
		case MODULE_UI:
			FS_FCloseFile(i, fsh[i].module);
			break;
		default:
			break;
		}
	}

	// free everything
	for ( p = fs_searchpaths ; p ; p = next ) {
		next = p->next;

		if ( p->pack ) {
			unzClose(p->pack->handle);
			Z_Free( p->pack->buildBuffer );
			Z_Free( p->pack );
		}
		if ( p->dir ) {
			Z_Free( p->dir );
		}
		Z_Free( p );
	}

	// any FS_ calls will now be an error until reinitialized
	fs_searchpaths = NULL;

	Cmd_RemoveCommand( "path" );
	Cmd_RemoveCommand( "dir" );
	Cmd_RemoveCommand( "fdir" );
	Cmd_RemoveCommand( "touchFile" );
	Cmd_RemoveCommand( "which" );
}

/*
================
FS_Startup
================
*/
static void FS_Startup( const char *gameName ) {
	const char *assetsPath;
	// Silence clang, they do get initialized
	char *mv_whitelist = NULL, *mv_blacklist = NULL, *mv_forcelist = NULL;
	fileHandle_t f_w, f_b, f_f;
	int f_wl, f_bl, f_fl;
	int s;
	searchpath_t *search;

	Com_Printf( "----- FS_Startup -----\n" );

	fs_debug = Cvar_Get( "fs_debug", "0", 0 );
	fs_copyfiles = Cvar_Get( "fs_copyfiles", "0", CVAR_INIT );
	fs_basepath = Cvar_Get ("fs_basepath", Sys_DefaultInstallPath(), CVAR_INIT | CVAR_VM_NOWRITE );
	fs_basegame = Cvar_Get ("fs_basegame", "", CVAR_INIT );
	fs_homepath = Cvar_Get ("fs_homepath", Sys_DefaultHomePath(), CVAR_INIT | CVAR_VM_NOWRITE );
	fs_gamedirvar = Cvar_Get ("fs_game", "", CVAR_INIT|CVAR_SYSTEMINFO );

	assetsPath = Sys_DefaultAssetsPath();
	fs_assetspath = Cvar_Get("fs_assetspath", assetsPath ? assetsPath : "", CVAR_INIT | CVAR_VM_NOWRITE);

	if (!FS_AllPath_Base_FileExists("assets5.pk3")) {
		// assets files found in none of the paths
#if defined(INSTALLED)
#ifdef WIN32
		Com_Error(ERR_FATAL, "could not find JK2 installation... make sure you have JK2 installed (CD or Steam).");
#elif MACOS_X
		Com_Error(ERR_FATAL, "could not find JK2 App... put it either in /Applications or side by side with the JK2MV app.");
#else
		Com_Error(ERR_FATAL, "could not find assets(0,1,2,5).pk3 files... copy them into the base directory (%s/base or %s/base).", fs_basepath->string, fs_homepath->string);
#endif
#else
		Com_Error(ERR_FATAL, "could not find assets(0,1,2,5).pk3 files... copy them into the base directory.");
#endif
		return;
	}

	// don't use the assetspath if assets files already found in fs_basepath or fs_homepath
	if (assetsPath && !FS_BaseHome_Base_FileExists("assets5.pk3")) {
		FS_AddGameDirectory(assetsPath, BASEGAME, qtrue);
	}

	// add search path elements in reverse priority order
	if (fs_basepath->string[0]) {
		FS_AddGameDirectory(fs_basepath->string, gameName, qfalse);
	}
  // fs_homepath is somewhat particular to *nix systems, only add if relevant
  // NOTE: same filtering below for mods and basegame
	if (fs_basepath->string[0] && Q_stricmp(fs_homepath->string,fs_basepath->string)) {
		FS_AddGameDirectory(fs_homepath->string, gameName, qfalse);
	}

	// check for additional base game so mods can be based upon other mods
	if ( fs_basegame->string[0] && !Q_stricmp( gameName, BASEGAME ) && Q_stricmp( fs_basegame->string, gameName ) ) {
		if (fs_basepath->string[0]) {
			FS_AddGameDirectory(fs_basepath->string, fs_basegame->string, qfalse);
		}
		if (fs_homepath->string[0] && Q_stricmp(fs_homepath->string,fs_basepath->string)) {
			FS_AddGameDirectory(fs_homepath->string, fs_basegame->string, qfalse);
		}
	}

	// check for additional game folder for mods
	if ( fs_gamedirvar->string[0] && !Q_stricmp( gameName, BASEGAME ) && Q_stricmp( fs_gamedirvar->string, gameName ) ) {
		if (fs_basepath->string[0]) {
			FS_AddGameDirectory(fs_basepath->string, fs_gamedirvar->string, qfalse);
		}
		if (fs_homepath->string[0] && Q_stricmp(fs_homepath->string,fs_basepath->string)) {
			FS_AddGameDirectory(fs_homepath->string, fs_gamedirvar->string, qfalse);
		}
	}

	// add our commands
	Cmd_AddCommand ("path", FS_Path_f);
	Cmd_AddCommand ("dir", FS_Dir_f );
	Cmd_AddCommand ("fdir", FS_NewDir_f );
	Cmd_AddCommand ("touchFile", FS_TouchFile_f );
	Cmd_AddCommand ("which", FS_Which_f );

	// print the current search paths
	FS_Path_f();

	fs_gamedirvar->modified = qfalse; // We just loaded, it's not modified

	// reference lists
	f_wl = FS_FOpenFileRead("ref_whitelist.txt", &f_w, qfalse);
	f_bl = FS_FOpenFileRead("ref_blacklist.txt", &f_b, qfalse);
	f_fl = FS_FOpenFileRead("ref_forcelist.txt", &f_f, qfalse);

	if (f_w) {
		Com_Printf("using whitelist for referenced files...\n");

		mv_whitelist = (char *)Hunk_AllocateTempMemory(1 + f_wl + 1);
		mv_whitelist[0] = '\n';
		s = FS_Read(mv_whitelist + 1, f_wl, f_w);
		mv_whitelist[s + 1] = 0;
	}

	if (f_b) {
		Com_Printf("using blacklist for referenced files...\n");

		mv_blacklist = (char *)Hunk_AllocateTempMemory(1 + f_bl + 1);
		mv_blacklist[0] = '\n';
		s = FS_Read(mv_blacklist + 1, f_bl, f_b);
		mv_blacklist[s + 1] = 0;
	}

	if (f_f) {
		Com_Printf("using forcelist for referenced files...\n");

		mv_forcelist = (char *)Hunk_AllocateTempMemory(1 + f_fl + 1);
		mv_forcelist[0] = '\n';
		s = FS_Read(mv_forcelist + 1, f_fl, f_f);
		mv_forcelist[s + 1] = 0;
	}

	for (search = fs_searchpaths; search; search = search->next) {
		if (search->pack) {
			char packstr[MAX_QPATH];
			Com_sprintf(packstr, sizeof(packstr), "\n%s/%s.pk3", search->pack->pakGamename, search->pack->pakBasename);

			if (f_w && !Q_stristr(mv_whitelist, packstr)) {
				search->pack->noref = qtrue;
				search->pack->referenced = 0;
			} else if (f_b && Q_stristr(mv_blacklist, packstr)) {
				search->pack->noref = qtrue;
				search->pack->referenced = 0;
			} else if (f_f && Q_stristr(mv_forcelist, packstr)) {
				search->pack->referenced |= FS_GENERAL_REF;
			}
		}
	}

	if (f_w) {
		FS_FCloseFile(f_w);
		Hunk_FreeTempMemory(mv_whitelist);
	}
	if (f_b) {
		FS_FCloseFile(f_b);
		Hunk_FreeTempMemory(mv_blacklist);
	}
	if (f_f) {
		FS_FCloseFile(f_f);
		Hunk_FreeTempMemory(mv_forcelist);
	}

	Com_Printf( "----------------------\n" );
	Com_Printf( "%d files in pk3 files\n", fs_packFiles );
}

/*
=====================
FS_LoadedPakChecksums

Returns a space separated string containing the checksums of all loaded pk3 files.
Servers with sv_pure set will get this string and pass it to clients.
=====================
*/
const char *FS_LoadedPakChecksums( void ) {
	static char	info[BIG_INFO_STRING];
	searchpath_t	*search;

	info[0] = 0;

	for ( search = fs_searchpaths ; search ; search = search->next ) {
		// is the element a pak file?
		if ( !search->pack ) {
			continue;
		}

		if (MV_GetCurrentGameversion() == VERSION_1_02 && (!Q_stricmp(search->pack->pakBasename, "assets2") || !Q_stricmp(search->pack->pakBasename, "assets5")))
			continue;

		if (MV_GetCurrentGameversion() == VERSION_1_03 && (!Q_stricmp(search->pack->pakBasename, "assets5")))
			continue;

		Q_strcat( info, sizeof( info ), va("%i ", search->pack->checksum ) );
	}

	return info;
}

/*
=====================
FS_LoadedPakNames

Returns a space separated string containing the names of all loaded pk3 files.
Servers with sv_pure set will get this string and pass it to clients.
=====================
*/
const char *FS_LoadedPakNames( void ) {
	static char	info[BIG_INFO_STRING];
	searchpath_t	*search;

	info[0] = 0;

	for ( search = fs_searchpaths ; search ; search = search->next ) {
		// is the element a pak file?
		if ( !search->pack ) {
			continue;
		}

		if (MV_GetCurrentGameversion() == VERSION_1_02 && (!Q_stricmp(search->pack->pakBasename, "assets2") || !Q_stricmp(search->pack->pakBasename, "assets5")))
			continue;

		if (MV_GetCurrentGameversion() == VERSION_1_03 && (!Q_stricmp(search->pack->pakBasename, "assets5")))
			continue;

		if (*info) {
			Q_strcat(info, sizeof( info ), " " );
		}
		Q_strcat( info, sizeof( info ), search->pack->pakBasename );
	}

	return info;
}

/*
=====================
FS_LoadedPakPureChecksums

Returns a space separated string containing the pure checksums of all loaded pk3 files.
Servers with sv_pure use these checksums to compare with the checksums the clients send
back to the server.
=====================
*/
const char *FS_LoadedPakPureChecksums( void ) {
	static char	info[BIG_INFO_STRING];
	searchpath_t	*search;

	info[0] = 0;

	for ( search = fs_searchpaths ; search ; search = search->next ) {
		// is the element a pak file?
		if ( !search->pack ) {
			continue;
		}

		if (MV_GetCurrentGameversion() == VERSION_1_02 && (!Q_stricmp(search->pack->pakBasename, "assets2") || !Q_stricmp(search->pack->pakBasename, "assets5")))
			continue;

		if (MV_GetCurrentGameversion() == VERSION_1_03 && (!Q_stricmp(search->pack->pakBasename, "assets5")))
			continue;

		Q_strcat( info, sizeof( info ), va("%i ", search->pack->pure_checksum ) );
	}

	return info;
}

/*
=====================
FS_ReferencedPakChecksums

Returns a space separated string containing the checksums of all referenced pk3 files.
The server will send this to the clients so they can check which files should be auto-downloaded.
=====================
*/
const char *FS_ReferencedPakChecksums( void ) {
	static char	info[BIG_INFO_STRING];
	searchpath_t *search;

	info[0] = 0;


	for ( search = fs_searchpaths ; search ; search = search->next ) {
		// is the element a pak file?
		if ( search->pack ) {
			if (MV_GetCurrentGameversion() == VERSION_1_02 && (!Q_stricmp(search->pack->pakBasename, "assets2") || !Q_stricmp(search->pack->pakBasename, "assets5")))
				continue;

			if (MV_GetCurrentGameversion() == VERSION_1_03 && (!Q_stricmp(search->pack->pakBasename, "assets5")))
				continue;

			if (search->pack->referenced) {
				Q_strcat(info, sizeof(info), va("%i ", search->pack->checksum));
			}
		}
	}

	return info;
}

/*
=====================
FS_ReferencedPakPureChecksums

Returns a space separated string containing the pure checksums of all referenced pk3 files.
Servers with sv_pure set will get this string back from clients for pure validation

The string has a specific order, "cgame ui @ ref1 ref2 ref3 ..."
=====================
*/
const char *FS_ReferencedPakPureChecksums( void ) {
	static char	info[BIG_INFO_STRING];
	searchpath_t	*search;
	int nFlags, numPaks, checksum;

	info[0] = 0;

	checksum = fs_checksumFeed;
	numPaks = 0;
	for (nFlags = FS_CGAME_REF; nFlags; nFlags = nFlags >> 1) {
		if (nFlags & FS_GENERAL_REF) {
			// add a delimter between must haves and general refs
			//Q_strcat(info, sizeof(info), "@ ");
			info[strlen(info)+1] = '\0';
			info[strlen(info)+2] = '\0';
			info[strlen(info)] = '@';
			info[strlen(info)] = ' ';
		}
		for ( search = fs_searchpaths ; search ; search = search->next ) {
			// is the element a pak file and has it been referenced based on flag?
			if ( search->pack && (search->pack->referenced & nFlags)) {
				if (MV_GetCurrentGameversion() == VERSION_1_02 && (!Q_stricmp(search->pack->pakBasename, "assets2") || !Q_stricmp(search->pack->pakBasename, "assets5")))
					continue;

				if (MV_GetCurrentGameversion() == VERSION_1_03 && (!Q_stricmp(search->pack->pakBasename, "assets5")))
					continue;

				Q_strcat( info, sizeof( info ), va("%i ", search->pack->pure_checksum ) );
				if (nFlags & (FS_CGAME_REF | FS_UI_REF)) {
					break;
				}
				checksum ^= search->pack->pure_checksum;
				numPaks++;
			}
		}
		if (fs_fakeChkSum != 0) {
			// only added if a non-pure file is referenced
			Q_strcat( info, sizeof( info ), va("%i ", fs_fakeChkSum ) );
		}
	}
	// last checksum is the encoded number of referenced pk3s
	checksum ^= numPaks;
	Q_strcat( info, sizeof( info ), va("%i ", checksum ) );

	return info;
}

/*
=====================
FS_ReferencedPakNames

Returns a space separated string containing the names of all referenced pk3 files.
The server will send this to the clients so they can check which files should be auto-downloaded.
=====================
*/
const char *FS_ReferencedPakNames( void ) {
	static char	info[BIG_INFO_STRING];
	searchpath_t	*search;

	info[0] = 0;

	// we want to return ALL pk3's from the fs_game path
	// and referenced one's from base
	for ( search = fs_searchpaths ; search ; search = search->next ) {
		// is the element a pak file?
		if ( search->pack ) {
			if (MV_GetCurrentGameversion() == VERSION_1_02 && (!Q_stricmp(search->pack->pakBasename, "assets2") || !Q_stricmp(search->pack->pakBasename, "assets5")))
				continue;

			if (MV_GetCurrentGameversion() == VERSION_1_03 && (!Q_stricmp(search->pack->pakBasename, "assets5")))
				continue;

			if (search->pack->referenced) {
				if (*info) {
					Q_strcat(info, sizeof( info ), " " );
				}

				Q_strcat(info, sizeof(info), search->pack->pakGamename);
				Q_strcat(info, sizeof(info), "/");
				Q_strcat(info, sizeof(info), search->pack->pakBasename);
			}
		}
	}

	return info;
}

/*
=====================
FS_ClearPakReferences
=====================
*/
void FS_ClearPakReferences( int flags ) {
	searchpath_t *search;

	if ( !flags ) {
		flags = -1;
	}
	for ( search = fs_searchpaths; search; search = search->next ) {
		// is the element a pak file and has it been referenced?
		if ( search->pack ) {
			search->pack->referenced &= ~flags;
		}
	}
}


/*
=====================
FS_PureServerSetLoadedPaks

If the string is empty, all data sources will be allowed.
If not empty, only pk3 files that match one of the space
separated checksums will be checked for files, with the
exception of .cfg and .dat files.
=====================
*/
void FS_PureServerSetLoadedPaks( const char *pakSums, const char *pakNames ) {
	int		i, c, d;

	Cmd_TokenizeString( pakSums );

	c = Cmd_Argc();
	if ( c > MAX_SEARCH_PATHS ) {
		c = MAX_SEARCH_PATHS;
	}

	fs_numServerPaks = c;

	for ( i = 0 ; i < c ; i++ ) {
		fs_serverPaks[i] = atoi( Cmd_Argv( i ) );
	}

	if (fs_numServerPaks) {
		Com_DPrintf( "Connected to a pure server.\n" );
	}

	for ( i = 0 ; i < c ; i++ ) {
		if (fs_serverPakNames[i]) {
			Z_Free((void *)fs_serverPakNames[i]);
		}
		fs_serverPakNames[i] = NULL;
	}
	if ( pakNames && *pakNames ) {
		Cmd_TokenizeString( pakNames );

		d = Cmd_Argc();
		if ( d > MAX_SEARCH_PATHS ) {
			d = MAX_SEARCH_PATHS;
		}

		for ( i = 0 ; i < d ; i++ ) {
			fs_serverPakNames[i] = CopyString( Cmd_Argv( i ) );
		}
	}
}

/*
=====================
FS_PureServerSetReferencedPaks

The checksums and names of the pk3 files referenced at the server
are sent to the client and stored here. The client will use these
checksums to see if any pk3 files need to be auto-downloaded.
=====================
*/
void FS_PureServerSetReferencedPaks( const char *pakSums, const char *pakNames ) {
	int		i, c, d;

	Cmd_TokenizeString( pakSums );

	c = Cmd_Argc();
	if ( c > MAX_SEARCH_PATHS ) {
		c = MAX_SEARCH_PATHS;
	}

	fs_numServerReferencedPaks = c;

	for ( i = 0 ; i < c ; i++ ) {
		fs_serverReferencedPaks[i] = atoi( Cmd_Argv( i ) );
	}

	for ( i = 0 ; i < c ; i++ ) {
		if (fs_serverReferencedPakNames[i]) {
			Z_Free((void *)fs_serverReferencedPakNames[i]);
		}
		fs_serverReferencedPakNames[i] = NULL;
	}
	if ( pakNames && *pakNames ) {
		Cmd_TokenizeString( pakNames );

		d = Cmd_Argc();
		if ( d > MAX_SEARCH_PATHS ) {
			d = MAX_SEARCH_PATHS;
		}

		for ( i = 0 ; i < d ; i++ ) {
			fs_serverReferencedPakNames[i] = CopyString( Cmd_Argv( i ) );
		}
	}
}

/*
================
FS_InitFilesystem

Called only at inital startup, not when the filesystem
is resetting due to a game change
================
*/
void FS_InitFilesystem( void ) {
	// allow command line parms to override our defaults
	// we have to specially handle this, because normal command
	// line variable sets don't happen until after the filesystem
	// has already been initialized
	Com_StartupVariable( "fs_basepath" );
	Com_StartupVariable( "fs_homepath" );
#if !defined(PORTABLE)
	Com_StartupVariable( "fs_assetspath" );
#endif
	Com_StartupVariable( "fs_game" );
	Com_StartupVariable( "fs_copyfiles" );
	Com_StartupVariable( "fs_restrict" );

	// try to start up normally
	FS_Startup( BASEGAME );

	// if we can't find default.cfg, assume that the paths are
	// busted and error out now, rather than getting an unreadable
	// graphics screen when the font fails to load
	if ( FS_ReadFile( "mpdefault.cfg", NULL ) <= 0 ) {
		Com_Error( ERR_FATAL, "Couldn't load mpdefault.cfg" );
		// bk001208 - SafeMode see below, FIXME?
	}

	Q_strncpyz(lastValidBase, fs_basepath->string, sizeof(lastValidBase));
	Q_strncpyz(lastValidGame, fs_gamedirvar->string, sizeof(lastValidGame));

  // bk001208 - SafeMode see below, FIXME?
}


/*
================
FS_Restart
================
*/
void FS_Restart( int checksumFeed ) {

	// free anything we currently have loaded
	FS_Shutdown(qfalse);

	// set the checksum feed
	fs_checksumFeed = checksumFeed;

	// clear pak references
	FS_ClearPakReferences(0);

	// try to start up normally
	FS_Startup( BASEGAME );

	// if we can't find default.cfg, assume that the paths are
	// busted and error out now, rather than getting an unreadable
	// graphics screen when the font fails to load
	if ( FS_ReadFile( "mpdefault.cfg", NULL ) <= 0 ) {
		// this might happen when connecting to a pure server not using BASEGAME/pak0.pk3
		// (for instance a TA demo server)
		if (lastValidBase[0]) {
			FS_PureServerSetLoadedPaks("", "");
			Cvar_Set("fs_basepath", lastValidBase);
			Cvar_Set("fs_gamedirvar", lastValidGame);
			lastValidBase[0] = '\0';
			lastValidGame[0] = '\0';
			FS_Restart(checksumFeed);
			Com_Error( ERR_DROP, "Invalid game folder" );
			return;
		}
		Com_Error( ERR_FATAL, "Couldn't load mpdefault.cfg" );
	}

	// bk010116 - new check before safeMode
	if ( Q_stricmp(fs_gamedirvar->string, lastValidGame) ) {
		// skip the jk2mpconfig.cfg if "safe" is on the command line
		if ( !Com_SafeMode() ) {
#ifdef DEDICATED
			Cbuf_AddText ("exec jk2mvserver.cfg\n");
#else
			Cbuf_AddText ("exec jk2mvconfig.cfg\n");
#endif
		}
	}

	Q_strncpyz(lastValidBase, fs_basepath->string, sizeof(lastValidBase));
	Q_strncpyz(lastValidGame, fs_gamedirvar->string, sizeof(lastValidGame));

}

/*
=================
FS_ConditionalRestart
restart if necessary
=================
*/
qboolean FS_ConditionalRestart( int checksumFeed ) {
	if( fs_gamedirvar->modified || checksumFeed != fs_checksumFeed ) {
		FS_Restart( checksumFeed );
		return qtrue;
	}
	return qfalse;
}

/*
========================================================================================

Handle based file calls for virtual machines

========================================================================================
*/

// We don't want VMs to be able to access the following files
static char *invalidExtensions[] = {
	"dll",
	"exe",
	"bat",
	"cmd",
	"dylib",
	"so",
	"qvm",
	"pk3",
};
static int invalidExtensionsAmount = sizeof(invalidExtensions) / sizeof(invalidExtensions[0]);

qboolean FS_IsInvalidExtension( const char *ext ) {
	int i;

	for ( i = 0; i < invalidExtensionsAmount; i++ ) {
		if ( !Q_stricmp(ext, invalidExtensions[i]) )
			return qtrue;
	}
	return qfalse;
}

// Invalid characters. Originally intended to be OS-specific, but considering that VMs run on different systems it's
// probably not a bad idea to share the same behavior on all systems.
qboolean FS_ContainsInvalidCharacters( const char *filename ) {
	static char *invalidCharacters = "<>:\"|?*";
	char *ptr = invalidCharacters;

	while ( *ptr ) {
		if ( strchr(filename, *ptr) )
			return qtrue;
		ptr++;
	}

	return qfalse;
}

int FS_FOpenFileByMode(const char *qpath, fileHandle_t *f, fsMode_t mode, module_t module) {
	return FS_FOpenFileByModeHash(qpath, f, mode, NULL, module);
}

int FS_FOpenFileByModeHash( const char *qpath, fileHandle_t *f, fsMode_t mode, unsigned long *hash, module_t module ) {
	int		r;
	qboolean	sync;
	char *resolved;
	char *realPath;

	sync = qfalse;

	// Only check the unresolved path for invalid characters, the os probably knows what it's doing
	if ( FS_ContainsInvalidCharacters(qpath) ) {
		Com_Printf( "FS_FOpenFileByMode: invalid filename (%s)\n", qpath );
		return -1;
	}

	// Prevent writing to files with some extensions to prevent bypassing several restrictions
	if ( mode != FS_READ ) {
		// Resolve paths
		resolved = Sys_ResolvePath( FS_BuildOSPath(fs_homepath->string, fs_gamedir, qpath) );
		if ( !strlen(resolved) ) return -1;
		realPath = Sys_RealPath( resolved );
		if ( !strlen(realPath) ) return -1;

		// Check both, the resolved and the real path, in case there is a symlink
		if ( FS_IsInvalidExtension(get_filename_ext(resolved)) || FS_IsInvalidExtension(get_filename_ext(realPath)) ) {
			Com_Printf( "FS_FOpenFileByMode: blocked writing binary file (%s) [%s].\n", resolved, realPath );
			Com_Error( ERR_DROP, "FS_FOpenFileByMode: blocked writing binary file.\n" );
			return -1;
		}
	}

	switch( mode ) {
	case FS_READ:
		r = FS_FOpenFileReadHash( qpath, f, qtrue, hash, module );
		break;
	case FS_WRITE:
		*f = FS_FOpenFileWrite( qpath, module );
		r = 0;
		if (*f == 0) {
			r = -1;
		}
		break;
	case FS_APPEND_SYNC:
		sync = qtrue;
	case FS_APPEND:
		*f = FS_FOpenFileAppend( qpath, module );
		r = 0;
		if (*f == 0) {
			r = -1;
		}
		break;
	default:
		Com_Error( ERR_FATAL, "FSH_FOpenFile: bad mode" );
		return -1;
	}

	if (!f) {
		return r;
	}

	if ( *f ) {
		fsh[*f].fileSize = r;
	}
	fsh[*f].handleSync = sync;

	return r;
}

int	FS_FTell( fileHandle_t f, module_t module ) {
	int pos;

	FS_CHECKHANDLE(f, module, -1)

	if (fsh[f].zipFile == qtrue) {
		pos = unztell(fsh[f].handleFiles.file.z);
	} else {
		pos = ftell(fsh[f].handleFiles.file.o);
	}
	return pos;
}

void FS_Flush( fileHandle_t f, module_t module ) {
	fflush(FS_FileForHandle(f, module));
}

// only referenced pk3 files can be downloaded
// this automatically fixes q3dirtrav
// returns the path to the GameData directory of the requested file.
const char *FS_MV_VerifyDownloadPath(const char *pk3file) {
	searchpath_t	*search;

	for (search = fs_searchpaths; search; search = search->next) {
		if (!search->pack)
			continue;
		
		if (FS_idPak(search->pack))
			continue;

		char tmp[MAX_OSPATH];
		Com_sprintf(tmp, sizeof(tmp), "%s/%s.pk3", search->pack->pakGamename, search->pack->pakBasename);

		if (!Q_stricmp(tmp, pk3file)) {
			if (search->pack->noref)
				return NULL;

			if (search->pack->referenced) {
				static char gameDataPath[MAX_OSPATH];
				Q_strncpyz(gameDataPath, search->pack->pakFilename, sizeof(gameDataPath));

				char *sp = strrchr(gameDataPath, PATH_SEP);
				*sp = '\0';
				sp = strrchr(gameDataPath, PATH_SEP);
				*sp = '\0';

				return gameDataPath;
			}
		}
	}

	return NULL;
}

static void FS_StripTrailingPathSeparator( char *path ) {
	size_t len = strlen(path);

	if (len > 0 && path[len - 1] == '/') {
		path[len - 1] = '\0';
	}
}

void FS_FilenameCompletion( const char *dir, const char *ext, qboolean stripExt, callbackFunc_t callback ) { // for auto-complete (copied from OpenJK)
	int nfiles;
	const char **filenames;
	char filename[MAX_STRING_CHARS];

	char realExt[32];
	// add: if ext is like "cfg|txt|bsp", we will first use a list of cfg files, then txt files, then bsp files
	const char *pch = strchr(ext, '|');

	if (pch)
		Q_strncpyz(realExt, ext , pch - ext + 1);	//parse out the ext before the '|', i.e. "txt|cfg" will turn to "txt"
	else
		Q_strncpyz(realExt, ext, sizeof(realExt));

	filenames = FS_ListFilteredFiles( dir, realExt, NULL, &nfiles, qfalse );

	FS_SortFileList( filenames, nfiles );

	// pass all the files to callback (FindMatches)
	for ( int i=0; i<nfiles; i++ ) {
		char *converted = FS_ConvertPath( filenames[i] );

		if ( stripExt ) {
			COM_StripExtension( converted, filename, sizeof( filename ) );
			FS_StripTrailingPathSeparator( filename );
		} else {
			Q_strncpyz( filename, converted, sizeof( filename ) );
		}

		callback( filename );
	}
	FS_FreeFileList( filenames );

	if (pch)
		//there are remaining extensions to list.
		FS_FilenameCompletion(dir, pch + 1, stripExt, callback);
}

int FS_GetDLList(dlfile_t *files, const int maxfiles) {
	const char **dirs, **pakfiles;
	int c, d, paknum, dirnum, filesnum;
	char *gamepath;
	int ret = 0;

	// get all fs_game dirs
	dirs = Sys_ListFiles(fs_homepath->string, "/", NULL, &dirnum, qtrue);
	if (!dirs) {
		return qfalse;
	}

	for (filesnum = 0, d = 0; d < dirnum; d++) {
		if (!strcmp(dirs[d], ".") || !strcmp(dirs[d], "..")) {
			continue;
		}

		gamepath = FS_BuildOSPath(fs_homepath->string, dirs[d], "");
		pakfiles = Sys_ListFiles(gamepath, ".pk3", NULL, &paknum, qfalse);
		if (!pakfiles) {
			continue;
		}

		for (c = 0; c < paknum && filesnum < maxfiles; c++, filesnum++) {
			if (Q_stricmpn(pakfiles[c], "dl_", 3)) {
				continue;
			}

			Com_sprintf(files->name, sizeof(files->name), "%s/%s", dirs[d], pakfiles[c]);
			files->blacklisted = qfalse;

			files++; ret++;
		}

		Sys_FreeFileList(pakfiles);
	}

	Sys_FreeFileList(dirs);
	return ret;
}

qboolean FS_RMDLPrefix(const char *qpath) {
	char old_ospath[MAX_OSPATH], new_ospath[MAX_OSPATH];

	Com_sprintf(old_ospath, sizeof(old_ospath), "%s/%s", fs_homepath->string, qpath);
	FS_ReplaceSeparators(old_ospath);

	const char *name = strchr(qpath, '/');
	if (name && name != qpath && !strncmp(name, "/dl_", 4) && strlen(name) > 4) {
		char newqpath[MAX_QPATH];

		memcpy(newqpath, qpath, name - qpath + 1);
		newqpath[name - qpath + 1] = '\0';
		Q_strcat(newqpath, sizeof(newqpath), name + 4);

		Com_sprintf(new_ospath, sizeof(new_ospath), "%s/%s", fs_homepath->string, newqpath);
		FS_ReplaceSeparators(new_ospath);

		return (qboolean)!!rename(old_ospath, new_ospath);
	}

	return qtrue;
}

qboolean FS_DeleteDLFile(const char *qpath) {
	char ospath[MAX_OSPATH];

	Com_sprintf(ospath, sizeof(ospath), "%s/%s", fs_homepath->string, qpath);
	FS_ReplaceSeparators(ospath);

	return (qboolean)!!remove(ospath);
}
