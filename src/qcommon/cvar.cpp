// cvar.c -- dynamic variable tracking

#include "../qcommon/q_shared.h"
#include "qcommon.h"

cvar_t		*cvar_vars;
cvar_t		*cvar_cheats;
int			cvar_modifiedFlags;

#define	MAX_CVARS	2048 // increased in jk2mv
cvar_t		cvar_indexes[MAX_CVARS];
int			cvar_numIndexes;

#define FILE_HASH_SIZE		256
static	cvar_t*		hashTable[FILE_HASH_SIZE];

cvar_t *Cvar_Set2( const char *var_name, const char *value, qboolean force);

/*
================
return a hash value for the filename
================
*/
static int generateHashValue( const char *fname ) {
	int		i;
	int		hash;
	char	letter;

	hash = 0;
	i = 0;
	while (fname[i] != '\0') {
		letter = tolower(fname[i]);
		hash+=(int)(letter)*(i+119);
		i++;
	}
	hash &= (FILE_HASH_SIZE-1);
	return hash;
}

/*
============
Cvar_ValidateString
============
*/
static qboolean Cvar_ValidateString( const char *s ) {
	if ( !s ) {
		return qfalse;
	}
	if ( strchr( s, '\\' ) ) {
		return qfalse;
	}
	if ( strchr( s, '\"' ) ) {
		return qfalse;
	}
	if ( strchr( s, ';' ) ) {
		return qfalse;
	}
	return qtrue;
}

/*
============
Cvar_FindVar
============
*/
cvar_t *Cvar_FindVar( const char *var_name ) {
	cvar_t	*var;
	int hash;

	hash = generateHashValue(var_name);

	for (var=hashTable[hash] ; var ; var=var->hashNext) {
		if (!Q_stricmp(var_name, var->name)) {
			return var;
		}
	}

	return NULL;
}

/*
============
Cvar_VariableValue
============
*/
float Cvar_VariableValue( const char *var_name, qboolean isVmCall ) {
	cvar_t	*var;

	var = Cvar_FindVar (var_name);
	if (!var)
	{
		return 0;
	}
	else if ( isVmCall && var->flags & CVAR_VM_NOREAD )
	{
		Com_DPrintf("Cvar_VariableValue: VM tried to read CVAR_VM_NOREAD %s\n", var_name);
		return 0; // No access to CVAR_VM_NOREAD
	}
	return var->value;
}
float Cvar_VariableValue( const char *var_name ) {
	return Cvar_VariableValue( var_name, qfalse );
}


/*
============
Cvar_VariableIntegerValue
============
*/
int Cvar_VariableIntegerValue( const char *var_name, qboolean isVmCall ) {
	cvar_t	*var;

	var = Cvar_FindVar (var_name);
	if (!var)
		return 0;
	else if ( isVmCall && var->flags & CVAR_VM_NOREAD )
	{
		Com_DPrintf("Cvar_VariableIntegerValue: VM tried to read CVAR_VM_NOREAD %s\n", var_name);
		return 0; // No access to CVAR_VM_NOREAD
	}
	return var->integer;
}
int Cvar_VariableIntegerValue( const char *var_name ) {
	return Cvar_VariableIntegerValue( var_name, qfalse );
}


/*
============
Cvar_VariableString
============
*/
const char *Cvar_VariableString( const char *var_name ) {
	cvar_t *var;

	var = Cvar_FindVar (var_name);
	if (!var)
		return "";
	return var->string;
}


/*
============
Cvar_VariableStringBuffer
============
*/
void Cvar_VariableStringBuffer( const char *var_name, char *buffer, int bufsize, qboolean isVmCall ) {
	cvar_t *var;

	var = Cvar_FindVar (var_name);
	if (!var) {
		*buffer = 0;
	}
	else if ( isVmCall && var->flags & CVAR_VM_NOREAD )
	{
		Com_DPrintf("Cvar_VariableStringBuffer: VM tried to read CVAR_VM_NOREAD %s\n", var_name);
		*buffer = 0; // No access to CVAR_VM_NOREAD
	}
	else {
		Q_strncpyz( buffer, var->string, bufsize );
	}
}

