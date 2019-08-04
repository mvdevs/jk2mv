// console.c

#include "client.h"
#include "../strings/con_text.h"
#include "../qcommon/strip.h"
#include <mv_setup.h>


console_t	con;

cvar_t		*con_height;
cvar_t		*con_notifytime;
cvar_t		*con_scale;
cvar_t		*con_speed;
cvar_t		*con_timestamps;

#define	DEFAULT_CONSOLE_WIDTH	78
#define CON_BLANK_CHAR			' '
#define CON_SCROLL_L_CHAR		'$'
#define CON_SCROLL_R_CHAR		'$'
#define CON_TIMESTAMP_LEN		11 // "[13:37:00] "
#define CON_MIN_WIDTH			20

static const conChar_t CON_WRAP = { { ColorIndex_Extended(COLOR_LT_TRANSPARENT), '\\' } };
static const conChar_t CON_BLANK = { { ColorIndex(COLOR_WHITE), CON_BLANK_CHAR } };

vec4_t	console_color = {1.0, 1.0, 1.0, 1.0};

/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f (void) {
	// closing a full screen console restarts the demo loop
	if ( cls.state == CA_DISCONNECTED && cls.keyCatchers == KEYCATCH_CONSOLE ) {
		CL_StartDemoLoop();
		return;
	}

	Field_Clear( &kg.g_consoleField );

	Con_ClearNotify ();
	cls.keyCatchers ^= KEYCATCH_CONSOLE;
}

/*
================
Con_MessageMode_f
================
*/
void Con_MessageMode_f (void) {	//yell
	chat_playerNum = -1;
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;

	cls.keyCatchers ^= KEYCATCH_MESSAGE;
}

/*
================
Con_MessageMode2_f
================
*/
void Con_MessageMode2_f (void) {	//team chat
	chat_playerNum = -1;
	chat_team = qtrue;
	Field_Clear( &chatField );
	chatField.widthInChars = 25;
	cls.keyCatchers ^= KEYCATCH_MESSAGE;
}

/*
================
Con_MessageMode3_f
================
*/
void Con_MessageMode3_f (void) {		//target chat
	chat_playerNum = VM_Call( cgvm, CG_CROSSHAIR_PLAYER );
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;
	cls.keyCatchers ^= KEYCATCH_MESSAGE;
}

/*
================
Con_MessageMode4_f
================
*/
void Con_MessageMode4_f (void) {	//attacker
	chat_playerNum = VM_Call( cgvm, CG_LAST_ATTACKER );
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;
	cls.keyCatchers ^= KEYCATCH_MESSAGE;
}

/*
================
Con_Clear_f
================
*/
void Con_Clear_f (void) {
	int		i;

	for ( i = 0 ; i < CON_TEXTSIZE ; i++ ) {
		con.text[i] = CON_BLANK;
	}

	Con_Bottom();		// go to end
}


