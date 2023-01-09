/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../server/server.h"
#include "sys_local.h"
#include "con_local.h"

#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/time.h>

/*
=============================================================
tty console routines

NOTE: if the user is editing a line when something gets printed to the early
console then it won't look good so we provide CON_Hide and CON_Show to be
called before and after a stdout or stderr output
=============================================================
*/

extern qboolean stdinIsATTY;
extern qboolean stdin_active;
// general flag to tell about tty console mode
static qboolean ttycon_on = qfalse;
static int ttycon_hide = 0;
static int ttycon_show_overdue = 0;

// some key codes that the terminal may be using, initialised on start up
static int TTY_erase;
static int TTY_eof;

static struct termios TTY_tc;

static field_t TTY_con;

// This is somewhat of aduplicate of the graphical console history
// but it's safer more modular to have our own here
#define CON_HISTORY 32
static char ttyEditLines[CON_HISTORY][MAX_EDIT_LINE];
static int hist_current;
static int hist_tail;
static int hist_head;

#ifndef DEDICATED
// Don't use "]" as it would be the same as in-game console,
//   this makes it clear where input came from.
#define TTY_CONSOLE_PROMPT "tty]"
#else
#define TTY_CONSOLE_PROMPT "]"
#endif

/*
==================
CON_CheckRep

Check integrity of console data structures
==================
*/
static void CON_CheckRep( void )
{
	assert(0 <= hist_head && hist_head < CON_HISTORY);
	assert(0 <= hist_tail && hist_tail < CON_HISTORY);
	assert(0 <= hist_current && hist_current < CON_HISTORY);

	if (hist_head <= hist_tail)
		assert(hist_head <= hist_current && hist_current <= hist_tail);
	else
		assert(hist_current <= hist_tail || hist_head <= hist_current);

	Field_CheckRep(&TTY_con);
}

/*
==================
CON_FlushIn

Flush stdin, I suspect some terminals are sending a LOT of shit
FIXME relevant?
==================
*/
static void CON_FlushIn( void )
{
	char key;
	while (read(STDIN_FILENO, &key, 1)!=-1);
}

/*
==================
CON_Back

Output a backspace

NOTE: it seems on some terminals just sending '\b' is not enough so instead we
send "\b \b"
(FIXME there may be a way to find out if '\b' alone would work though)
==================
*/
static void CON_Back( void )
{
	char key;

	key = '\b';
	write(STDOUT_FILENO, &key, 1);
	key = ' ';
	write(STDOUT_FILENO, &key, 1);
	key = '\b';
	write(STDOUT_FILENO, &key, 1);
}

/*
==================
CON_Hide

Clear the display of the line currently edited
bring cursor back to beginning of line
==================
*/
static void CON_Hide( void )
{
	if( ttycon_on )
	{
		int i;
		if (ttycon_hide)
		{
			ttycon_hide++;
			return;
		}
		if (TTY_con.cursor>0)
		{
			for (i=0; i<TTY_con.cursor; i++)
			{
				CON_Back();
			}
		}
		// Delete prompt
		for (i = strlen(TTY_CONSOLE_PROMPT); i > 0; i--) {
			CON_Back();
		}
		ttycon_hide++;
	}
}

/*
==================
CON_Show

Show the current line
FIXME need to position the cursor if needed?
==================
*/
static void CON_Show( void )
{
	if( ttycon_on )
	{
		int i;

		assert(ttycon_hide>0);
		ttycon_hide--;
		if (ttycon_hide == 0)
		{
			write(STDOUT_FILENO, TTY_CONSOLE_PROMPT, strlen(TTY_CONSOLE_PROMPT));
			if (TTY_con.cursor)
			{
				for (i=0; i<TTY_con.cursor; i++)
				{
					write(STDOUT_FILENO, TTY_con.buffer+i, 1);
				}
			}
		}
	}
}

/*
==================
CON_Shutdown

Never exit without calling this, or your terminal will be left in a pretty bad state
==================
*/
void CON_Shutdown( void )
{
	if (ttycon_on)
	{
		CON_Hide();
		tcsetattr (STDIN_FILENO, TCSADRAIN, &TTY_tc);
	}

	// Restore blocking to stdin reads
	fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) & ~O_NONBLOCK);
}

/*
==================
Hist_Add
==================
*/
void Hist_Add( void )
{
	// Don't save blank lines in history.
	if (!TTY_con.cursor)
		return;

	memcpy(ttyEditLines[hist_tail], TTY_con.buffer, MAX_EDIT_LINE);

	hist_tail = (hist_tail + 1) % CON_HISTORY;
	hist_current = hist_tail;

	if (hist_tail == hist_head)
		hist_head = (hist_head + 1) % CON_HISTORY;
}