void Cvar_VariableStringBuffer( const char *var_name, char *buffer, int bufsize ) {
	Cvar_VariableStringBuffer( var_name, buffer, bufsize, qfalse );
}


/*
============
Cvar_CommandCompletion
============
*/
void	Cvar_CommandCompletion( void(*callback)(const char *s) ) {
	cvar_t		*cvar;

	for ( cvar = cvar_vars ; cvar ; cvar = cvar->next ) {
		// Dont show internal cvars
		if ( cvar->flags & CVAR_INTERNAL )
		{
			continue;
		}
		callback( cvar->name );
	}
}


/*
============
Cvar_Get

If the variable already exists, the value will not be set unless CVAR_ROM
The flags will be or'ed in if the variable exists.
============
*/
cvar_t *Cvar_Get( const char *var_name, const char *var_value, int flags, qboolean isVmCall ) {
	cvar_t	*var;
	int	hash;

    if ( !var_name || ! var_value ) {
		Com_Error( ERR_FATAL, "Cvar_Get: NULL parameter" );
    }

	if ( !Cvar_ValidateString( var_name ) ) {
		Com_Printf("invalid cvar name string: %s\n", var_name );
		var_name = "BADNAME";
	}

#if 0		// FIXME: values with backslash happen
	if ( !Cvar_ValidateString( var_value ) ) {
		Com_Printf("invalid cvar value string: %s\n", var_value );
		var_value = "BADVALUE";
	}
#endif


	var = Cvar_FindVar (var_name);

	if ( !var && flags & CVAR_VM_NOWRITE )
	{
		assert(((flags & CVAR_ROM) || (flags & CVAR_INIT)));
	}

	if ( var ) {
		if ( isVmCall && var->flags & CVAR_VM_NOWRITE )
		{
			Com_DPrintf("Cvar_Get: attempt to re-create existing CVAR_VM_NOWRITE %s\n", var_name);
			return var; // Return the var. Other functions make sure the VM can't alter it
		}

		// if the C code is now specifying a variable that the user already
		// set a value for, take the new value as the reset value
		if ( ( var->flags & CVAR_USER_CREATED ) && !( flags & CVAR_USER_CREATED )
			&& var_value[0] ) {
			var->flags &= ~CVAR_USER_CREATED;
			Z_Free( (void *)var->resetString );
			var->resetString = CopyString( var_value );

			// ZOID--needs to be set so that cvars the game sets as
			// SERVERINFO get sent to clients
			cvar_modifiedFlags |= flags;
		}

		var->flags |= flags;
		// only allow one non-empty reset string without a warning
		if ( !var->resetString[0] ) {
			// we don't have a reset string yet
			Z_Free( (void *)var->resetString );
			var->resetString = CopyString( var_value );
		} else if ( var_value[0] && strcmp( var->resetString, var_value ) ) {
			Com_DPrintf( "Warning: cvar \"%s\" given initial values: \"%s\" and \"%s\"\n",
				var_name, var->resetString, var_value );
		}
		// if we have a latched string, take that value now
		if ( var->latchedString ) {
			const char *s;

			s = var->latchedString;
			var->latchedString = NULL;	// otherwise cvar_set2 would free it
			Cvar_Set2( var_name, s, qtrue );
			Z_Free( (void *)s );
		}

// use a CVAR_SET for rom sets, get won't override
#if 0
		// CVAR_ROM always overrides
		if ( flags & CVAR_ROM ) {
			Cvar_Set2( var_name, var_value, qtrue );
		}
#endif
		return var;
	}

	//
	// allocate a new cvar
	//
	if ( cvar_numIndexes >= MAX_CVARS ) {
		Com_Error( ERR_FATAL, "MAX_CVARS" );
	}
	var = &cvar_indexes[cvar_numIndexes];
	cvar_numIndexes++;
	var->name = CopyString (var_name);
	var->string = CopyString (var_value);
	var->modified = qtrue;
	var->modificationCount = 1;
#if defined (_MSC_VER) && (_MSC_VER < 1800)
	var->value = atof (var->string);
	var->integer = atoi(var->string);
#else
	double strValue = strtod(var->string, NULL);
	var->value = strValue;
	var->integer = strValue;
#endif
	var->resetString = CopyString( var_value );

	// link the variable in
	var->next = cvar_vars;
	cvar_vars = var;

	var->flags = flags;

	hash = generateHashValue(var_name);
	var->hashNext = hashTable[hash];
	hashTable[hash] = var;

	return var;
}
cvar_t *Cvar_Get( const char *var_name, const char *var_value, int flags ) {
	return Cvar_Get( var_name, var_value, flags, qfalse );
}