/*
================
Con_Dump_f

Save the console contents out to a file
================
*/
void Con_Dump_f (void)
{
	char			fileName[MAX_QPATH];
	qboolean		empty;
	int				l, i, j;
	int				line;
	int				lineLen;
	fileHandle_t	f;
	char			buffer[CON_TIMESTAMP_LEN + MAXPRINTMSG + 1];

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("%s\n", SP_GetStringText(CON_TEXT_DUMP_USAGE));
		return;
	}

	Q_strncpyz( fileName, Cmd_Argv( 1 ), sizeof( fileName ) );
	COM_SanitizeExtension( fileName, sizeof( fileName ), ".txt" );

	f = FS_FOpenFileWrite( fileName );
	if (!f)
	{
		Com_Printf (S_COLOR_RED"ERROR: couldn't open %s.\n", fileName);
		return;
	}

	// skip empty lines
	for (l = 1, empty = qtrue ; l < con.totallines && empty ; l++)
	{
		line = ((con.current + l) % con.totallines) * con.rowwidth;

		for (j = CON_TIMESTAMP_LEN ; j < con.rowwidth - 1 ; j++)
			if (con.text[line + j].f.character != CON_BLANK_CHAR)
				empty = qfalse;
	}

	for ( ; l < con.totallines ; l++)
	{
		lineLen = 0;
		i = 0;

		// Print timestamp
		if (con_timestamps->integer) {
			line = ((con.current + l) % con.totallines) * con.rowwidth;

			for (i = 0; i < CON_TIMESTAMP_LEN; i++)
				buffer[i] = con.text[line + i].f.character;

			lineLen = CON_TIMESTAMP_LEN;
		}

		// Concatenate wrapped lines
		for ( ; l < con.totallines ; l++)
		{
			line = ((con.current + l) % con.totallines) * con.rowwidth;

			for (j = CON_TIMESTAMP_LEN; j < con.rowwidth - 1 && i < (int)sizeof(buffer) - 1; j++, i++) {
				buffer[i] = con.text[line + j].f.character;

				if (con.text[line + j].f.character != CON_BLANK_CHAR)
					lineLen = i + 1;
			}

			if (i == sizeof(buffer) - 1)
				break;

			if (con.text[line + j].compare != CON_WRAP.compare)
				break;
		}

		buffer[lineLen] = '\n';
		FS_Write(buffer, lineLen + 1, f);
	}

	FS_FCloseFile( f );

	Com_Printf ("Dumped console text to %s.\n", fileName );
}


/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify( void ) {
	int		i;

	for ( i = 0 ; i < NUM_CON_TIMES ; i++ ) {
		con.times[i] = 0;
	}
}

/*
================
Con_Initialize

Initialize console for the first time.
================
*/
void Con_Initialize(void)
{
	int	i;

	Vector4Copy(colorWhite, con.color);
	con.charWidth = SMALLCHAR_WIDTH;
	con.charHeight = SMALLCHAR_HEIGHT;
	con.linewidth = DEFAULT_CONSOLE_WIDTH;
	con.rowwidth = CON_TIMESTAMP_LEN + con.linewidth + 1;
	con.totallines = CON_TEXTSIZE / con.rowwidth;
	con.current = con.totallines - 1;
	con.display = con.current;
	for(i=0; i<CON_TEXTSIZE; i++)
	{
		con.text[i] = CON_BLANK;
	}

	con.initialized = qtrue;
}

/*
================
Con_Resize

Reformat the buffer for new row width
================
*/
static void Con_Resize(int rowwidth)
{
	static conChar_t tbuf[CON_TEXTSIZE];
	int		i, j;
	int		oldrowwidth;
	int		oldtotallines;

	oldrowwidth = con.rowwidth;
	oldtotallines = con.totallines;

	con.rowwidth = rowwidth;
	con.totallines = CON_TEXTSIZE / rowwidth;

	Com_Memcpy (tbuf, con.text, sizeof(tbuf));
	for(i=0; i<CON_TEXTSIZE; i++)
		con.text[i] = CON_BLANK;

	int oi = 0;
	int ni = 0;

	while (oi < oldtotallines)
		{
			conChar_t	line[MAXPRINTMSG];
			conChar_t	timestamp[CON_TIMESTAMP_LEN];
			int		lineLen = 0;
			int		oldline = ((con.current + oi) % oldtotallines) * oldrowwidth;
			int		newline = (ni % con.totallines) * con.rowwidth;

			// Store timestamp
			for (i = 0; i < CON_TIMESTAMP_LEN; i++)
				timestamp[i] = tbuf[oldline + i];

			// Store whole line concatenating on CON_WRAP
			for (i = 0; oi < oldtotallines; oi++)
				{
					oldline = ((con.current + oi) % oldtotallines) * oldrowwidth;

					for (j = CON_TIMESTAMP_LEN; j < oldrowwidth - 1 && i < (int)ARRAY_LEN(line); j++, i++) {
						line[i] = tbuf[oldline + j];

						if (line[i].f.character != CON_BLANK_CHAR)
							lineLen = i + 1;
					}

					if (i == ARRAY_LEN(line))
						break;

					if (tbuf[oldline + j].compare != CON_WRAP.compare)
						break;
				}

			oi++;

			// Print stored line to a new text buffer
			for (i = 0; ; ni++) {
				newline = (ni % con.totallines) * con.rowwidth;

				// Print timestamp at the begining of each line
				for (j = 0; j < CON_TIMESTAMP_LEN; j++)
					con.text[newline + j] = timestamp[j];

				for (j = CON_TIMESTAMP_LEN; j < con.rowwidth - 1 && i < lineLen; j++, i++)
					con.text[newline + j] = line[i];

				if (i == lineLen) {
					// Erase remaining chars in case newline wrapped
					for (; j < con.rowwidth - 1; j++)
						con.text[newline + j] = CON_BLANK;

					ni++;
					break;
				}

				con.text[newline + j] = CON_WRAP;
			}
		}

	con.current = ni;

	// Erase con.current line for next CL_ConsolePrint
	int newline = (con.current % con.totallines) * con.rowwidth;
	for (j = 0; j < con.rowwidth; j++)
		con.text[newline + j] = CON_BLANK;

	Con_ClearNotify ();

	con.display = con.current;
}

