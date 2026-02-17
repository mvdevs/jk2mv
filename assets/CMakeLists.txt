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
	"ui/jk2mp/controls.menu_patch"
	"ui/jk2mp/createserver.menu_patch"
	"ui/jk2mp/ingame_controls.menu_patch"
	"ui/jk2mp/ingame_player.menu_patch"
	"ui/jk2mp/ingame_setup.menu_patch"
	"ui/jk2mp/ingame_setup_modoptions.menu"
	"ui/jk2mp/joinserver.menu_patch"
	"ui/jk2mp/setup.menu_patch"
	"ui/jk2mv/download_info.menu"
	"ui/jk2mv/download_popup.menu"

	"vm/cgame_102.map"
	"vm/cgame_103.map"
	"vm/cgame_104.map"
	"vm/jk2mpgame_102.map"
	"vm/jk2mpgame_103.map"
	"vm/jk2mpgame_104.map"
	"vm/ui_102.map"
	"vm/ui_103.map"
	"vm/ui_104.map"
)

source_group("assetsmv" FILES ${AssetsFiles})

# ============================================================
# Compile SPIR-V shaders from GLSL sources using glslangValidator
# ============================================================
find_program(GLSLANG_VALIDATOR glslangValidator HINTS $ENV{VULKAN_SDK}/bin ${Vulkan_INCLUDE_DIRS}/../bin)
if(NOT GLSLANG_VALIDATOR)
	message(WARNING "glslangValidator not found! SPIR-V shaders will not be compiled. Install the Vulkan SDK to enable shader compilation.")
endif()

set(SPIRV_SHADER_SOURCES
	"shaders/spirv/single_tex.vert"
	"shaders/spirv/single_tex.frag"
	"shaders/spirv/multi_tex.frag"
	"shaders/spirv/gamma.vert"
	"shaders/spirv/gamma.frag"
	"shaders/spirv/glow.vert"
	"shaders/spirv/glow.frag"
	"shaders/spirv/glow_composite.frag"
)

set(SPIRV_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY ${SPIRV_OUTPUT_DIR})

set(SPIRV_OUTPUT_FILES "")

if(GLSLANG_VALIDATOR)
	foreach(SHADER ${SPIRV_SHADER_SOURCES})
		get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
		get_filename_component(SHADER_EXT ${SHADER} EXT)
		# Convert .vert -> _vert.spv, .frag -> _frag.spv
		string(REPLACE "." "_" SPV_SUFFIX ${SHADER_EXT})
		set(SPV_FILE "${SPIRV_OUTPUT_DIR}/${SHADER_NAME}${SPV_SUFFIX}.spv")

		add_custom_command(
			OUTPUT ${SPV_FILE}
			COMMAND ${GLSLANG_VALIDATOR} -V "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}" -o ${SPV_FILE}
			DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}"
			COMMENT "Compiling SPIR-V: ${SHADER}"
		)
		list(APPEND SPIRV_OUTPUT_FILES ${SPV_FILE})

		# Add to assets file list (from build directory, not source)
		get_filename_component(SPV_FILENAME ${SPV_FILE} NAME)
		list(APPEND AssetsFiles_Generated "shaders/${SPV_FILENAME}")
	endforeach()

	add_custom_target(compile_shaders DEPENDS ${SPIRV_OUTPUT_FILES})
endif()

# generate assetsmv.pk3
if(MultiConfigGenerator)
	set(OUTFILE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/base/assetsmv.pk3)
else()
	set(OUTFILE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/base/assetsmv.pk3)
endif()

# Create a staging directory for pk3 contents: source assets + compiled shaders
set(ASSETS_STAGING_DIR "${CMAKE_CURRENT_BINARY_DIR}/assets_staging")

# Custom command to prepare the staging directory
add_custom_command(
	OUTPUT "${ASSETS_STAGING_DIR}/staging_marker.txt"
	COMMAND ${CMAKE_COMMAND} -E remove_directory "${ASSETS_STAGING_DIR}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${ASSETS_STAGING_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "${ASSETS_STAGING_DIR}"
	COMMAND ${CMAKE_COMMAND} -E remove_directory "${ASSETS_STAGING_DIR}/shaders/spirv"
	COMMAND ${CMAKE_COMMAND} -E echo "Staging complete" > "${ASSETS_STAGING_DIR}/staging_marker.txt"
	COMMENT "Staging source assets for pk3"
	DEPENDS ${AssetsFiles}
)

# Add commands to copy each compiled shader to staging directory
set(STAGING_MARKER_FILE "${ASSETS_STAGING_DIR}/staging_marker.txt")
if(GLSLANG_VALIDATOR)
	foreach(SPV ${SPIRV_OUTPUT_FILES})
		get_filename_component(SPV_FILENAME "${SPV}" NAME)
		add_custom_command(
			OUTPUT "${ASSETS_STAGING_DIR}/shaders/${SPV_FILENAME}"
			COMMAND ${CMAKE_COMMAND} -E copy "${SPV}" "${ASSETS_STAGING_DIR}/shaders/${SPV_FILENAME}"
			DEPENDS "${SPV}" "${STAGING_MARKER_FILE}"
			COMMENT "Staging compiled shader: ${SPV_FILENAME}"
		)
		list(APPEND STAGED_SHADERS "${ASSETS_STAGING_DIR}/shaders/${SPV_FILENAME}")
	endforeach()
endif()

# Create a marker file to indicate staging is complete
add_custom_command(
	OUTPUT "${ASSETS_STAGING_DIR}/staging_complete.txt"
	COMMAND ${CMAKE_COMMAND} -E echo "Staging complete" > "${ASSETS_STAGING_DIR}/staging_complete.txt"
	DEPENDS ${STAGED_SHADERS}
	COMMENT "Staging complete"
)

include(InstallZIP)
add_zip_command(${CMAKE_CURRENT_BINARY_DIR}/assetsmv.pk3 ${OUTFILE}
	DIR "${ASSETS_STAGING_DIR}"
	FILES "."
	DEPENDS "${ASSETS_STAGING_DIR}/staging_complete.txt"
)

add_custom_target(assetsmv DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/assetsmv.pk3" "${OUTFILE}")

if(GLSLANG_VALIDATOR)
	add_dependencies(assetsmv compile_shaders)
endif()

if(WIN32 OR BuildPortableVersion)
	install(FILES ${OUTFILE} DESTINATION "base")
elseif(APPLE)
	install(FILES ${OUTFILE} DESTINATION "jk2mvmp.app/Contents/MacOS/base")
else()
	install(FILES ${OUTFILE} DESTINATION "share/jk2mv/base")
endif()
