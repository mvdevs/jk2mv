# Make sure the user is not executing this script directly
if(NOT InMV)
	message(FATAL_ERROR "Use the top-level cmake script!")
endif(NOT InMV)

set(MVMENUDefines "GAME_EXPORTS")

set(MVMENUFiles
	"${SRCDir}/menu/keycodes.h"
	"${SRCDir}/menu/menudef.h"
	"${SRCDir}/menu/ui_force.h"
	"${SRCDir}/menu/ui_local.h"
	"${SRCDir}/menu/ui_public.h"
	"${SRCDir}/menu/ui_shared.h"
	"${SRCDir}/game/bg_weapons.h"
	"${SRCDir}/game/q_shared.h"

	"${SRCDir}/menu/q_math.c"
	"${SRCDir}/menu/ui_atoms.c"
	"${SRCDir}/menu/ui_force.c"
	"${SRCDir}/menu/ui_gameinfo.c"
	"${SRCDir}/menu/ui_main.c"
	"${SRCDir}/menu/ui_shared.c"
	"${SRCDir}/menu/ui_syscalls.c"
	"${SRCDir}/menu/ui_util.c"
	"${SRCDir}/game/bg_misc.c"
	"${SRCDir}/game/bg_weapons.c"
	"${SRCDir}/game/q_shared.c"
)

add_library(${MVMENU} SHARED ${MVMENUFiles})

if(NOT WIN32)
	set_target_properties(${MVMENU} PROPERTIES PREFIX "") # remove "lib" prefix for .so/.dylib files
endif(NOT WIN32)

set_target_properties(${MVMENU} PROPERTIES COMPILE_DEFINITIONS_RELEASE "${ReleaseDefines};${GlobalDefines};${MVMENUDefines}")
set_target_properties(${MVMENU} PROPERTIES COMPILE_DEFINITIONS_DEBUG "${DebugDefines};${GlobalDefines};${MVMENUDefines}")
set_target_properties(${MVMENU} PROPERTIES PROJECT_LABEL "jk2mvmenu")

if(WIN32)
	install(TARGETS ${MVMENU} RUNTIME DESTINATION ".")
elseif(APPLE)
	install(TARGETS ${MVMENU} LIBRARY DESTINATION "${MVMP}.app/Contents/MacOS")
else()
	install(TARGETS ${MVMENU} LIBRARY DESTINATION ${ProjectName})
endif()