/*
============
Cvar_Set
============
*/
cvar_t *Cvar_Set2( const char *var_name, const char *value, qboolean force, qboolean isVmCall ) {
	cvar_t	*var;

	if ( !Cvar_ValidateString( var_name ) ) {
		Com_Printf("invalid cvar name string: %s\n", var_name );
		var_name = "BADNAME";
	}

#if 0	// FIXME
	if ( value && !Cvar_ValidateString( value ) ) {
		Com_Printf("invalid cvar value string: %s\n", value );
		var_value = "BADVALUE";
	}
#endif

	var = Cvar_FindVar (var_name);
	if (!var) {
		if ( !value ) {
			return NULL;
		}
		// create it
		if ( !force ) {
			return Cvar_Get( var_name, value, CVAR_USER_CREATED );
		} else {
			return Cvar_Get (var_name, value, 0);
		}
	}

	if ( isVmCall && var->flags & CVAR_VM_NOWRITE )
	{
		Com_DPrintf("Cvar_Set2: attempt to modify CVAR_VM_NOWRITE %s\n", var_name);
		return NULL; // We can return NULL here, cause the VM never gets the return value
	}

	// Dont display the update when its internal
	if ( !(var->flags & CVAR_INTERNAL) )
	{
		Com_DPrintf( "Cvar_Set2: %s %s\n", var_name, value );
	}

	if (!value ) {
		value = var->resetString;
	}

	if (!strcmp(value, var->string)) {

		if ( (var->flags & CVAR_LATCH) && var->latchedString ) {
			Com_Printf("Cvar %s is no longer latched to \"%s\".\n", var->name, var->latchedString);
			Z_Free ((void *)var->latchedString);
			var->latchedString = NULL;
			var->modified = qtrue;
			var->modificationCount++;
		}

		return var;
	}

	// note what types of cvars have been modified (userinfo, archive, serverinfo, systeminfo)
	cvar_modifiedFlags |= var->flags;

	if (!force)
	{
		if (var->flags & CVAR_ROM)
		{
			Com_Printf ("%s is read only.\n", var_name);
			return var;
		}

		if (var->flags & CVAR_INIT)
		{
			Com_Printf ("%s is write protected.\n", var_name);
			return var;
		}

		if (var->flags & CVAR_LATCH)
		{
			if (var->latchedString)
			{
				if (strcmp(value, var->latchedString) == 0)
				{
					Com_Printf("Cvar %s is already latched to \"%s\".\n", var->name, value);
					return var;
				}
				Z_Free ((void *)var->latchedString);
			}
			else
			{
				if (strcmp(value, var->string) == 0)
					return var;
			}

			Com_Printf ("%s will be changed to \"%s\" upon restarting.\n", var_name, value);
			var->latchedString = CopyString(value);
			var->modified = qtrue;
			var->modificationCount++;
			return var;
		}

		if ( (var->flags & CVAR_CHEAT) && !cvar_cheats->integer && !com_demoplaying ) //Allow modifying cheat cvars during demo playback.
		{
			Com_Printf ("%s is cheat protected.\n", var_name);
			return var;
		}

	}
	else
	{
		if (var->latchedString)
		{
			Z_Free ((void *)var->latchedString);
			var->latchedString = NULL;
		}
	}

	if (!strcmp(value, var->string))
		return var;		// not changed

	var->modified = qtrue;
	var->modificationCount++;

	Z_Free ((void *)var->string);	// free the old value string

	var->string = CopyString(value);
#if defined (_MSC_VER) && (_MSC_VER < 1800)
	var->value = atof (var->string);
	var->integer = atoi (var->string);
#else
	double strValue = strtod(var->string, NULL);
	var->value = strValue;
	var->integer = strValue;
#endif

	return var;
}