/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize (void)
{
	int		charWidth, rowwidth, width;
	float	scale;

	assert(SMALLCHAR_HEIGHT >= SMALLCHAR_WIDTH);

	scale = cls.glconfig.displayDPI / 96.0f *
		((con_scale->value > 0.0f) ? con_scale->value : 1.0f);
	charWidth = scale * SMALLCHAR_WIDTH;

	if (charWidth < 1) {
		charWidth = 1;
		scale = (float)charWidth / SMALLCHAR_WIDTH;
	}

	width = (cls.glconfig.vidWidth / charWidth) - 2;

	if (width < 20) {
		width = 20;
		charWidth = cls.glconfig.vidWidth / 22;
		scale = (float)charWidth / SMALLCHAR_WIDTH;
	}

	if (charWidth < 1) {
		Com_Error(ERR_FATAL, "Con_CheckResize: Window too small to draw a console");
	}

	rowwidth = width + 1 + CON_TIMESTAMP_LEN;

	con.charWidth = charWidth;
	con.charHeight = scale * SMALLCHAR_HEIGHT;
	con.linewidth = width - (con_timestamps->integer ? 0 : CON_TIMESTAMP_LEN);
	kg.g_consoleField.widthInChars = width - 1; // Command prompt

	if (con.rowwidth != rowwidth)
	{
		Con_Resize(rowwidth);
	}
}

/*
================
Con_Init
================
*/
void Con_Init (void) {
	con_height = Cvar_Get ("con_height", "0.5", CVAR_GLOBAL | CVAR_ARCHIVE);
	con_notifytime = Cvar_Get ("con_notifytime", "3", CVAR_GLOBAL | CVAR_ARCHIVE);
	con_speed = Cvar_Get ("con_speed", "3", CVAR_GLOBAL | CVAR_ARCHIVE);
	con_scale = Cvar_Get ("con_scale", "1", CVAR_GLOBAL | CVAR_ARCHIVE);
	con_timestamps = Cvar_Get ("con_timestamps", "0", CVAR_GLOBAL | CVAR_ARCHIVE);

	Field_Clear( &kg.g_consoleField );
	kg.g_consoleField.widthInChars = DEFAULT_CONSOLE_WIDTH - 1; // Command prompt

	Cmd_AddCommand ("toggleconsole", Con_ToggleConsole_f);
	Cmd_AddCommand ("messagemode", Con_MessageMode_f);
	Cmd_AddCommand ("messagemode2", Con_MessageMode2_f);
	Cmd_AddCommand ("messagemode3", Con_MessageMode3_f);
	Cmd_AddCommand ("messagemode4", Con_MessageMode4_f);
	Cmd_AddCommand ("clear", Con_Clear_f);
	Cmd_AddCommand ("condump", Con_Dump_f);
	Cmd_SetCommandCompletionFunc( "condump", Cmd_CompleteTxtName );

	//Initialize values on first print
	con.initialized = qfalse;
}