/*
==================
Hist_Prev
==================
*/
void Hist_Prev( void )
{
	if (hist_current == hist_head)
		return;

	hist_current = (hist_current + CON_HISTORY - 1) % CON_HISTORY;

	CON_Hide();
	Field_Clear(&TTY_con);
	memcpy(TTY_con.buffer, ttyEditLines[hist_current], MAX_EDIT_LINE);
	TTY_con.cursor = strlen(TTY_con.buffer);
	CON_Show();
}

/*
==================
Hist_Next
==================
*/
void Hist_Next( void )
{
	if (hist_current == hist_tail)
		return;

	hist_current = (hist_current + 1) % CON_HISTORY;

	CON_Hide();
	Field_Clear(&TTY_con);
	if (hist_current != hist_tail)
		memcpy(TTY_con.buffer, ttyEditLines[hist_current], MAX_EDIT_LINE);
	TTY_con.cursor = strlen(TTY_con.buffer);
	CON_Show();
}

/*
==================
CON_SigCont
Reinitialize console input after receiving SIGCONT, as on Linux the terminal seems to lose all
set attributes if user did CTRL+Z and then does fg again.
==================
*/

void CON_SigCont(int signum)
{
	CON_Init();
}

/*
==================
CON_Init

Initialize the console input (tty mode if possible)
==================
*/
void CON_Init( void )
{
	struct termios tc;

	// If the process is backgrounded (running non interactively)
	// then SIGTTIN or SIGTOU is emitted, if not caught, turns into a SIGSTP
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);

	// If SIGCONT is received, reinitialize console
	signal(SIGCONT, CON_SigCont);

	// Make stdin reads non-blocking
	fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK );

	if (!stdinIsATTY)
	{
		Com_Printf("tty console mode disabled\n");
		ttycon_on = qfalse;
		stdin_active = qtrue;
		return;
	}

	Field_Clear(&TTY_con);
	tcgetattr (STDIN_FILENO, &TTY_tc);
	TTY_erase = TTY_tc.c_cc[VERASE];
	TTY_eof = TTY_tc.c_cc[VEOF];
	tc = TTY_tc;

	/*
	ECHO: don't echo input characters
	ICANON: enable canonical mode.  This  enables  the  special
	characters  EOF,  EOL,  EOL2, ERASE, KILL, REPRINT,
	STATUS, and WERASE, and buffers by lines.
	ISIG: when any of the characters  INTR,  QUIT,  SUSP,  or
	DSUSP are received, generate the corresponding signal
	*/
	tc.c_lflag &= ~(ECHO | ICANON);

	/*
	ISTRIP strip off bit 8
	INPCK enable input parity checking
	*/
	tc.c_iflag &= ~(ISTRIP | INPCK);
	tc.c_cc[VMIN] = 1;
	tc.c_cc[VTIME] = 0;
	tcsetattr (STDIN_FILENO, TCSADRAIN, &tc);
	ttycon_on = qtrue;
	ttycon_hide = 1; // Mark as hidden, so prompt is shown in CON_Show
	CON_Show();
}

void CON_CreateConsoleWindow() {}
void CON_DeleteConsoleWindow() {}

/*
==================
CON_Input
==================
*/
char *CON_Input( void )
{
	// we use this when sending back commands
	static char text[MAX_EDIT_LINE];
	int avail;
	char key;

	if(ttycon_on)
	{
		CON_CheckRep();
		avail = read(STDIN_FILENO, &key, 1);
		if (avail != -1)
		{
			// we have something

			// disable hibernation for ten seconds to workaround console input lagg
			svs.hibernation.disableUntil = svs.time + 10000;

			// backspace?
			// NOTE TTimo testing a lot of values .. seems it's the only way to get it to work everywhere
			if ((key == TTY_erase) || (key == 127) || (key == 8))
			{
				if (TTY_con.cursor > 0)
				{
					TTY_con.cursor--;
					TTY_con.buffer[TTY_con.cursor] = '\0';
					CON_Back();
				}
				return NULL;
			}
			// check if this is a control char
			if ((key) && (key) < ' ')
			{
				if (key == '\n')
				{
#ifndef DEDICATED
					if (TTY_con.buffer[0] == '/' || TTY_con.buffer[0] == '\\') {
						Q_strncpyz(text, TTY_con.buffer + 1, sizeof(text));
					} else if (TTY_con.cursor) {
						Q_strncpyz(text, TTY_con.buffer, sizeof(text));
					} else {
						text[0] = '\0';
					}

					// push it in history
					Hist_Add();
					CON_Hide();
					Com_Printf("%s%s\n", TTY_CONSOLE_PROMPT, TTY_con.buffer);
					Field_Clear(&TTY_con);
					CON_Show();
#else
					// push it in history
					Hist_Add();
					Q_strncpyz(text, TTY_con.buffer, sizeof(text));
					Field_Clear(&TTY_con);
					key = '\n';
					write(STDOUT_FILENO, &key, 1);
					write(STDOUT_FILENO, TTY_CONSOLE_PROMPT, strlen(TTY_CONSOLE_PROMPT));
#endif
					return text;
				}
				if (key == '\t')
				{
					CON_Hide();
					Field_AutoComplete( &TTY_con );
					CON_Show();
					return NULL;
				}
				avail = read(STDIN_FILENO, &key, 1);
				if (avail != -1)
				{
					// VT 100 keys
					if (key == '[' || key == 'O')
					{
						avail = read(STDIN_FILENO, &key, 1);
						if (avail != -1)
						{
							switch (key)
							{
								case 'A':
									Hist_Prev();
									CON_FlushIn();
									return NULL;
									break;
								case 'B':
									Hist_Next();
									CON_FlushIn();
									return NULL;
									break;
								case 'C':
									return NULL;
								case 'D':
									return NULL;
							}
						}
					}
				}
				Com_DPrintf("droping ISCTL sequence: %d, TTY_erase: %d\n", key, TTY_erase);
				CON_FlushIn();
				return NULL;
			}
			if (TTY_con.cursor >= (int)sizeof(text) - 1)
				return NULL;
			// push regular character
			TTY_con.buffer[TTY_con.cursor] = key;
			TTY_con.cursor++; // next char will always be '\0'
			// print the current line (this is differential)
			write(STDOUT_FILENO, &key, 1);
		}

		return NULL;
	}
	else if (stdin_active)
	{
		int	 len;
		fd_set  fdset;
		struct timeval timeout;

		FD_ZERO(&fdset);
		FD_SET(STDIN_FILENO, &fdset); // stdin
		timeout.tv_sec = 0;
		timeout.tv_usec = 0;
		if(select (STDIN_FILENO + 1, &fdset, NULL, NULL, &timeout) == -1 || !FD_ISSET(STDIN_FILENO, &fdset))
			return NULL;

		len = read(STDIN_FILENO, text, sizeof(text));
		if (len == 0)
		{ // eof!
			stdin_active = qfalse;
			return NULL;
		}

		if (len < 1)
			return NULL;
		text[len-1] = 0;	// rip off the /n and terminate

		return text;
	}
	return NULL;
}