/*
============
Cvar_Set2
============
*/
cvar_t *Cvar_Set2( const char *var_name, const char *value, qboolean force )
{
	return Cvar_Set2( var_name, value, force, qfalse );
}

/*
============
Cvar_Set
============
*/
void Cvar_Set( const char *var_name, const char *value) {
	Cvar_Set2 (var_name, value, qtrue);
}

/*
============
Cvar_SetLatched
============
*/
void Cvar_SetLatched( const char *var_name, const char *value) {
	Cvar_Set2 (var_name, value, qfalse);
}

/*
============
Cvar_SetValue
============
*/
void Cvar_SetValue( const char *var_name, float value, qboolean isVmCall ) {
	char	val[32];

	if ( value == (int)value ) {
		Com_sprintf (val, sizeof(val), "%i",(int)value);
	} else {
		Com_sprintf (val, sizeof(val), "%f",value);
	}
	Cvar_Set2 (var_name, val, qtrue, isVmCall);
}
void Cvar_SetValue( const char *var_name, float value ) {
	Cvar_SetValue( var_name, value, qfalse );
}


/*
============
Cvar_Reset
============
*/
void Cvar_Reset( const char *var_name, qboolean isVmCall ) {
	Cvar_Set2( var_name, NULL, qfalse, isVmCall );
}
void Cvar_Reset( const char *var_name ) {
	Cvar_Reset( var_name, qfalse );
}


/*
============
Cvar_SetCheatState

Any testing variables will be reset to the safe values
============
*/
void Cvar_SetCheatState( void ) {
	cvar_t	*var;

	// set all default vars to the safe value
	for ( var = cvar_vars ; var ; var = var->next ) {
		if ( var->flags & CVAR_CHEAT ) {
			// the CVAR_LATCHED|CVAR_CHEAT vars might escape the reset here
			// because of a different var->latchedString
			if (var->latchedString)
			{
				Z_Free((void *)var->latchedString);
				var->latchedString = NULL;
			}
			if (strcmp(var->resetString,var->string)) {
				Cvar_Set( var->name, var->resetString );
			}
		}
	}
}

/*
============
Cvar_Command

Handles variable inspection and changing from the console
============
*/

void Cvar_Print (cvar_t *v) {
	Com_Printf ("\"%s\" is:\"%s" S_COLOR_WHITE "\" ", v->name, v->string);

	if (strcmp(v->string, v->resetString))
		Com_Printf("default:\"%s" S_COLOR_WHITE "\"\n", v->resetString );
	else
		Com_Printf("(the default)\n");

	if ( v->latchedString ) {
		Com_Printf( "latched: \"%s\"\n", v->latchedString );
	}
}

qboolean Cvar_Command( void ) {
	cvar_t			*v;

	// check variables
	v = Cvar_FindVar (Cmd_Argv(0));
	if (!v) {
		return qfalse;
	}

	// perform a variable print or set
	if ( Cmd_Argc() == 1 )
	{
/*		if (v->flags & CVAR_INTERNAL) // don't display
		{
			return qtrue;
		}
*/
		Cvar_Print( v );
		return qtrue;
	}

//JFM toggle test
	char *value;
	value = Cmd_Argv(1);

	if (!strcmp(value, "!")) //toggle
	{
		char buff[5];
		sprintf(buff,"%i",!v->value);
		Cvar_Set2 (v->name, buff, qfalse);// toggle the value
	}
	else
	{
		Cvar_Set2 (v->name, value, qfalse);// set the value if forcing isn't required
	}

	return qtrue;
}

