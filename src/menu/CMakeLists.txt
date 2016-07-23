# Make sure the user is not executing this script directly
if(NOT InMV)
	message(FATAL_ERROR "Use the top-level cmake script!")
endif(NOT InMV)

set(MVMENUDefines "GAME_EXPORTS")
set(MVMENUIncludeDirs ${MVGLOBALIncludeDirs})

set(MVMENUFiles
	"keycodes.h"
	"menudef.h"
	"ui_force.h"
	"ui_local.h"
	"ui_public.h"
	"ui_shared.h"
	"../game/bg_weapons.h"
	"../qcommon/q_shared.h"

	"q_math.c"
	"q_shared.c"
	"ui_atoms.c"
	"ui_force.c"
	"ui_gameinfo.c"
	"ui_main.c"
	"ui_shared.c"
	"ui_syscalls.c"
	"../game/bg_misc.c"
	"../game/bg_weapons.c"
)

add_library(${MVMENU} SHARED ${MVMENUFiles})

if(NOT WIN32)
	set_target_properties(${MVMENU} PROPERTIES PREFIX "") # remove "lib" prefix for .so/.dylib files
endif()

set_target_properties(${MVMENU} PROPERTIES COMPILE_DEFINITIONS_RELEASE "${ReleaseDefines};${GlobalDefines};${MVMENUDefines}")
set_target_properties(${MVMENU} PROPERTIES COMPILE_DEFINITIONS_DEBUG "${DebugDefines};${GlobalDefines};${MVMENUDefines}")
set_target_properties(${MVMENU} PROPERTIES INCLUDE_DIRECTORIES "${MVMENUIncludeDirs}")
set_target_properties(${MVMENU} PROPERTIES PROJECT_LABEL "jk2mvmenu")

if(WIN32)
	install(TARGETS ${MVMENU} RUNTIME DESTINATION ".")
elseif(APPLE)
	install(TARGETS ${MVMENU} LIBRARY DESTINATION "${MVMP}.app/Contents/MacOS")
else()
	install(TARGETS ${MVMENU} LIBRARY DESTINATION "lib")
endif()