/*
===============
Con_Linefeed
===============
*/
void Con_Linefeed (void)
{
	int		i;
	int		line = (con.current % con.totallines) * con.rowwidth;

	// print timestamp on the PREVIOUS line
	{
		qtime_t	time;
		char	timestamp[CON_TIMESTAMP_LEN + 1];
		const unsigned char color = ColorIndex_Extended(COLOR_LT_TRANSPARENT);

		Com_RealTime(&time);
		Com_sprintf(timestamp, sizeof(timestamp), "[%02d:%02d:%02d] ",
			time.tm_hour, time.tm_min, time.tm_sec);

		for ( i = 0; i < CON_TIMESTAMP_LEN; i++ ) {
			con.text[line + i].f = { color, timestamp[i] };
		}
	}

	// mark time for transparent overlay
	if (con.current >= 0)
		con.times[con.current % NUM_CON_TIMES] = cls.realtime;

	con.x = 0;

	if (con.display == con.current)
		con.display++;
	con.current++;

	line = (con.current % con.totallines) * con.rowwidth;

	for ( i = 0; i < con.rowwidth; i++ )
		con.text[line + i] = CON_BLANK;
}

/*
================
CL_ConsolePrint

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the text will appear at the top of the game window
================
*/
void CL_ConsolePrint( const char *txt, qboolean extendedColors ) {
	unsigned char	color;
	char			c;
	int				y;

	// for some demos we don't want to ever show anything on the console
	if ( cl_noprint && cl_noprint->integer ) {
		return;
	}

	if (!con.initialized) {
		Con_Initialize();
	}

	const bool use102color = MV_USE102COLOR;

	color = ColorIndex(COLOR_WHITE);

	while ( (c = *txt) != 0 ) {
		if ( Q_IsColorString( txt ) ||
			(extendedColors && Q_IsColorString_Extended( txt )) ||
			( use102color && Q_IsColorString_1_02( txt ) ) )
		{
			if (extendedColors) color = ColorIndex_Extended( *(txt+1) );
			else color = ColorIndex( *(txt+1) );
			txt += 2;
			continue;
		}

		txt++;

		switch (c)
		{
		case '\n':
			Con_Linefeed ();
			break;
		case '\r':
			con.x = 0;
			break;
		default:	// display character and advance
			y = con.current % con.totallines;

			if (con.x == con.rowwidth - CON_TIMESTAMP_LEN - 1) {
				con.text[y * con.rowwidth + CON_TIMESTAMP_LEN + con.x] = CON_WRAP;
				Con_Linefeed();
				y = con.current % con.totallines;
			}

			con.text[y * con.rowwidth + CON_TIMESTAMP_LEN + con.x].f = { color, c };
			con.x++;
			break;
		}
	}


	// mark time for transparent overlay

	if (con.current >= 0)
		con.times[con.current % NUM_CON_TIMES] = cls.realtime;
}


/*
==============================================================================

DRAWING

==============================================================================
*/