typedef struct {
	int i;
	const char *s;
} intString_t;

static const intString_t cvarflags[] = {
	{CVAR_USER_CREATED, "USER CREATED"},
	{CVAR_ARCHIVE, "ARCHIVE"},
	{CVAR_LATCH, "LATCH"},
	{CVAR_USERINFO, "USERINFO"},
	{CVAR_SERVERINFO, "SERVERINFO"},
	{CVAR_SYSTEMINFO, "SYSTEMINFO"},
	{CVAR_INIT, "WRITE PROTECTED"},
	{CVAR_ROM, "READONLY"},
	{CVAR_CHEAT, "CHEAT"},
	{CVAR_NORESTART, "NO RESTART"},
	{CVAR_INTERNAL, "INTERNAL"},
	{CVAR_GLOBAL, "GLOBAL"},
};

static const size_t numCvarFlags = ARRAY_LEN( cvarflags );

static void Cvar_PrintFlags (cvar_t* cv) {
	char buf[1024] = {0};

	Com_sprintf(buf, sizeof(buf), "Flags: ");

	if ( !(cv->flags & ~CVAR_TEMP) )
		//discard CVAR_TEMP flag because it is useless /not used for anything
		Q_strcat(buf, sizeof(buf), "none");
	else {
		for (size_t i = 0; i < numCvarFlags; ++i) {
			if (cv->flags & cvarflags[i].i)
				Q_strcat(buf, sizeof(buf), va("%s, ", cvarflags[i].s));
		}

		size_t ln = strlen( buf );
		if (ln && buf[ln - 1] == ' ')
			buf[ln - 2] = 0;	//cut off ", "
	}
	Com_Printf("%s\n",buf);
}


/*
============
Cvar_Print_f

Print out the contents of a cvar
============
*/
static void Cvar_Print_f(void)
{
	char *name;
	cvar_t *cv;

	if(Cmd_Argc() != 2)
	{
		Com_Printf ("Usage: %s <cvar>\n", Cmd_Argv(0));
		return;
	}

	name = Cmd_Argv(1);

	cv = Cvar_FindVar(name);
	if(cv)
	{
		Cvar_Print(cv);
		Cvar_PrintFlags(cv);
	}
	else
		Com_Printf ("Cvar \"%s\" does not exist.\n", name);
}


/*
============
Cvar_Toggle_f

Toggles a cvar for easy single key binding
============
*/
void Cvar_Toggle_f( void ) {
	int		v;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf ("usage: toggle <variable>\n");
		return;
	}

	v = Cvar_VariableValue( Cmd_Argv( 1 ) );
	v = !v;

	Cvar_Set2 (Cmd_Argv(1), va("%i", v), qfalse);
}

/*
============
Cvar_Set_f

Allows setting and defining of arbitrary cvars from console, even if they
weren't declared in C code.
============
*/
void Cvar_Set_f( void ) {
	int		c;

	c = Cmd_Argc();
	if ( c < 3 ) {
		Com_Printf ("usage: set <variable> <value>\n");
		return;
	}

	Cvar_Set2 (Cmd_Argv(1), Cmd_ArgsFrom(2), qfalse);
}

/*
============
Cvar_SetU_f

As Cvar_Set, but also flags it as userinfo
============
*/
void Cvar_SetU_f( void ) {
	cvar_t	*v;

	if ( Cmd_Argc() != 3 ) {
		Com_Printf ("usage: setu <variable> <value>\n");
		return;
	}
	Cvar_Set_f();
	v = Cvar_FindVar( Cmd_Argv( 1 ) );
	if ( !v ) {
		return;
	}
	v->flags |= CVAR_USERINFO;
}

/*
============
Cvar_SetS_f

As Cvar_Set, but also flags it as userinfo
============
*/
void Cvar_SetS_f( void ) {
	cvar_t	*v;

	if ( Cmd_Argc() != 3 ) {
		Com_Printf ("usage: sets <variable> <value>\n");
		return;
	}
	Cvar_Set_f();
	v = Cvar_FindVar( Cmd_Argv( 1 ) );
	if ( !v ) {
		return;
	}
	v->flags |= CVAR_SERVERINFO;
}