/*
==================
CON_Print
==================
*/
void CON_Print( const char *msg, bool extendedColors )
{
	if (!msg[0])
		return;

	CON_Hide( );

	Sys_AnsiColorPrint( msg, extendedColors );

	if (!ttycon_on) {
		// CON_Hide didn't do anything.
		return;
	}

	// Only print prompt when msg ends with a newline, otherwise the console
	//   might get garbled when output does not fit on one line.
	if (msg[strlen(msg) - 1] == '\n') {
		CON_Show();

		// Run CON_Show the number of times it was deferred.
		while (ttycon_show_overdue > 0) {
			CON_Show();
			ttycon_show_overdue--;
		}
	}
	else
	{
		// Defer calling CON_Show
		ttycon_show_overdue++;
	}
}

/*
=================
Sys_AnsiColorPrint

Transform Q3 colour codes to ANSI escape sequences
=================
*/
void Sys_AnsiColorPrint( const char *msg, bool extendedColors )
{
	static char buffer[MAXPRINTMSG];
	int         length = 0;
	static int  q3ToAnsi[Q_COLOR_BITS+1] =
	{
		30, // COLOR_BLACK
		31, // COLOR_RED
		32, // COLOR_GREEN
		33, // COLOR_YELLOW
		34, // COLOR_BLUE
		36, // COLOR_CYAN
		35, // COLOR_MAGENTA
		0,  // COLOR_WHITE
	};

	const bool use102col = MV_USE102COLOR;

	while ( *msg )
	{
		if ( Q_IsColorString( msg ) || (use102col && Q_IsColorString_1_02(msg)) || (extendedColors && Q_IsColorString_Extended(msg)) || *msg == '\n' )
		{
			// First empty the buffer
			if ( length > 0 )
			{
				buffer[length] = '\0';
				fputs( buffer, stderr );
				length = 0;
			}

			if ( *msg == '\n' )
			{
				// Issue a reset and then the newline
				fputs( "\033[0m\n", stderr );
				msg++;
			}
			else
			{
				// Print the color code
				int colIndex = (extendedColors ? ColorIndex_Extended(*(msg+1)) : ColorIndex(*(msg+1)));
				if ( colIndex >= (int)ARRAY_LEN(q3ToAnsi) ) colIndex = COLOR_JK2MV_FALLBACK;
				Com_sprintf( buffer, sizeof( buffer ), "\033[%dm",
					q3ToAnsi[colIndex] );
				fputs( buffer, stderr );
				msg += 2;
			}
		}
		else
		{
			if ( length >= MAXPRINTMSG - 1 )
				break;

			// Limit to printable range
			if ( *msg >= ' ' && *msg <= '~' )
				buffer[length] = *msg;
			else
				buffer[length] = '.';

			length++;
			msg++;
		}
	}

	// Empty anything still left in the buffer
	if ( length > 0 )
	{
		buffer[length] = '\0';
		fputs( buffer, stderr );
	}
}

