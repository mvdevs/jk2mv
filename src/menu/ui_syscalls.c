// Copyright (C) 1999-2000 Id Software, Inc.
//
#include <stdint.h>
#include "ui_local.h"

// this file is only included when building a dll
// syscalls.asm is included instead when building a qvm

static intptr_t (QDECL *engine_syscall)( intptr_t arg, ... );

LIBEXPORT void QDECL dllEntry(intptr_t (QDECL *syscallptr)(intptr_t arg, ...)) {
	engine_syscall = syscallptr;
}

int PASSFLOAT( float x ) {
	floatint_t fi;
	fi.f = x;
	return fi.i;
}

void trap_Print( const char *string ) {
	engine_syscall( UI_PRINT, string );
}

void Q_NORETURN trap_Error( const char *string ) {
	engine_syscall( UI_ERROR, string );
	q_unreachable();
}

int trap_Milliseconds( void ) {
	return engine_syscall( UI_MILLISECONDS );
}

void trap_Cvar_Register( vmCvar_t *cvar, const char *var_name, const char *value, int flags ) {
	engine_syscall( UI_CVAR_REGISTER, cvar, var_name, value, flags );
}

void trap_Cvar_Update( vmCvar_t *cvar ) {
	engine_syscall( UI_CVAR_UPDATE, cvar );
}

void trap_Cvar_Set( const char *var_name, const char *value ) {
	engine_syscall( UI_CVAR_SET, var_name, value );
}

float trap_Cvar_VariableValue( const char *var_name ) {
	floatint_t fi;
	fi.i = engine_syscall( UI_CVAR_VARIABLEVALUE, var_name );
	return fi.f;
}

void trap_Cvar_VariableStringBuffer( const char *var_name, char *buffer, int bufsize ) {
	engine_syscall( UI_CVAR_VARIABLESTRINGBUFFER, var_name, buffer, bufsize );
}

void trap_Cvar_SetValue( const char *var_name, float value ) {
	engine_syscall( UI_CVAR_SETVALUE, var_name, PASSFLOAT( value ) );
}

void trap_Cvar_Reset( const char *name ) {
	engine_syscall( UI_CVAR_RESET, name );
}

void trap_Cvar_Create( const char *var_name, const char *var_value, int flags ) {
	engine_syscall( UI_CVAR_CREATE, var_name, var_value, flags );
}

void trap_Cvar_InfoStringBuffer( int bit, char *buffer, int bufsize ) {
	engine_syscall( UI_CVAR_INFOSTRINGBUFFER, bit, buffer, bufsize );
}

int trap_Argc( void ) {
	return engine_syscall( UI_ARGC );
}

void trap_Argv( int n, char *buffer, int bufferLength ) {
	engine_syscall( UI_ARGV, n, buffer, bufferLength );
}

void trap_Cmd_ExecuteText( cbufExec_t exec_when, const char *text ) {
	engine_syscall( UI_CMD_EXECUTETEXT, exec_when, text );
}

int trap_FS_FOpenFile( const char *qpath, fileHandle_t *f, fsMode_t mode ) {
	return engine_syscall( UI_FS_FOPENFILE, qpath, f, mode );
}

void trap_FS_Read( void *buffer, int len, fileHandle_t f ) {
	engine_syscall( UI_FS_READ, buffer, len, f );
}

void trap_FS_Write( const void *buffer, int len, fileHandle_t f ) {
	engine_syscall( UI_FS_WRITE, buffer, len, f );
}

void trap_FS_FCloseFile( fileHandle_t f ) {
	engine_syscall( UI_FS_FCLOSEFILE, f );
}

int trap_FS_GetFileList(  const char *path, const char *extension, char *listbuf, int bufsize ) {
	return engine_syscall( UI_FS_GETFILELIST, path, extension, listbuf, bufsize );
}

qhandle_t trap_R_RegisterModel( const char *name ) {
	return engine_syscall( UI_R_REGISTERMODEL, name );
}

qhandle_t trap_R_RegisterSkin( const char *name ) {
	return engine_syscall( UI_R_REGISTERSKIN, name );
}

qhandle_t trap_R_RegisterFont( const char *fontName )
{
	return engine_syscall( UI_R_REGISTERFONT, fontName);
}

int	trap_R_Font_StrLenPixels(const char *text, const int iFontIndex, const float scale)
{
	return engine_syscall( UI_R_FONT_STRLENPIXELS, text, iFontIndex, PASSFLOAT(scale));
}