/*
================
Con_DrawInput

Draw the editline after a ] prompt
================
*/
void Con_DrawInput (void) {
	int		y;

	if ( cls.state != CA_DISCONNECTED && !(cls.keyCatchers & KEYCATCH_CONSOLE ) ) {
		return;
	}

	y = con.vislines - ( con.charHeight * (re.Language_IsAsian() ? 1.5 : 2) );

	re.SetColor( con.color );

	Field_Draw( &kg.g_consoleField, 2 * con.charWidth, y, qtrue );

	SCR_DrawSmallChar( con.charWidth, y, CONSOLE_PROMPT_CHAR );

	re.SetColor( g_color_table[ColorIndex_Extended(COLOR_LT_TRANSPARENT)] );

	if ( kg.g_consoleField.scroll > 0 )
		SCR_DrawSmallChar( 0, y, CON_SCROLL_L_CHAR );

	int len = Q_PrintStrlen( kg.g_consoleField.buffer, MV_USE102COLOR );
	int pos = Q_PrintStrLenTo( kg.g_consoleField.buffer, kg.g_consoleField.scroll, NULL, MV_USE102COLOR);
	if ( pos + kg.g_consoleField.widthInChars < len )
		SCR_DrawSmallChar( cls.glconfig.vidWidth - con.charWidth, y, CON_SCROLL_R_CHAR );
}




/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
void Con_DrawNotify (void)
{
	unsigned char	currentColor;
	conChar_t		*text;
	int		x, v;
	int		i;
	int		time;
	int		skip;

	currentColor = 7;
	re.SetColor( g_color_table[currentColor] );

	static int iFontIndex = re.RegisterFont("ocr_a");
	float fFontScale = 1.0f;
	int iPixelHeightToAdvance = 0;
	if (re.Language_IsAsian())
	{
		fFontScale = con.charWidth * 10.0f /
			re.Font_StrLenPixels("aaaaaaaaaa", iFontIndex, 1.0f, cls.xadjust, cls.yadjust);
		iPixelHeightToAdvance = 1.3 * re.Font_HeightPixels(iFontIndex, fFontScale, cls.xadjust, cls.yadjust);
	}

	v = 0;
	for (i= con.current-NUM_CON_TIMES+1 ; i<=con.current ; i++)
	{
		if (i < 0)
			continue;
		time = con.times[i % NUM_CON_TIMES];
		if (time == 0)
			continue;
		time = cls.realtime - time;
		if (time >= con_notifytime->value*1000)
			continue;
		text = con.text + (i % con.totallines)*con.rowwidth;
		if (!con_timestamps->integer)
			text += CON_TIMESTAMP_LEN;

		if (cl.snap.ps.pm_type != PM_INTERMISSION && cls.keyCatchers & (KEYCATCH_UI | KEYCATCH_CGAME) ) {
			continue;
		}


		if (!cl_conXOffset)
		{
			cl_conXOffset = Cvar_Get ("cl_conXOffset", "0", 0);
		}

		// asian language needs to use the new font system to print glyphs...
		//
		// (ignore colours since we're going to print the whole thing as one string)
		//
		if (re.Language_IsAsian())
		{
			// concat the text to be printed...
			//
			char sTemp[4096];	// ott
			sTemp[0] = '\0';
			for (x = 0 ; x < con.linewidth ; x++)
			{
				if ( text[x].f.color != currentColor ) {
					currentColor = text[x].f.color;
					strcat(sTemp,va("^%i", (currentColor > 7 ? COLOR_JK2MV_FALLBACK : currentColor) ));
				}
				strcat(sTemp,va("%c",text[x].f.character));
			}
			//
			// and print...
			//
			re.Font_DrawString(cl_conXOffset->integer + con.charWidth, v, sTemp,
				g_color_table[currentColor], iFontIndex, -1, fFontScale, cls.xadjust, cls.yadjust);

			v +=  iPixelHeightToAdvance;
		}
		else
		{
			for (x = 0 ; x < con.linewidth ; x++) {
				if ( text[x].f.character == ' ' ) {
					continue;
				}
				if ( text[x].f.color != currentColor ) {
					currentColor = text[x].f.color;
					re.SetColor( g_color_table[currentColor] );
				}
				if (!cl_conXOffset)
				{
					cl_conXOffset = Cvar_Get ("cl_conXOffset", "0", 0);
				}
				SCR_DrawSmallChar( (int)(cl_conXOffset->integer + (x+1)*con.charWidth), v, text[x].f.character );
			}

			v += con.charHeight;
		}
	}

	re.SetColor( NULL );

	if (cls.keyCatchers & (KEYCATCH_UI | KEYCATCH_CGAME) ) {
		return;
	}

	// draw the chat line
	if ( cls.keyCatchers & KEYCATCH_MESSAGE )
	{
		if (chat_team)
		{
			SCR_DrawBigString (8, v, "say_team:", 1.0f );
			skip = 11;
		}
		else
		{
			SCR_DrawBigString (8, v, "say:", 1.0f );
			skip = 5;
		}

		Field_BigDraw( &chatField, skip * BIGCHAR_WIDTH, v, qtrue );

		v += BIGCHAR_HEIGHT;
	}

}