/*
============
Cvar_SetA_f

As Cvar_Set, but also flags it as archived
============
*/
void Cvar_SetA_f( void ) {
	cvar_t	*v;

	if ( Cmd_Argc() != 3 ) {
		Com_Printf ("usage: seta <variable> <value>\n");
		return;
	}
	Cvar_Set_f();
	v = Cvar_FindVar( Cmd_Argv( 1 ) );
	if ( !v ) {
		return;
	}
	v->flags |= CVAR_ARCHIVE;
}

/*
============
Cvar_Reset_f
============
*/
void Cvar_Reset_f( void ) {
	if ( Cmd_Argc() != 2 ) {
		Com_Printf ("usage: reset <variable>\n");
		return;
	}
	Cvar_Reset( Cmd_Argv( 1 ) );
}

/*
============
Cvar_WriteVariables

Appends lines containing "set variable value" for all variables
with the archive flag set to qtrue.
============
*/

static int Cvar_CvarCmp(const void *p1, const void *p2) {
    const cvar_t * const *e1 = (const cvar_t * const *)p1;
    const cvar_t * const *e2 = (const cvar_t * const *)p2;

	return strcmp( (*e1)->name, (*e2)->name );
}

void Cvar_WriteVariables( fileHandle_t f, qboolean locals ) {
	cvar_t	*var;
	char	buffer[1024];
	cvar_t *sortedCvars[MAX_CVARS];

	int i;
	int numSorted = 0;
	for (var = cvar_vars ; var ; var = var->next) {
		if((var->flags & CVAR_ARCHIVE) &&
				( ( locals && !(var->flags & CVAR_GLOBAL) ) || ( !locals && (var->flags & CVAR_GLOBAL) ) ) ) {
			sortedCvars[numSorted++] = var;
		}
	}

	if (!numSorted)
		return;

	qsort(sortedCvars, numSorted, sizeof(sortedCvars[0]), Cvar_CvarCmp);

	for (i = 0; i < numSorted ; ++i) {
		var = sortedCvars[i];

		// write the latched value, even if it hasn't taken effect yet
		if ( var->latchedString ) {
			Com_sprintf (buffer, sizeof(buffer), "seta %s \"%s\"\n", var->name, var->latchedString);
		} else {
			Com_sprintf (buffer, sizeof(buffer), "seta %s \"%s\"\n", var->name, var->string);
		}
		FS_Printf (f, "%s", buffer);
	}
}

/*
============
Cvar_List_f
============
*/
void Cvar_List_f( void ) {
	cvar_t	*var;
	int		i;
	char	*match;

	if ( Cmd_Argc() > 1 ) {
		match = Cmd_Argv( 1 );
	} else {
		match = NULL;
	}

	cvar_t *sortedCvars[MAX_CVARS];

	int numSorted = 0;
	for (var = cvar_vars ; var ; var = var->next) {
		// Dont show internal cvars
		if ( var->flags & CVAR_INTERNAL )
			continue;

		if (match && !Com_Filter(match, var->name, qfalse))
			continue;

		sortedCvars[numSorted++] = var;
	}

	if (!numSorted)
		return;

	qsort(sortedCvars, numSorted, sizeof(sortedCvars[0]), Cvar_CvarCmp);

	for (i = 0; i < numSorted; ++i)
	{
		var = sortedCvars[i];

		if (var->flags & CVAR_SERVERINFO) {
			Com_Printf("S");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_USERINFO) {
			Com_Printf("U");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_ROM) {
			Com_Printf("R");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_INIT) {
			Com_Printf("I");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_ARCHIVE) {
			Com_Printf("A");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_LATCH) {
			Com_Printf("L");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_CHEAT) {
			Com_Printf("C");
		} else {
			Com_Printf(" ");
		}

		Com_Printf (" %s \"%s\"\n", var->name, var->string);
	}

	Com_Printf ("\n%i total cvars\n", i);
	Com_Printf ("%i cvar indexes\n", cvar_numIndexes);
}

