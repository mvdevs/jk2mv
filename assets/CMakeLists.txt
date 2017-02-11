# Make sure the user is not executing this script directly
if(NOT InMV)
	message(FATAL_ERROR "Use the top-level cmake script!")
endif(NOT InMV)

set(AssetsFiles
	"fonts/anewhope.fontdat"
	"fonts/anewhope_sharp1.fontdat"
	"fonts/anewhope_sharp1.tga"
	"fonts/anewhope_sharp2.fontdat"
	"fonts/anewhope_sharp2.tga"
	"fonts/anewhope_sharp3.fontdat"
	"fonts/anewhope_sharp3.tga"
	"fonts/anewhope_sharp4.fontdat"
	"fonts/anewhope_sharp4.tga"
	"fonts/anewhope_sharp5.fontdat"
	"fonts/anewhope_sharp5.tga"
	"fonts/anewhope.tga"
	"fonts/ergoec_sharp1.fontdat"
	"fonts/ergoec_sharp1.tga"
	"fonts/ergoec_sharp2.fontdat"
	"fonts/ergoec_sharp2.tga"
	"fonts/ergoec_sharp3.fontdat"
	"fonts/ergoec_sharp3.tga"
	"fonts/ergoec_sharp4.fontdat"
	"fonts/ergoec_sharp4.tga"
	"fonts/ergoec_sharp5.fontdat"
	"fonts/ergoec_sharp5.tga"
	"fonts/ocr_a.fontdat"
	"fonts/ocr_a_sharp1.fontdat"
	"fonts/ocr_a_sharp1.tga"
	"fonts/ocr_a_sharp2.fontdat"
	"fonts/ocr_a_sharp2.tga"
	"fonts/ocr_a_sharp3.fontdat"
	"fonts/ocr_a_sharp3.tga"
	"fonts/ocr_a_sharp4.fontdat"
	"fonts/ocr_a_sharp4.tga"
	"fonts/ocr_a.tga"

	"gfx/2d/code_new_roman_mip1.tga"
	"gfx/2d/code_new_roman_mip2.tga"
	"gfx/2d/code_new_roman_mip3.tga"
	"gfx/2d/code_new_roman.tga"
	"gfx/2d/demorec.tga"
	"gfx/2d/mplus_1m_bold_mip1.tga"
	"gfx/2d/mplus_1m_bold_mip2.tga"
	"gfx/2d/mplus_1m_bold_mip3.tga"
	"gfx/2d/mplus_1m_bold.tga"
	"gfx/menus/srv_102.jpg"
	"gfx/menus/srv_103.jpg"
	"gfx/menus/srv_104.jpg"

	"shaders/jk2mv.shader_mv"
	"shaders/sabers.dynGlow"

	"strip/mv.sp"

	"ui/jk2mpingame.txt"
	"ui/jk2mp/createserver.menu_patch"
	"ui/jk2mp/ingame_player.menu_patch"
	"ui/jk2mp/ingame_setup.menu_patch"
	"ui/jk2mp/ingame_setup_modoptions.menu"
	"ui/jk2mp/joinserver.menu_patch"
	"ui/jk2mp/setup.menu_patch"
	"ui/jk2mv/download_info.menu"
	"ui/jk2mv/download_popup.menu"
)

source_group("assets" FILES ${AssetsFiles})

# generate assetsmv.pk3
include(InstallZIP)
if(NOT ZIP_EXECUTABLE)
	if(WIN32)
		message(FATAL_ERROR "No ZIP executable found! Install 7zip, WinZIP or WinRAR.")
	else()
		message(FATAL_ERROR "No zip executable found! Install the zip program.")
	endif()
endif()

add_zip_command("${CMAKE_CURRENT_BINARY_DIR}/assetsmv.pk3"
	DIR "${CMAKE_SOURCE_DIR}/assets"
	FILES "${AssetsFiles}"
	DEPENDS "${AssetsFiles}"
)

add_custom_target(assets DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/assetsmv.pk3" ${AssetsFiles})

if(WIN32)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/assetsmv.pk3 DESTINATION "base")
elseif(NOT APPLE)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/assetsmv.pk3 DESTINATION "share/jk2mv/base")
endif()
