#ifndef MVAPI_H
#define MVAPI_H

#ifndef Q3_VM
    #include <stdint.h>
#elif !defined(VM_STDINT)
    #define VM_STDINT
    typedef unsigned char uint8_t;
    typedef unsigned short uint16_t;
    typedef unsigned int uint32_t;

    typedef signed char int8_t;
    typedef signed short int16_t;
    typedef signed int int32_t;
#endif


// #####################################################################################################
// ### Forks of JK2MV should NOT modify any content of this file. Forks should define their own api. ###
// #####################################################################################################
// ### Removing, adding or modifying defines, flags, functions, etc. might lead to incompatibilities ###
// ### and crashes.                                                                                  ###
// #####################################################################################################


// -------------------------------------- API Version -------------------------------------- //
// MV_APILEVEL defines the API-Level which implements the following features.
// MV_MIN_VERSION is the minimum required JK2MV version which implements this API-Level.
// All future JK2MV versions are guaranteed to implement this API-Level.
// ----------------------------------------------------------------------------------------- //
#define MV_APILEVEL 3
#define MV_MIN_VERSION "1.4"
// ----------------------------------------------------------------------------------------- //

// ----------------------------------------- SHARED ---------------------------------------- //

typedef enum {
    MVFIX_NONE                = 0,

    /* GAME */
    MVFIX_NAMECRASH           = (1 << 0),
    MVFIX_FORCECRASH          = (1 << 1),
    MVFIX_GALAKING            = (1 << 2),
    MVFIX_BROKENMODEL         = (1 << 3),
    MVFIX_TURRETCRASH         = (1 << 4),
    MVFIX_CHARGEJUMP          = (1 << 5),
    MVFIX_SPEEDHACK           = (1 << 6),
    MVFIX_SABERSTEALING       = (1 << 7),
    MVFIX_PLAYERGHOSTING      = (1 << 8),

    /* CGAME */
    MVFIX_WPGLOWING           = (1 << 16),
} mvfix_t;

typedef enum {
    VERSION_UNDEF = 0,
    VERSION_1_02 = 2,
    VERSION_1_03 = 3,
    VERSION_1_04 = 4,
} mvversion_t;

typedef enum {
    PROTOCOL_UNDEF = 0,
    PROTOCOL15 = 15,
    PROTOCOL16 = 16,
} mvprotocol_t;

typedef enum
{
    FLOCK_SH,
    FLOCK_EX,
    FLOCK_UN
} flockCmd_t;


// ----------------------------------------- GAME ------------------------------------------ //

typedef enum {
    MV_IPV4
} mviptype_t;

typedef struct {
    mviptype_t type;

    union {
        uint8_t v4[4];
        uint8_t reserved[16];
    } ip;

    uint16_t port;
} mvaddr_t;

#define MVF_NOSPEC      0x01
#define MVF_SPECONLY    0x02

typedef struct {
    uint8_t     snapshotIgnore[32];
    uint8_t     snapshotEnforce[32];
    uint32_t    mvFlags;
} mvsharedEntity_t;

// ------------------------------------------ UI ------------------------------------------- //

#define MVSORT_CLIENTS_NOBOTS 5

// ----------------------------------------------------------------------------------------- //

// ---------------------------------------- Syscalls --------------------------------------- //
typedef enum
{
    // ******** Level 1 ******** //
    // -701: qboolean trap_MVAPI_SendConnectionlessPacket(const mvaddr_t *addr, const char *message);
    G_MVAPI_SEND_CONNECTIONLESSPACKET = 700,                                    // GAME

    // -702: qboolean trap_MVAPI_GetConnectionlessPacket(mvaddr_t *addr, char *buf, unsigned int bufsize);
    G_MVAPI_GET_CONNECTIONLESSPACKET,                                           // GAME

    // -703: qboolean trap_MVAPI_LocateGameData(mvsharedEntity_t *mvEnts, int numGEntities, int sizeofmvsharedEntity_t);
    G_MVAPI_LOCATE_GAME_DATA,                                                   // GAME

    // -704: qboolean trap_MVAPI_ControlFixes(int fixes);
    MVAPI_CONTROL_FIXES,                                                        // SHARED

    // -705: mvversion_t trap_MVAPI_GetVersion(void);
    MVAPI_GET_VERSION,                                                          // SHARED


    // ******** Level 2 ******** //
    // -706: qboolean trap_MVAPI_DisableStructConversion(qboolean disable);
    G_MVAPI_DISABLE_STRUCT_CONVERSION,                                          // GAME


    // ******** Level 3 ******** //
    // -707: void trap_R_AddRefEntityToScene2(const refEntity_t *re);
    CG_MVAPI_R_ADDREFENTITYTOSCENE2,                                            // CGAME
    UI_MVAPI_R_ADDREFENTITYTOSCENE2 = CG_MVAPI_R_ADDREFENTITYTOSCENE2,          // UI

    // -708: void trap_MVAPI_SetVirtualScreen(float w, float h);
    CG_MVAPI_SETVIRTUALSCREEN,                                                  // CGAME
    UI_MVAPI_SETVIRTUALSCREEN = CG_MVAPI_SETVIRTUALSCREEN,                      // UI

    // -709: int trap_FS_FLock(fileHandle_t h, flockCmd_t cmd, qboolean nb);
    MVAPI_FS_FLOCK,                                                             // SHARED

    // -710: void trap_MVAPI_SetVersion(mvversion_t version);
    MVAPI_SET_VERSION,                                                          // SHARED


    // ******** Level 4 ******** //
    // -711: qboolean trap_MVAPI_ResetServerTime(qboolean enable)
    G_MVAPI_RESET_SERVER_TIME,                                                  // GAME

    // -712: qboolean trap_MVAPI_EnablePlayerSnapshots(qboolean enable);
    G_MVAPI_ENABLE_PLAYERSNAPSHOTS,                                             // GAME

    // -713: qboolean trap_MVAPI_EnableSubmodelBypass(qboolean enable);
    CG_MVAPI_ENABLE_SUBMODELBYPASS,                                             // CGAME
    G_MVAPI_ENABLE_SUBMODELBYPASS = CG_MVAPI_ENABLE_SUBMODELBYPASS              // GAME
} mvSyscall_t;
// ----------------------------------------------------------------------------------------- //

// ---------------------------------------- vmCalls ---------------------------------------- //
typedef enum
{
    // ******** Level 1 ******** //
    // vmMain(MVAPI_AFTER_INIT, ...)
    MVAPI_AFTER_INIT = 100,                                                     // SHARED

    // vmMain(GAME_MVAPI_RECV_CONNECTIONLESSPACKET, ...)
    GAME_MVAPI_RECV_CONNECTIONLESSPACKET,                                       // GAME

    // ******** Level 4 ******** //
    // vmMain(GAME_MVAPI_PLAYERSNAPSHOT, ...)
    GAME_MVAPI_PLAYERSNAPSHOT                                                   // GAME
} mvVmCall_t;
// ----------------------------------------------------------------------------------------- //

#endif /* MVAPI_H */
