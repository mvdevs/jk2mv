#pragma once

#include <cstdio>

/* con_passive.cpp | con_win32.cpp | con_tty.cpp */
void CON_Shutdown( void );
void CON_Init( void );
char *CON_Input( void );
void CON_Print( const char *msg, bool extendedColors );

/* con_log.cpp */
#define MAX_CONSOLE_LOG_SIZE (65535)

struct ConsoleLog
{
	// Cicular buffer of characters. Be careful, there is no null terminator.
	// You're expected to use the console log length to know where the end
	// of the string is.
	char text[MAX_CONSOLE_LOG_SIZE];

	// Where to start writing the next string
	int writeHead;

	// Length of buffer
	int length;
};

extern ConsoleLog consoleLog;

void ConsoleLogAppend( const char *string );
void ConsoleLogWriteOut( FILE *fp );