/*
================
Con_DrawSolidConsole

Draws the console with the solid background
================
*/
void Con_DrawSolidConsole( float frac ) {
	unsigned char	currentColor;
	conChar_t		*text;
	int				i, x, y;
	int				rows;
	int				row;
	int				lines;
//	qhandle_t		conShader;
	char *vertext;

	lines = (int) (cls.glconfig.vidHeight * frac);
	if (lines <= 0)
		return;

	if (lines > cls.glconfig.vidHeight )
		lines = cls.glconfig.vidHeight;

	// draw the background
	y = (int) (frac * SCREEN_HEIGHT - 2);
	if ( y < 1 ) {
		y = 0;
	}
	else {
		SCR_DrawPic( 0, 0, SCREEN_WIDTH, (float) y, cls.consoleShader );
	}

	// draw the bottom bar and version number
	re.SetColor( g_color_table[ColorIndex_Extended(COLOR_JK2MV)] );
	SCR_DrawPic( 0, y, SCREEN_WIDTH, 2, cls.whiteShader );


	vertext = Q3_VERSION;
	i = (int)strlen(vertext);
	for (x=0 ; x<i ; x++) {
		SCR_DrawSmallChar( cls.glconfig.vidWidth - ( i - x ) * con.charWidth,
			(lines-(con.charHeight+con.charHeight/2)), vertext[x] );
	}


	// draw the text
	con.vislines = lines;
	rows = (lines-con.charHeight)/con.charHeight;		// rows of text to draw

	y = lines - (con.charHeight*3);

	// draw from the bottom up
	if (con.display != con.current)
	{
		// draw arrows to show the buffer is backscrolled
		re.SetColor( g_color_table[ColorIndex(COLOR_RED)] );
		for (x=0 ; x<con.linewidth ; x+=4)
			SCR_DrawSmallChar( (x+1)*con.charWidth, y, '^' );
		y -= con.charHeight;
		rows--;
	}

	row = con.display;

	if ( con.x == 0 ) {
		row--;
	}

	currentColor = 7;
	re.SetColor( g_color_table[currentColor] );

	static int iFontIndex = re.RegisterFont("ocr_a");
	float fFontScale = 1.0f;
	int iPixelHeightToAdvance = con.charHeight;
	if (re.Language_IsAsian())
	{
		fFontScale = con.charWidth * 10.0f /
			re.Font_StrLenPixels("aaaaaaaaaa", iFontIndex, 1.0f, cls.xadjust, cls.yadjust);
		iPixelHeightToAdvance = 1.3 * re.Font_HeightPixels(iFontIndex, fFontScale, cls.xadjust, cls.yadjust);
	}

	for (i=0 ; i<rows ; i++, y -= iPixelHeightToAdvance, row--)
	{
		if (row < 0)
			break;
		if (con.current - row >= con.totallines) {
			// past scrollback wrap point
			continue;
		}

		text = con.text + (row % con.totallines)*con.rowwidth;
		if (!con_timestamps->integer)
			text += CON_TIMESTAMP_LEN;

		// asian language needs to use the new font system to print glyphs...
		//
		// (ignore colours since we're going to print the whole thing as one string)
		//
		if (re.Language_IsAsian())
		{
			// concat the text to be printed...
			//
			char sTemp[4096];	// ott
			sTemp[0] = '\0';
			for (x = 0 ; x < con.linewidth + 1 ; x++)
			{
				if ( text[x].f.color != currentColor ) {
					currentColor = text[x].f.color;
					strcat(sTemp,va("^%i", (currentColor > 7 ? COLOR_JK2MV_FALLBACK : currentColor) ));
				}
				strcat(sTemp,va("%c",text[x].f.character));
			}
			//
			// and print...
			//
			re.Font_DrawString(con.charWidth, y, sTemp, g_color_table[currentColor],
				iFontIndex, -1, fFontScale, cls.xadjust, cls.yadjust);
		}
		else
		{
			for (x = 0; x < con.linewidth + 1 ; x++) {
				if ( text[x].f.character == ' ' ) {
					continue;
				}

				if ( text[x].f.color != currentColor ) {
					currentColor = text[x].f.color;
					re.SetColor( g_color_table[currentColor] );
				}
				SCR_DrawSmallChar( (x+1)*con.charWidth, y, text[x].f.character );
			}
		}
	}

	// draw the input prompt, user text, and cursor if desired
	Con_DrawInput ();

	re.SetColor( NULL );
}



