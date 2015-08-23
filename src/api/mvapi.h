#ifndef MVAPI_H
#define MVAPI_H

#ifndef Q3_VM
#include <stdint.h>
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

typedef enum {
	MVFIX_NAMECRASH = 1,
	MVFIX_FORCECRASH = 2,
	MVFIX_GALAKING = 4,
	MVFIX_BROKENMODEL = 8,
	MVFIX_TURRETCRASH = 16,
	MVFIX_CHARGEJUMP = 32,
	MVFIX_SPEEDHACK = 64
} mvfix_t;

// ------------------------------------ Shared Functions ----------------------------------- //

// ******** SYSCALLS ******** //

// qboolean trap_MVAPI_ControlFixes(mvfix_t fixes);
// Since MV_APILEVEL 1
#define MVAPI_CONTROL_FIXES 703                  /* asm: -704 */

// ************************** //

// ------------------------------------ GAME Functions ------------------------------------- //

// ******** SYSCALLS ******** //

// qboolean trap_MVAPI_SendConnectionlessPacket(const mvaddr_t *addr, const char *message);
// Since MV_APILEVEL 1
#define MVAPI_SEND_CONNECTIONLESSPACKET 700      /* asm: -701 */

// qboolean trap_MVAPI_GetConnectionlessPacket(mvaddr_t *addr, char *buf, unsigned int bufsize);
// Since MV_APILEVEL 1
#define MVAPI_GET_CONNECTIONLESSPACKET 701       /* asm: -702 */

// qboolean trap_MVAPI_LocateGameData(mvsharedEntity_t *mvEnts, int numGEntities, int sizeofmvsharedEntity_t);
// Since MV_APILEVEL 1
#define MVAPI_LOCATE_GAME_DATA 702               /* asm: -703 */

// ************************** //

// ******** VMCALLS ******** //

// vmMain(MVAPI_RECV_CONNECTIONLESSPACKET, ...)
// Since MV_APILEVEL 1
#define MVAPI_RECV_CONNECTIONLESSPACKET 21

// ************************** //

// ----------------------------------------------------------------------------------------- //

#endif /* MVAPI_H */