int trap_R_Font_StrLenChars(const char *text)
{
	return engine_syscall( UI_R_FONT_STRLENCHARS, text);
}

int trap_R_Font_HeightPixels(const int iFontIndex, const float scale)
{
	return engine_syscall( UI_R_FONT_STRHEIGHTPIXELS, iFontIndex, PASSFLOAT(scale));
}

void trap_R_Font_DrawString(int ox, int oy, const char *text, const float *rgba, const int setIndex, int iCharLimit, const float scale)
{
	engine_syscall( UI_R_FONT_DRAWSTRING, ox, oy, text, rgba, setIndex, iCharLimit, PASSFLOAT(scale));
}

qboolean trap_Language_IsAsian(void)
{
	return engine_syscall( UI_LANGUAGE_ISASIAN );
}

qboolean trap_Language_UsesSpaces(void)
{
	return engine_syscall( UI_LANGUAGE_USESSPACES );
}

unsigned int trap_AnyLanguage_ReadCharFromString( const char *psText, int *piAdvanceCount, qboolean *pbIsTrailingPunctuation )
{
	return engine_syscall( UI_ANYLANGUAGE_READCHARFROMSTRING, psText, piAdvanceCount, pbIsTrailingPunctuation);
}

qhandle_t trap_R_RegisterShaderNoMip( const char *name ) {
	return engine_syscall( UI_R_REGISTERSHADERNOMIP, name );
}

void trap_R_ClearScene( void ) {
	engine_syscall( UI_R_CLEARSCENE );
}

void trap_R_AddRefEntityToScene( const refEntity_t *re ) {
	engine_syscall( UI_R_ADDREFENTITYTOSCENE, re );
}

void trap_R_AddPolyToScene( qhandle_t hShader , int numVerts, const polyVert_t *verts ) {
	engine_syscall( UI_R_ADDPOLYTOSCENE, hShader, numVerts, verts );
}

void trap_R_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {
	engine_syscall( UI_R_ADDLIGHTTOSCENE, org, PASSFLOAT(intensity), PASSFLOAT(r), PASSFLOAT(g), PASSFLOAT(b) );
}

void trap_R_RenderScene( const refdef_t *fd ) {
	engine_syscall( UI_R_RENDERSCENE, fd );
}

void trap_R_SetColor( const float *rgba ) {
	engine_syscall( UI_R_SETCOLOR, rgba );
}

void trap_R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader ) {
	engine_syscall( UI_R_DRAWSTRETCHPIC, PASSFLOAT(x), PASSFLOAT(y), PASSFLOAT(w), PASSFLOAT(h), PASSFLOAT(s1), PASSFLOAT(t1), PASSFLOAT(s2), PASSFLOAT(t2), hShader );
}

void	trap_R_ModelBounds( clipHandle_t model, vec3_t mins, vec3_t maxs ) {
	engine_syscall( UI_R_MODELBOUNDS, model, mins, maxs );
}

void trap_UpdateScreen( void ) {
	engine_syscall( UI_UPDATESCREEN );
}

int trap_CM_LerpTag( orientation_t *tag, clipHandle_t mod, int startFrame, int endFrame, float frac, const char *tagName ) {
	return engine_syscall( UI_CM_LERPTAG, tag, mod, startFrame, endFrame, PASSFLOAT(frac), tagName );
}

void trap_S_StartLocalSound( sfxHandle_t sfx, int channelNum ) {
	engine_syscall( UI_S_STARTLOCALSOUND, sfx, channelNum );
}

sfxHandle_t	trap_S_RegisterSound( const char *sample ) {
	return engine_syscall( UI_S_REGISTERSOUND, sample );
}

void trap_Key_KeynumToStringBuf( int keynum, char *buf, int buflen ) {
	engine_syscall( UI_KEY_KEYNUMTOSTRINGBUF, keynum, buf, buflen );
}

void trap_Key_GetBindingBuf( int keynum, char *buf, int buflen ) {
	engine_syscall( UI_KEY_GETBINDINGBUF, keynum, buf, buflen );
}

void trap_Key_SetBinding( int keynum, const char *binding ) {
	engine_syscall( UI_KEY_SETBINDING, keynum, binding );
}

qboolean trap_Key_IsDown( int keynum ) {
	return engine_syscall( UI_KEY_ISDOWN, keynum );
}

qboolean trap_Key_GetOverstrikeMode( void ) {
	return engine_syscall( UI_KEY_GETOVERSTRIKEMODE );
}

void trap_Key_SetOverstrikeMode( qboolean state ) {
	engine_syscall( UI_KEY_SETOVERSTRIKEMODE, state );
}