/*
==================
Con_DrawConsole
==================
*/
void Con_DrawConsole( void ) {
	// check for console width changes from a vid mode change
	Con_CheckResize ();

	// if disconnected, render console full screen
	if ( cls.state == CA_DISCONNECTED ) {
		if ( !( cls.keyCatchers & (KEYCATCH_UI | KEYCATCH_CGAME)) ) {
			Con_DrawSolidConsole( 1.0 );
			return;
		}
	}

	if ( con.displayFrac ) {
		Con_DrawSolidConsole( con.displayFrac );
	} else {
		// draw notify lines
		if ( cls.state == CA_ACTIVE ) {
			Con_DrawNotify ();
		}
	}
}

//================================================================

/*
==================
Con_RunConsole

Scroll it up or down
==================
*/
void Con_RunConsole (void) {
	// decide on the destination height of the console
	if ( cls.keyCatchers & KEYCATCH_CONSOLE )
		con.finalFrac = con_height->value;
	else
		con.finalFrac = 0;				// none visible

	if ( clc.demoplaying && (com_timescale->value < 1 || cl_paused->integer) ) {
		con.displayFrac = con.finalFrac;	// set console height instantly if timescale is very low or 0 in demo playback (happens when menu is up)
	} else {
		// scroll towards the destination height
		if (con.finalFrac < con.displayFrac)
		{
			con.displayFrac -= con_speed->value*(float)(cls.realFrametime*0.001);
			if (con.finalFrac > con.displayFrac)
				con.displayFrac = con.finalFrac;

		}
		else if (con.finalFrac > con.displayFrac)
		{
			con.displayFrac += con_speed->value*(float)(cls.realFrametime*0.001);
			if (con.finalFrac < con.displayFrac)
				con.displayFrac = con.finalFrac;
		}
	}
}


void Con_PageUp( int lines  ) {
	con.display -= lines;
	if ( con.current - con.display >= con.totallines ) {
		con.display = con.current - con.totallines + 1;
	}
}

void Con_PageDown( int lines  ) {
	con.display += lines;
	if (con.display > con.current) {
		con.display = con.current;
	}
}

void Con_Top( void ) {
	con.display = con.totallines;
	if ( con.current - con.display >= con.totallines ) {
		con.display = con.current - con.totallines + 1;
	}
}

void Con_Bottom( void ) {
	con.display = con.current;
}


void Con_Close( void ) {
	if ( !com_cl_running->integer ) {
		return;
	}
	Field_Clear( &kg.g_consoleField );
	Con_ClearNotify ();
	cls.keyCatchers &= ~KEYCATCH_CONSOLE;
	con.finalFrac = 0;				// none visible
	con.displayFrac = 0;
}
