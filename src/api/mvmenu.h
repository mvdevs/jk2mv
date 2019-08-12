#ifndef MVMENU_H
#define MVMENU_H

#if defined(JK2MV_MENU) || defined(JK2_UI)
#   include "../game/q_shared.h"
#else
#   include "../qcommon/q_shared.h"
#endif


// #####################################################################################################
// ### Forks of JK2MV should NOT modify any content of this file. Forks should define their own api. ###
// #####################################################################################################
// ### Removing, adding or modifying defines, flags, functions, etc. might lead to incompatibilities ###
// ### and crashes.                                                                                  ###
// #####################################################################################################

// -------------------------------------- Menu Level --------------------------------------- //
// The menulevel is used for mvmenu and ui modules loaded while the client is not ingame.
// Future versions of JK2MV might drop previous menulevels at any time.
// ----------------------------------------------------------------------------------------- //
#define MV_MENULEVEL_MAX 2
#define MV_MENULEVEL_MIN 2
// ----------------------------------------------------------------------------------------- //

// ---------------------------------------- MVMENU ----------------------------------------- //

typedef enum {
    DL_ACCEPT,
    DL_ABORT,
    DL_ABORT_BLACKLIST,
} dldecision_t;

typedef struct {
    char name[256];
    int checkksum;
    qboolean blacklisted;
} dlfile_t;

// ******** SYSCALLS ******** //

#define UI_MVAPI_CONTINUE_DOWNLOAD 300              /* asm: -301 */
#define UI_MVAPI_GETDLLIST 301                      /* asm: -302 */
#define UI_MVAPI_RMDLPREFIX 302                     /* asm: -303 */
#define UI_MVAPI_DELDLFILE 303                      /* asm: -304 */

// ----------------------------------------------------------------------------------------- //

#endif /* MVMENU_H */