void trap_Key_ClearStates( void ) {
	engine_syscall( UI_KEY_CLEARSTATES );
}

int trap_Key_GetCatcher( void ) {
	return engine_syscall( UI_KEY_GETCATCHER );
}

void trap_Key_SetCatcher( int catcher ) {
	engine_syscall( UI_KEY_SETCATCHER, catcher );
}

void trap_GetClipboardData( char *buf, int bufsize ) {
	engine_syscall( UI_GETCLIPBOARDDATA, buf, bufsize );
}

void trap_GetClientState( uiClientState_t *state ) {
	engine_syscall( UI_GETCLIENTSTATE, state );
}

void trap_GetGlconfig( vmglconfig_t *glconfig ) {
	engine_syscall( UI_GETGLCONFIG, glconfig );
}

int trap_GetConfigString( int index, char* buff, int buffsize ) {
	return engine_syscall( UI_GETCONFIGSTRING, index, buff, buffsize );
}

int	trap_LAN_GetServerCount( int source ) {
	return engine_syscall( UI_LAN_GETSERVERCOUNT, source );
}

void trap_LAN_GetServerAddressString( int source, int n, char *buf, int buflen ) {
	engine_syscall( UI_LAN_GETSERVERADDRESSSTRING, source, n, buf, buflen );
}

void trap_LAN_GetServerInfo( int source, int n, char *buf, int buflen ) {
	engine_syscall( UI_LAN_GETSERVERINFO, source, n, buf, buflen );
}

int trap_LAN_GetServerPing( int source, int n ) {
	return engine_syscall( UI_LAN_GETSERVERPING, source, n );
}

int trap_LAN_GetPingQueueCount( void ) {
	return engine_syscall( UI_LAN_GETPINGQUEUECOUNT );
}

int trap_LAN_ServerStatus( const char *serverAddress, char *serverStatus, int maxLen ) {
	return engine_syscall( UI_LAN_SERVERSTATUS, serverAddress, serverStatus, maxLen );
}

void trap_LAN_SaveCachedServers() {
	engine_syscall( UI_LAN_SAVECACHEDSERVERS );
}

void trap_LAN_LoadCachedServers() {
	engine_syscall( UI_LAN_LOADCACHEDSERVERS );
}

void trap_LAN_ResetPings(int n) {
	engine_syscall( UI_LAN_RESETPINGS, n );
}

void trap_LAN_ClearPing( int n ) {
	engine_syscall( UI_LAN_CLEARPING, n );
}

void trap_LAN_GetPing( int n, char *buf, int buflen, int *pingtime ) {
	engine_syscall( UI_LAN_GETPING, n, buf, buflen, pingtime );
}

void trap_LAN_GetPingInfo( int n, char *buf, int buflen ) {
	engine_syscall( UI_LAN_GETPINGINFO, n, buf, buflen );
}

void trap_LAN_MarkServerVisible( int source, int n, qboolean visible ) {
	engine_syscall( UI_LAN_MARKSERVERVISIBLE, source, n, visible );
}

int trap_LAN_ServerIsVisible( int source, int n) {
	return engine_syscall( UI_LAN_SERVERISVISIBLE, source, n );
}

qboolean trap_LAN_UpdateVisiblePings( int source ) {
	return engine_syscall( UI_LAN_UPDATEVISIBLEPINGS, source );
}

int trap_LAN_AddServer(int source, const char *name, const char *addr) {
	return engine_syscall( UI_LAN_ADDSERVER, source, name, addr );
}

void trap_LAN_RemoveServer(int source, const char *addr) {
	engine_syscall( UI_LAN_REMOVESERVER, source, addr );
}

int trap_LAN_CompareServers( int source, int sortKey, int sortDir, int s1, int s2 ) {
	return engine_syscall( UI_LAN_COMPARESERVERS, source, sortKey, sortDir, s1, s2 );
}

int trap_MemoryRemaining( void ) {
	return engine_syscall( UI_MEMORY_REMAINING );
}

#ifdef USE_CD_KEY

void trap_GetCDKey( char *buf, int buflen ) {
	engine_syscall( UI_GET_CDKEY, buf, buflen );
}

void trap_SetCDKey( char *buf ) {
	engine_syscall( UI_SET_CDKEY, buf );
}

qboolean trap_VerifyCDKey( const char *key, const char *chksum) {
	return engine_syscall( UI_VERIFY_CDKEY, key, chksum);
}

#endif // USE_CD_KEY

