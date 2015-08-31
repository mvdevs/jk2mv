#ifndef MVAPI_H
#define MVAPI_H

#ifndef Q3_VM
#	include <stdint.h>
#else
	typedef unsigned char uint8_t;
	typedef unsigned short uint16_t;
#endif

// -------------------------------------- API Version -------------------------------------- //
// MV_APILEVEL defines the API-Level which implements the following features.
// MV_MIN_VERSION is the minimum required JK2MV version which implements this API-Level.
// All future JK2MV versions are guaranteed to implement this API-Level.
// ----------------------------------------------------------------------------------------- //
#define MV_APILEVEL 1
#define MV_MIN_VERSION "1.1"
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

	/* CGAME */
	MVFIX_WPGLOWING           = (1 << 16),
} mvfix_t;

typedef enum {
	VERSION_UNDEF = 0,
	VERSION_1_02 = 2,
	VERSION_1_03 = 3,
	VERSION_1_04 = 4,
} mvversion_t;

// ******** SYSCALLS ******** //

// qboolean trap_MVAPI_ControlFixes(mvfix_t fixes);
#define MVAPI_CONTROL_FIXES 703                  /* asm: -704 */

// mvversion_t trap_MVAPI_GetVersion(void);
#define MVAPI_GET_VERSION 704                    /* asm: -705 */

// ******** VMCALLS ******** //

// vmMain(MVAPI_AFTER_INIT, ...)
#define MVAPI_AFTER_INIT 100

// ************************** //
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

typedef struct {
	uint8_t snapshotIgnore[32];
	uint8_t snapshotEnforce[32];
} mvsharedEntity_t;

// ******** SYSCALLS ******** //

// qboolean trap_MVAPI_SendConnectionlessPacket(const mvaddr_t *addr, const char *message);
#define MVAPI_SEND_CONNECTIONLESSPACKET 700      /* asm: -701 */

// qboolean trap_MVAPI_GetConnectionlessPacket(mvaddr_t *addr, char *buf, unsigned int bufsize);
#define MVAPI_GET_CONNECTIONLESSPACKET 701       /* asm: -702 */

// qboolean trap_MVAPI_LocateGameData(mvsharedEntity_t *mvEnts, int numGEntities, int sizeofmvsharedEntity_t);
#define MVAPI_LOCATE_GAME_DATA 702               /* asm: -703 */

// ******** VMCALLS ******** //

// vmMain(MVAPI_RECV_CONNECTIONLESSPACKET, ...)
#define MVAPI_RECV_CONNECTIONLESSPACKET 101

// ------------------------------------------ UI ------------------------------------------- //

#define MVSORT_CLIENTS_NOBOTS 5

// ----------------------------------------------------------------------------------------- //

#endif /* MVAPI_H */
