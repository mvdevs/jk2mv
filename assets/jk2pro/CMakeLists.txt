# Make sure the user is not executing this script directly
if(NOT InMV)
	message(FATAL_ERROR "Use the top-level cmake script!")
endif(NOT InMV)

set(AssetsFiles
	"gfx/hud/keys/back_off.tga"
	"gfx/hud/keys/back_on.tga"
	"gfx/hud/keys/crouch_off.tga"
	"gfx/hud/keys/crouch_on.tga"
	"gfx/hud/keys/forward_off.tga"
	"gfx/hud/keys/forward_on.tga"
	"gfx/hud/keys/jump_off.tga"
	"gfx/hud/keys/jump_on.tga"
	"gfx/hud/keys/left_off.tga"
	"gfx/hud/keys/left_on.tga"
	"gfx/hud/keys/right_off.tga"
	"gfx/hud/keys/right_on.tga"
	
	"menu/medals/medal_assist.png"
	"menu/medals/medal_capture.png"
	"menu/medals/medal_defend.png"
	"menu/medals/medal_excellent.png"
	"menu/medals/medal_gauntlet.png"
	"menu/medals/medal_impressive.png"
	
	"shaders/q3mod.shader"
	
	"sound/chars/mothma/misc/40MOM017.mp3"
	"sound/chars/mothma/misc/40MOM019.mp3"
	"sound/chars/mothma/misc/40MOM024.mp3"
	"sound/chars/mothma/misc/40MOM025.mp3"
	"sound/chars/mothma/misc/40MOM026.mp3"
	"sound/chars/mothma/misc/40MOM053.mp3"
	"sound/chars/tavion/misc/jump1.mp3"
	"sound/common/warning.wav"
)

source_group("jk2pro-assets" FILES ${AssetsFiles})

# generate jk2pro-assets.pk3
if(MultiConfigGenerator)
	set(OUTFILE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/eternaljk2/jk2pro-assets.pk3)
else()
	set(OUTFILE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/eternaljk2/jk2pro-assets.pk3)
endif()

include(InstallZIP)
add_zip_command(${CMAKE_CURRENT_BINARY_DIR}/jk2pro-assets.pk3 ${OUTFILE}
	DIR "${CMAKE_SOURCE_DIR}/assets/jk2pro"
	FILES "${AssetsFiles}"
	DEPENDS "${AssetsFiles}"
)

add_custom_target(assets-jk2pro DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/jk2pro-assets.pk3" ${AssetsFiles})

if(WIN32 OR BuildPortableVersion)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/eternaljk2-assets.pk3 DESTINATION "eternaljk2")
elseif(APPLE)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/eternaljk2-assets.pk3 DESTINATION "jk2mvmp.app/Contents/MacOS/eternaljk2")
else()
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/eternaljk2-assets.pk3 DESTINATION "share/jk2mv/eternaljk2")
endif()