int trap_PC_AddGlobalDefine( char *define ) {
	return engine_syscall( UI_PC_ADD_GLOBAL_DEFINE, define );
}

int trap_PC_LoadSource( const char *filename ) {
	return engine_syscall( UI_PC_LOAD_SOURCE, filename );
}

int trap_PC_FreeSource( int handle ) {
	return engine_syscall( UI_PC_FREE_SOURCE, handle );
}

int trap_PC_ReadToken( int handle, pc_token_t *pc_token ) {
	return engine_syscall( UI_PC_READ_TOKEN, handle, pc_token );
}

int trap_PC_SourceFileAndLine( int handle, char *filename, int *line ) {
	return engine_syscall( UI_PC_SOURCE_FILE_AND_LINE, handle, filename, line );
}

int trap_PC_LoadGlobalDefines ( const char* filename )
{
	return engine_syscall ( UI_PC_LOAD_GLOBAL_DEFINES, filename );
}

void trap_PC_RemoveAllGlobalDefines ( void )
{
	engine_syscall ( UI_PC_REMOVE_ALL_GLOBAL_DEFINES );
}

void trap_S_StopBackgroundTrack( void ) {
	engine_syscall( UI_S_STOPBACKGROUNDTRACK );
}

void trap_S_StartBackgroundTrack( const char *intro, const char *loop, qboolean bReturnWithoutStarting) {
	engine_syscall( UI_S_STARTBACKGROUNDTRACK, intro, loop, bReturnWithoutStarting );
}

int trap_RealTime(qtime_t *qtime) {
	return engine_syscall( UI_REAL_TIME, qtime );
}

// this returns a handle.  arg0 is the name in the format "idlogo.roq", set arg1 to NULL, alteredstates to qfalse (do not alter gamestate)
int trap_CIN_PlayCinematic( const char *arg0, int xpos, int ypos, int width, int height, int bits) {
  return engine_syscall(UI_CIN_PLAYCINEMATIC, arg0, xpos, ypos, width, height, bits);
}

// stops playing the cinematic and ends it.  should always return FMV_EOF
// cinematics must be stopped in reverse order of when they are started
e_status trap_CIN_StopCinematic(int handle) {
  return engine_syscall(UI_CIN_STOPCINEMATIC, handle);
}


// will run a frame of the cinematic but will not draw it.  Will return FMV_EOF if the end of the cinematic has been reached.
e_status trap_CIN_RunCinematic (int handle) {
  return engine_syscall(UI_CIN_RUNCINEMATIC, handle);
}


// draws the current frame
void trap_CIN_DrawCinematic (int handle) {
  engine_syscall(UI_CIN_DRAWCINEMATIC, handle);
}


// allows you to resize the animation dynamically
void trap_CIN_SetExtents (int handle, int x, int y, int w, int h) {
  engine_syscall(UI_CIN_SETEXTENTS, handle, x, y, w, h);
}


void	trap_R_RemapShader( const char *oldShader, const char *newShader, const char *timeOffset ) {
	engine_syscall( UI_R_REMAP_SHADER, oldShader, newShader, timeOffset );
}

qboolean trap_SP_Register(char *file )
{
	return engine_syscall( UI_SP_REGISTER,file );
}

int trap_SP_GetStringTextString(const char *text, char *buffer, int bufferLength)
{
	return engine_syscall( UI_SP_GETSTRINGTEXTSTRING, text, buffer, bufferLength );
}
/*
Ghoul2 Insert Start
*/
qboolean trap_G2API_SetBoneAngles(void *ghoul2, int modelIndex, const char *boneName, const vec3_t angles, const int flags,
								const int up, const int right, const int forward, qhandle_t *modelList,
								int blendTime , int currentTime )
{
	return (engine_syscall(UI_G2_ANGLEOVERRIDE, ghoul2, modelIndex, boneName, angles, flags, up, right, forward, modelList, blendTime, currentTime));
}

/*
Ghoul2 Insert End
*/

void trap_CL_ContinueCurrentDownload(dldecision_t decision) {
	engine_syscall(UI_MV_CONTINUE_DOWNLOAD, decision);
}

int trap_FS_GetDLList(dlfile_t *files, int maxfiles) {
	return engine_syscall(UI_MV_GETDLLIST, files, maxfiles);
}

qboolean trap_FS_RMDLPrefix(const char *qpath) {
	return engine_syscall(UI_MV_RMDLPREFIX, qpath);
}

qboolean trap_UI_DeleteDLFile(const dlfile_t *file) {
	return engine_syscall(UI_MV_DELDLFILE, file);
}