/*
============
Cvar_Restart_f

Resets all cvars to their hardcoded values
============
*/
void Cvar_Restart_f( void ) {
	cvar_t	*var;
	cvar_t	**prev;

	prev = &cvar_vars;
	while ( 1 ) {
		var = *prev;
		if ( !var ) {
			break;
		}

		// don't mess with rom values, or some inter-module
		// communication will get broken (com_cl_running, etc)
		if ( var->flags & ( CVAR_ROM | CVAR_INIT | CVAR_NORESTART ) ) {
			prev = &var->next;
			continue;
		}

		// throw out any variables the user created
		if ( var->flags & CVAR_USER_CREATED ) {
			*prev = var->next;
			if ( var->name ) {
				Z_Free( (void *)var->name );
			}
			if ( var->string ) {
				Z_Free( (void *)var->string );
			}
			if ( var->latchedString ) {
				Z_Free( (void *)var->latchedString );
			}
			if ( var->resetString ) {
				Z_Free( (void *)var->resetString );
			}
			// clear the var completely, since we
			// can't remove the index from the list
			Com_Memset( var, 0, sizeof( var ) );
			continue;
		}

		Cvar_Set( var->name, var->resetString );

		prev = &var->next;
	}
}



/*
=====================
Cvar_InfoString
=====================
*/
char	*Cvar_InfoString( int bit, qboolean isVmCall ) {
	static char	info[MAX_INFO_STRING];
	cvar_t	*var;

	info[0] = 0;

	for (var = cvar_vars ; var ; var = var->next) {
		if (var->flags & bit && !(isVmCall && var->flags & CVAR_VM_NOREAD)) {
			Info_SetValueForKey (info, var->name, var->string);
		}
	}
	return info;
}
char	*Cvar_InfoString( int bit ) {
	return Cvar_InfoString( bit, qfalse );
}

/*
=====================
Cvar_InfoString_Big

  handles large info strings ( CS_SYSTEMINFO )
=====================
*/
char	*Cvar_InfoString_Big( int bit ) {
	static char	info[BIG_INFO_STRING];
	cvar_t	*var;

	info[0] = 0;

	for (var = cvar_vars ; var ; var = var->next) {
		if (var->flags & bit) {
			Info_SetValueForKey_Big (info, var->name, var->string);
		}
	}
	return info;
}



/*
=====================
Cvar_InfoStringBuffer
=====================
*/
void Cvar_InfoStringBuffer( int bit, char* buff, int buffsize, qboolean isVmCall ) {
	Q_strncpyz(buff,Cvar_InfoString(bit),buffsize);
}
void Cvar_InfoStringBuffer( int bit, char* buff, int buffsize ) {
	Cvar_InfoStringBuffer( bit, buff, buffsize, qfalse );
}

/*
=====================
Cvar_Register

basically a slightly modified Cvar_Get for the interpreted modules
=====================
*/
void	Cvar_Register( vmCvar_t *vmCvar, const char *varName, const char *defaultValue, int flags ) {
	cvar_t	*cv;

	// Warn if the VM tries to register a CVAR_VM_* cvar.
	cv = Cvar_FindVar(varName);
	if ( cv )
	{
		if ( cv->flags & CVAR_VM_NOREAD ) Com_DPrintf("Cvar_Register: attempt to register CVAR_VM_NOREAD %s\n", varName);
		if ( cv->flags & CVAR_VM_NOWRITE ) Com_DPrintf("Cvar_Register: attempt to register CVAR_VM_NOWRITE %s\n", varName);
	}

	// The VM should never set CVAR_VM_* flags itself
	flags &= ~CVAR_VM_NOREAD;
	flags &= ~CVAR_VM_NOWRITE;

	cv = Cvar_Get( varName, defaultValue, flags, qtrue );
	if ( !vmCvar ) {
		return;
	}
	vmCvar->handle = cv - cvar_indexes;
	vmCvar->modificationCount = -1;
	Cvar_Update( vmCvar );
}


