#pragma once

#include "../qcommon/qcommon.h"

qboolean	Sys_GetPacket( netadr_t *net_from, msg_t *net_message );
char		*Sys_ConsoleInput( void );
void		Sys_QueEvent( int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr );
void		Sys_SigHandler( int signal );
#ifndef _WIN32
void		Sys_AnsiColorPrint( const char *msg );
#endif

void Sys_Sleep(int msec);