/*
=====================
Cvar_Register

updates an interpreted modules' version of a cvar
=====================
*/
void	Cvar_Update( vmCvar_t *vmCvar ) {
	cvar_t	*cv = NULL; // bk001129
	assert(vmCvar); // bk

	if ( (unsigned)vmCvar->handle >= (unsigned)cvar_numIndexes ) {
		Com_Error( ERR_DROP, "Cvar_Update: handle out of range" );
	}

	cv = cvar_indexes + vmCvar->handle;

	if ( cv->flags & CVAR_VM_NOREAD ) return; // Never update vmCvar_t for CVAR_VM_NOREAD

	if ( cv->modificationCount == vmCvar->modificationCount ) {
		return;
	}
	if ( !cv->string ) {
		return;		// variable might have been cleared by a cvar_restart
	}
	vmCvar->modificationCount = cv->modificationCount;
	// bk001129 - mismatches.
	if ( strlen(cv->string)+1 > MAX_CVAR_VALUE_STRING )
	  Com_Error( ERR_DROP, "Cvar_Update: src %s length %d exceeds MAX_CVAR_VALUE_STRING",
		  cv->string,
		  (int)strlen(cv->string) );
	// bk001212 - Q_strncpyz guarantees zero padding and dest[MAX_CVAR_VALUE_STRING-1]==0
	// bk001129 - paranoia. Never trust the destination string.
	// bk001129 - beware, sizeof(char*) is always 4 (for cv->string).
	//			sizeof(vmCvar->string) always MAX_CVAR_VALUE_STRING
	//Q_strncpyz( vmCvar->string, cv->string, sizeof( vmCvar->string ) ); // id
	Q_strncpyz( vmCvar->string, cv->string,  MAX_CVAR_VALUE_STRING );

	vmCvar->value = cv->value;
	vmCvar->integer = cv->integer;
}


/*
==================
Cvar_CompleteCvarName
==================
*/
void Cvar_CompleteCvarName( char *args, int argNum )
{ // for auto-complete (copied from OpenJK)
	if( argNum == 2 )
	{
		// Skip "<cmd> "
		char *p = Com_SkipTokens( args, 1, " " );

		if( p > args )
			Field_CompleteCommand( p, qfalse, qtrue, qtrue );
	}
}

/*
============
Cvar_Init

Reads in all archived cvars
============
*/
void Cvar_Init (void) {
	cvar_cheats = Cvar_Get("sv_cheats", "0", CVAR_ROM | CVAR_SYSTEMINFO );

	Cmd_AddCommand ("print", Cvar_Print_f );
	Cmd_SetCommandCompletionFunc( "print", Cvar_CompleteCvarName );

	Cmd_AddCommand ("toggle", Cvar_Toggle_f);
	Cmd_SetCommandCompletionFunc( "toggle", Cvar_CompleteCvarName );
	Cmd_AddCommand ("set", Cvar_Set_f);
	Cmd_SetCommandCompletionFunc( "set", Cvar_CompleteCvarName );
	Cmd_AddCommand ("sets", Cvar_SetS_f);
	Cmd_SetCommandCompletionFunc( "sets", Cvar_CompleteCvarName );
	Cmd_AddCommand ("setu", Cvar_SetU_f);
	Cmd_SetCommandCompletionFunc( "setu", Cvar_CompleteCvarName );
	Cmd_AddCommand ("seta", Cvar_SetA_f);
	Cmd_SetCommandCompletionFunc( "seta", Cvar_CompleteCvarName );
	Cmd_AddCommand ("reset", Cvar_Reset_f);
	Cmd_SetCommandCompletionFunc( "reset", Cvar_CompleteCvarName );
	Cmd_AddCommand ("cvarlist", Cvar_List_f);
	Cmd_AddCommand ("cvar_restart", Cvar_Restart_f);
}
