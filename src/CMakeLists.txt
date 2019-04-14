# Make sure the user is not executing this script directly
if(NOT InMV)
	message(FATAL_ERROR "Use the top-level cmake script!")
endif(NOT InMV)

if(BuildMVSDK)
	add_subdirectory("mvsdk")
endif()

if (BuildMVMP OR BuildMVDED)
	# Libraries
	
	# mongoose
	set(MVMPDEDLibraries ${MVMPDEDLibraries} ${MONGOOSE_LIBRARIES})
	set(MVMPDEDIncludeDirs ${MVMPDEDIncludeDirs} ${MONGOOSE_INCLUDE_DIRS})
	
	if(WIN32)
		set(MVMPDEDLibraries ${MVMPDEDLibraries} "winmm" "ws2_32")
	else()
		find_package(Threads)
		set(MVMPDEDLibraries ${MVMPDEDLibraries} ${CMAKE_THREAD_LIBS_INIT})
		set(MVMPDEDLibraries ${MVMPDEDLibraries} ${CMAKE_DL_LIBS})
	endif()

	# zlib
	if(NOT UseInternalZLIB)
		find_package(ZLIB)
		if(NOT ZLIB_FOUND)
			message(FATAL_ERROR "zlib dev files not found! Install it or set UseInternalZLIB ON")
		endif()
	endif()
	set(MVMPDEDIncludeDirs ${MVMPDEDIncludeDirs} ${ZLIB_INCLUDE_DIRS})
	
	# minizip
	if(NOT UseInternalMiniZip)
		find_package(MiniZip)
		if(NOT MINIZIP_FOUND)
			message(FATAL_ERROR "minizip dev files not found! Install it or set UseInternalMiniZip ON")
		endif()
	endif()
	set(MVMPDEDIncludeDirs ${MVMPDEDIncludeDirs} ${MINIZIP_INCLUDE_DIRS})
	
	# Source
	set(BotlibFiles
		"botlib/aasfile.h"
		"botlib/be_aas_bsp.h"
		"botlib/be_aas_cluster.h"
		"botlib/be_aas_debug.h"
		"botlib/be_aas_def.h"
		"botlib/be_aas_entity.h"
		"botlib/be_aas_file.h"
		"botlib/be_aas_funcs.h"
		"botlib/be_aas_main.h"
		"botlib/be_aas_move.h"
		"botlib/be_aas_optimize.h"
		"botlib/be_aas_reach.h"
		"botlib/be_aas_route.h"
		"botlib/be_aas_routealt.h"
		"botlib/be_aas_sample.h"
		"botlib/be_ai_weight.h"
		"botlib/be_interface.h"
		"botlib/l_crc.h"
		"botlib/l_libvar.h"
		"botlib/l_log.h"
		"botlib/l_memory.h"
		"botlib/l_precomp.h"
		"botlib/l_script.h"
		"botlib/l_struct.h"
		"botlib/l_utils.h"

		"botlib/be_aas_bspq3.cpp"
		"botlib/be_aas_cluster.cpp"
		"botlib/be_aas_debug.cpp"
		"botlib/be_aas_entity.cpp"
		"botlib/be_aas_file.cpp"
		"botlib/be_aas_main.cpp"
		"botlib/be_aas_move.cpp"
		"botlib/be_aas_optimize.cpp"
		"botlib/be_aas_reach.cpp"
		"botlib/be_aas_route.cpp"
		"botlib/be_aas_routealt.cpp"
		"botlib/be_aas_sample.cpp"
		"botlib/be_ai_char.cpp"
		"botlib/be_ai_chat.cpp"
		"botlib/be_ai_gen.cpp"
		"botlib/be_ai_goal.cpp"
		"botlib/be_ai_move.cpp"
		"botlib/be_ai_weap.cpp"
		"botlib/be_ai_weight.cpp"
		"botlib/be_ea.cpp"
		"botlib/be_interface.cpp"
		"botlib/l_crc.cpp"
		"botlib/l_libvar.cpp"
		"botlib/l_log.cpp"
		"botlib/l_memory.cpp"
		"botlib/l_precomp.cpp"
		"botlib/l_script.cpp"
		"botlib/l_struct.cpp"
	)
	source_group("botlib" FILES ${BotlibFiles})
	set(MVMPDEDFiles ${MVMPDEDFiles} ${BotlibFiles})

	set(MVMPDEDServerFiles
		"server/server.h"

		"server/sv_bot.cpp"
		"server/sv_ccmds.cpp"
		"server/sv_client.cpp"
		"server/sv_game.cpp"
		"server/sv_init.cpp"
		"server/sv_main.cpp"
		"server/sv_net_chan.cpp"
		"server/sv_snapshot.cpp"
		"server/sv_world.cpp"
	)
	source_group("server" FILES ${MVMPDEDServerFiles})
	set(MVMPDEDFiles ${MVMPDEDFiles} ${MVMPDEDServerFiles})

	set(MVMPDEDGhoulFiles
		"ghoul2/G2.h"
		"ghoul2/G2_local.h"
		"ghoul2/ghoul2_shared.h"

		"ghoul2/G2_API.cpp"
		"ghoul2/G2_bolts.cpp"
		"ghoul2/G2_bones.cpp"
		"ghoul2/G2_misc.cpp"
		"ghoul2/G2_surfaces.cpp"
	)
	source_group("ghoul2" FILES ${MVMPDEDGhoulFiles})
	set(MVMPDEDFiles ${MVMPDEDFiles} ${MVMPDEDGhoulFiles})

	set(MVMPDEDCommonFiles
		"qcommon/GenericParser2.h"
		"qcommon/INetProfile.h"
		"qcommon/MiniHeap.h"
		"qcommon/RoffSystem.h"
		"qcommon/chash.h"
		"qcommon/cm_local.h"
		"qcommon/cm_patch.h"
		"qcommon/cm_polylib.h"
		"qcommon/cm_public.h"
		"qcommon/hstring.h"
		"qcommon/qcommon.h"
		"qcommon/qfiles.h"
		"qcommon/q_shared.h"
		"qcommon/strip.h"
		"qcommon/tags.h"
		"qcommon/vm_local.h"

		"qcommon/CNetProfile.cpp"
		"qcommon/GenericParser2.cpp"
		"qcommon/RoffSystem.cpp"
		"qcommon/cm_load.cpp"
		"qcommon/cm_patch.cpp"
		"qcommon/cm_polylib.cpp"
		"qcommon/cm_test.cpp"
		"qcommon/cm_trace.cpp"
		"qcommon/cmd.cpp"
		"qcommon/common.cpp"
		"qcommon/cvar.cpp"
		"qcommon/files.cpp"
		"qcommon/hstring.cpp"
		"qcommon/huffman.cpp"
		"qcommon/md4.cpp"
		"qcommon/msg.cpp"
		"qcommon/net_chan.cpp"
		"qcommon/net_ip.cpp"
		"qcommon/net_http.cpp"
		"qcommon/q_math.cpp"
		"qcommon/q_shared.cpp"
		"qcommon/strip.cpp"
		"qcommon/vm.cpp"
		"qcommon/vm_interpreted.cpp"
	)

	if(ARCH_X86)
		set(MVMPDEDCommonFiles ${MVMPDEDCommonFiles}
			"qcommon/vm_x86.cpp"
		)
	elseif(ARCH_ARM)
		set(MVMPDEDCommonFiles ${MVMPDEDCommonFiles}
			"qcommon/vm_arm.cpp"
		)
	endif()
	source_group("qcommon" FILES ${MVMPDEDCommonFiles})
	set(MVMPDEDFiles ${MVMPDEDFiles} ${MVMPDEDCommonFiles})

	set(MVMPDEDRendererFiles
		"renderer/matcomp.h"
		"renderer/mdx_format.h"
		"renderer/qgl.h"
		"renderer/tr_font.h"
		"renderer/tr_local.h"
		"renderer/tr_public.h"
		"renderer/tr_quicksprite.h"

		"renderer/matcomp.c"
		"renderer/tr_backend.cpp"
		"renderer/tr_ghoul2.cpp"
		"renderer/tr_image.cpp"
		"renderer/tr_init.cpp"
		"renderer/tr_main.cpp"
		"renderer/tr_mesh.cpp"
		"renderer/tr_model.cpp"
		"renderer/tr_shader.cpp"
	)
	source_group("renderer" FILES ${MVMPDEDRendererFiles})
	set(MVMPDEDFiles ${MVMPDEDFiles} ${MVMPDEDRendererFiles})

	# Crashlog utilities

	if(MSVC)
		set(MVMPDEDWIN32Files
			"${LIBDir}/StackWalker/include/StackWalker.h"

			"${LIBDir}/StackWalker/StackWalker.cpp"

			"win32/q3asm.asm"
		)
		source_group("win32" FILES ${MVMPDEDWIN32Files})
		set(MVMPDEDIncludeDirs ${MVMPDEDIncludeDirs} "${LIBDir}/StackWalker/include")
		set(MVMPDEDFiles ${MVMPDEDFiles} ${MVMPDEDWIN32Files})
	endif()

	if(NOT WIN32)
		find_package(Backtrace)
		if(Backtrace_FOUND)
			set(MVMPDEDIncludeDirs ${MVMPDEDIncludeDirs} ${Backtrace_INCLUDE_DIRS})
			set(MVMPDEDLibraries ${MVMPDEDLibraries} ${Backtrace_LIBRARIES})
		endif()
	endif()

	if(WIN32)
		set(MVMPDEDSysFiles
			"sys/sys_public.h"
			"sys/sys_local.h"

			"sys/con_win32.cpp"
			"sys/con_log.cpp"
			"sys/sys_main.cpp"
			"sys/sys_event.cpp"
			"sys/sys_win32.cpp"
		)
	else()
		set(MVMPDEDSysFiles
			"sys/con_local.h"
			"sys/sys_public.h"
			"sys/sys_local.h"

			"sys/con_log.cpp"
			"sys/con_tty.cpp"
			"sys/sys_main.cpp"
			"sys/sys_event.cpp"
			"sys/sys_unix.cpp"
		)
	endif()
	
	source_group("sys" FILES ${MVMPDEDSysFiles})
	set(MVMPDEDFiles ${MVMPDEDFiles} ${MVMPDEDSysFiles})
	
	set(MVMPDEDApiFiles
		"api/mvapi.h"
		"api/mvmenu.h"
	)
	source_group("api" FILES ${MVMPDEDApiFiles})
	set(MVMPDEDFiles ${MVMPDEDFiles} ${MVMPDEDApiFiles})
endif()

# mv_setup.h preparation
set(MVGLOBALIncludeDirs ${MVGLOBALIncludeDirs} ${CMAKE_BINARY_DIR})
configure_file("mv_setup.h.in" "${CMAKE_BINARY_DIR}/mv_setup.h" @ONLY)

# jk2mvmp
if (BuildMVMP)
	set(MVMPFiles ${MVMPDEDFiles})
	set(MVMPIncludeDirs ${MVGLOBALIncludeDirs} ${MVMPDEDIncludeDirs})
	set(MVMPLibraries ${MVMPDEDLibraries})

	# Libraries
	
	# OpenGL
	find_package(OpenGL)
	if(NOT OPENGL_FOUND)
		message(FATAL_ERROR "OpenGL dev files not found!")
	endif()
	set(MVMPIncludeDirs ${MVMPIncludeDirs} ${OPENGL_INCLUDE_DIR})
	set(MVMPLibraries ${MVMPLibraries} ${OPENGL_gl_LIBRARY})

	if(UseInternalOpenAL)
		# OpenAL
		set(MVMPLibraries ${MVMPLibraries}
			${LIBDir}/OpenAL/lib/${Architecture}/OpenAL32.lib
		)
		
		set(MVMPIncludeDirs ${MVMPIncludeDirs} "${LIBDir}/OpenAL/include")
	else()
		# OpenAL
		find_package(OpenAL)
		if(NOT OPENAL_FOUND)
			message(FATAL_ERROR "OpenAL dev files not found!")
		endif()
		set(MVMPIncludeDirs ${MVMPIncludeDirs} ${OPENAL_INCLUDE_DIR})
		set(MVMPLibraries ${MVMPLibraries} ${OPENAL_LIBRARY})
	endif()
	
	# SDL2
	if(UseInternalSDL2)
		set(MVMPLibraries ${MVMPLibraries}
			${LIBDir}/SDL2/lib/${Architecture}/SDL2main.lib
			${LIBDir}/SDL2/lib/${Architecture}/SDL2.lib
		)
		
		set(MVMPIncludeDirs ${MVMPIncludeDirs} "${LIBDir}/SDL2/include")
	else()
		find_package(SDL2)
		if(NOT SDL2_FOUND)
			message(FATAL_ERROR "libsdl2 dev files not found!")
		endif()
		set(MVMPIncludeDirs ${MVMPIncludeDirs} ${SDL2_INCLUDE_DIR})
		set(MVMPLibraries ${MVMPLibraries} ${SDL2_LIBRARY})
	endif()

	# EAX
	set(MVMPIncludeDirs ${MVMPIncludeDirs} "${LIBDir}/eax")

	# JPEG
	if(NOT UseInternalJPEG)
		find_package(JPEG)
		if(NOT JPEG_FOUND)
			message(FATAL_ERROR "libjpeg dev files not found! Install it or set UseInternalJPEG ON")
		endif()
	endif()
	set(MVMPIncludeDirs ${MVMPIncludeDirs} ${JPEG_INCLUDE_DIR})

	# PNG
	if(NOT UseInternalPNG)
		find_package(PNG)
		if(NOT PNG_FOUND)
			message(FATAL_ERROR "libpng dev files not found! Install it or set UseInternalPNG ON")
		endif()
	endif()
	set(MVMPIncludeDirs ${MVMPIncludeDirs} ${PNG_INCLUDE_DIRS})

	# Source
	set(MVMPClientFiles
		"client/FXExport.h"
		"client/FxPrimitives.h"
		"client/FxScheduler.h"
		"client/FxSystem.h"
		"client/FxUtil.h"
		"client/client.h"
		"client/keys.h"
		"client/snd_local.h"
		"client/snd_mp3.h"
		"client/snd_public.h"

		"client/FXExport.cpp"
		"client/FxPrimitives.cpp"
		"client/FxScheduler.cpp"
		"client/FxSystem.cpp"
		"client/FxTemplate.cpp"
		"client/FxUtil.cpp"
		"client/cl_avi.cpp"
		"client/cl_cgame.cpp"
		"client/cl_cin.cpp"
		"client/cl_console.cpp"
		"client/cl_demos_auto.cpp"
		"client/cl_input.cpp"
		"client/cl_keys.cpp"
		"client/cl_main.cpp"
		"client/cl_net_chan.cpp"
		"client/cl_parse.cpp"
		"client/cl_scrn.cpp"
		"client/cl_ui.cpp"
		"client/snd_dma.cpp"
		"client/snd_mem.cpp"
		"client/snd_mix.cpp"
		"client/snd_mp3.cpp"
	)
	source_group("client" FILES ${MVMPClientFiles})
	set(MVMPFiles ${MVMPFiles} ${MVMPClientFiles})

	set(MVMPMP3Files
		"${LIBDir}/mp3/config.h"
		"${LIBDir}/mp3/copyright.h"
		"${LIBDir}/mp3/htable.h"
		"${LIBDir}/mp3/jdw.h"
		"${LIBDir}/mp3/l3.h"
		"${LIBDir}/mp3/mhead.h"
		"${LIBDir}/mp3/mp3struct.h"
		"${LIBDir}/mp3/port.h"
		"${LIBDir}/mp3/small_header.h"
		"${LIBDir}/mp3/tableawd.h"

		"${LIBDir}/mp3/cdct.c"
		"${LIBDir}/mp3/csbt.c"
		"${LIBDir}/mp3/csbtb.c"
		"${LIBDir}/mp3/csbtl3.c"
		"${LIBDir}/mp3/cup.c"
		"${LIBDir}/mp3/cupini.c"
		"${LIBDir}/mp3/cupl1.c"
		"${LIBDir}/mp3/cupl3.c"
		"${LIBDir}/mp3/cwin.c"
		"${LIBDir}/mp3/cwinb.c"
		"${LIBDir}/mp3/cwinm.c"
		"${LIBDir}/mp3/hwin.c"
		"${LIBDir}/mp3/l3dq.c"
		"${LIBDir}/mp3/l3init.c"
		"${LIBDir}/mp3/mdct.c"
		"${LIBDir}/mp3/mhead.c"
		"${LIBDir}/mp3/msis.c"
		"${LIBDir}/mp3/towave.c"
		"${LIBDir}/mp3/uph.c"
		"${LIBDir}/mp3/upsf.c"
		"${LIBDir}/mp3/wavep.c"
	)
	source_group("mp3" FILES ${MVMPMP3Files})
	set(MVMPIncludeDirs ${MVMPIncludeDirs} "${LIBDir}/mp3")
	set(MVMPFiles ${MVMPFiles} ${MVMPMP3Files})

	set(MVMPRendererFiles
		"renderer/glext.h"
		"renderer/tr_WorldEffects.h"

		"renderer/tr_WorldEffects.cpp"
		"renderer/tr_animation.cpp"
		"renderer/tr_bsp.cpp"
		"renderer/tr_cmds.cpp"
		"renderer/tr_curve.cpp"
		"renderer/tr_flares.cpp"
		"renderer/tr_font.cpp"
		"renderer/tr_light.cpp"
		"renderer/tr_marks.cpp"
		"renderer/tr_noise.cpp"
		"renderer/tr_quicksprite.cpp"
		"renderer/tr_scene.cpp"
		"renderer/tr_shade.cpp"
		"renderer/tr_shade_calc.cpp"
		"renderer/tr_shadows.cpp"
		"renderer/tr_sky.cpp"
		"renderer/tr_surface.cpp"
		"renderer/tr_surfacesprites.cpp"
		"renderer/tr_world.cpp"
	)
	source_group("renderer" FILES ${MVMPRendererFiles})
	set(MVMPFiles ${MVMPFiles} ${MVMPRendererFiles})

	if(WIN32)
		set(MVMPWIN32Files
			"win32/resource.h"
			"win32/mv.rc"
			"win32/dpi_scaling.manifest"
		)
		source_group("win32" FILES ${MVMPWIN32Files})
		set(MVMPFiles ${MVMPFiles} ${MVMPWIN32Files})
	endif()

	set(MVMPSDLFiles
		"sdl/sdl_input.cpp"
		"sdl/sdl_sound.cpp"
		"sdl/sdl_window.cpp"
	)
	source_group("sdl" FILES ${MVMPSDLFiles})
	set(MVMPFiles ${MVMPFiles} ${MVMPSDLFiles})

	if ((${CMAKE_C_COMPILER_ID} STREQUAL GNU) OR (${CMAKE_C_COMPILER_ID} STREQUAL Clang))
		set(LibFiles ${MVMPMP3Files})
		set_source_files_properties(${LibFiles} PROPERTIES COMPILE_FLAGS "-w")
	endif()

	# Project
	if(WIN32)
		add_executable(${MVMP} WIN32 ${MVMPFiles})
	elseif(APPLE AND NOT BuildPortableVersion)
		set(MACOSX_BUNDLE_BUNDLE_NAME ${ProjectName})
		set(MACOSX_BUNDLE_DISPLAY_NAME ${ProjectName})
		set(MACOSX_BUNDLE_ICON_FILE "mv.icns")
		set(MVMPFiles ${MVMPFiles} ${RESDir}/mv.icns)

		add_executable(${MVMP} MACOSX_BUNDLE ${MVMPFiles})
		set_source_files_properties(${RESDir}/mv.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
		set_target_properties(${MVMP} PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${RESDir}/mv.plist")
	else()
		add_executable(${MVMP} ${MVMPFiles})
	endif()

	if(MSVC)
		# copy over openal & sdl dll
		add_custom_command(TARGET ${MVMP} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${LIBDir}/OpenAL/bin/${Architecture}/OpenAL32.dll"
			$<TARGET_FILE_DIR:${MVMP}>
		)
		
		add_custom_command(TARGET ${MVMP} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${LIBDir}/SDL2/bin/${Architecture}/SDL2.dll"
			$<TARGET_FILE_DIR:${MVMP}>
		)
	endif()

	target_compile_definitions(${MVMP}
		PRIVATE ${GlobalDefines}
		PRIVATE $<$<CONFIG:Debug>:${DebugDefines}>
		PRIVATE $<$<CONFIG:Release>:${ReleaseDefines}>
		PRIVATE $<$<CONFIG:RelWithDebInfo>:${ReleaseDefines}>
	)

	set_target_properties(${MVMP} PROPERTIES CXX_STANDARD 11)
	set_target_properties(${MVMP} PROPERTIES INCLUDE_DIRECTORIES "${MVMPIncludeDirs}")
	target_link_libraries(${MVMP} ${MVMPLibraries} ${JPEG_LIBRARIES} ${PNG_LIBRARIES} ${MINIZIP_LIBRARIES} ${ZLIB_LIBRARIES})
	set_target_properties(${MVMP} PROPERTIES PROJECT_LABEL "jk2mvmp")

	# installation
	if(MSVC)
		install(TARGETS ${MVMP} RUNTIME DESTINATION ".")
		install(FILES $<TARGET_PDB_FILE:${MVMP}> DESTINATION "." OPTIONAL)
		install(FILES "${LIBDir}/OpenAL/bin/${Architecture}/OpenAL32.dll" DESTINATION ".")
		install(FILES "${LIBDir}/SDL2/bin/${Architecture}/SDL2.dll" DESTINATION ".")
		if(NOT BuildPortableVersion)
			install(FILES "${RESDir}/mv.ico" DESTINATION ".")
		endif()
	elseif(APPLE AND NOT BuildPortableVersion)
		install(TARGETS ${MVMP} BUNDLE DESTINATION ".")

		# copy over frameworks into the app
		# for this to work it's required that the SDL2.framework is under ~/Library/Frameworks
		# and SDL2 has to be compiled without @rpath (builds from website are with @rpath)
		SET(APPS "\${CMAKE_INSTALL_PREFIX}/${MVMP}.app")
		install(CODE "
			include(BundleUtilities)
			fixup_bundle(\"${APPS}\" \"\" \"\")
		")
	else()
		if(BuildPortableVersion)
			install(TARGETS ${MVMP} RUNTIME DESTINATION ".")
		else()
			install(TARGETS ${MVMP} RUNTIME DESTINATION "bin")
			install(FILES ${RESDir}/org.mvdevs.jk2mv.desktop DESTINATION "share/applications")
			install(FILES ${RESDir}/org.mvdevs.jk2mv.48.png RENAME org.mvdevs.jk2mv.png DESTINATION "share/icons/hicolor/48x48/apps")
			install(FILES ${RESDir}/org.mvdevs.jk2mv.256.png RENAME org.mvdevs.jk2mv.png DESTINATION "share/icons/hicolor/256x256/apps")
			install(FILES ${RESDir}/org.mvdevs.jk2mv.appdata.xml DESTINATION "share/metainfo")
		endif()
	endif()

	# jk2mvmenu and assetsmv are required to run JK2MV
	add_dependencies(${MVMP} ${MVMENU} assetsmv)

	if(BuildMVSDK)
		add_dependencies(${MVMP} assetsmv2)
	endif()

	# mvmenu is now built from source conainted in the mvsdk submodule
	set(MVMENUDefines "GAME_EXPORTS")
	set(MVMENUDefines "JK2MV_MENU")
	set(MVMENUIncludeDirs ${MVGLOBALIncludeDirs})

	set(MVMENUFiles
		"mvsdk/code/api/mvapi.h"

		"mvsdk/code/ui/keycodes.h"
		"mvsdk/code/ui/ui_force.h"
		"mvsdk/code/ui/ui_local.h"
		"mvsdk/code/ui/ui_public.h"
		"mvsdk/code/ui/ui_shared.h"
		"mvsdk/code/ui/ui_multiversion.h"
		"mvsdk/code/ui/ui_multiversion_syscalls.h"

		"mvsdk/code/game/bg_weapons.h"
		"mvsdk/code/game/q_shared.h"
		"mvsdk/code/game/bg_multiversion.h"

		"mvsdk/code/ui/ui_main.c" # keep this on top (qvm compiler needs VM_Main as the first compiled function)
		"mvsdk/code/ui/ui_atoms.c"
		"mvsdk/code/ui/ui_force.c"
		"mvsdk/code/ui/ui_gameinfo.c"
		"mvsdk/code/ui/ui_shared.c"
		"mvsdk/code/ui/ui_multiversion.c"

		"mvsdk/code/game/bg_misc.c"
		"mvsdk/code/game/bg_weapons.c"
		"mvsdk/code/game/q_math.c"
		"mvsdk/code/game/q_shared.c"
		"mvsdk/code/ui/ui_multiversion_syscalls.c"
		"mvsdk/code/game/bg_multiversion.c"

		"mvsdk/code/ui/ui_syscalls.c"
	)

	add_library(${MVMENU} SHARED ${MVMENUFiles})


	set_target_properties(${MVMENU} PROPERTIES PREFIX "") # remove "lib" prefix for library files

	target_compile_definitions(${MVMENU}
		PRIVATE ${GlobalDefines} ${MVMENUDefines}
		PRIVATE $<$<CONFIG:Debug>:${DebugDefines}>
		PRIVATE $<$<CONFIG:Release>:${ReleaseDefines}>
		PRIVATE $<$<CONFIG:RelWithDebInfo>:${ReleaseDefines}>
	)

	set_target_properties(${MVMENU} PROPERTIES C_STANDARD 90)
	set_target_properties(${MVMENU} PROPERTIES INCLUDE_DIRECTORIES "${MVMENUIncludeDirs}")
	set_target_properties(${MVMENU} PROPERTIES PROJECT_LABEL "jk2mvmenu")

	if(BuildPortableVersion)
		if(WIN32)
			install(TARGETS ${MVMENU} RUNTIME DESTINATION ".")
		else()
			install(TARGETS ${MVMENU} LIBRARY DESTINATION ".")
		endif()
	else()
		if(WIN32)
			install(TARGETS ${MVMENU} RUNTIME DESTINATION ".")
			install(FILES $<TARGET_PDB_FILE:${MVMENU}> DESTINATION "." OPTIONAL)
		elseif(APPLE)
			install(TARGETS ${MVMENU} LIBRARY DESTINATION "${MVMP}.app/Contents/MacOS")
		else()
			install(TARGETS ${MVMENU} LIBRARY DESTINATION "lib")
		endif()
	endif()
endif()

# jk2mvded
if (BuildMVDED)
	set(MVDEDFiles ${MVMPDEDFiles})
	set(MVDEDIncludeDirs ${MVGLOBALIncludeDirs} ${MVMPDEDIncludeDirs})
	set(MVDEDLibraries ${MVMPDEDLibraries})

	# Source
	set(MVDEDNullFiles
		"null/null_client.cpp"
		"null/null_input.cpp"
		"null/null_renderer.cpp"
		"null/null_snddma.cpp"
	)
	source_group("null" FILES ${MVDEDNullFiles})
	set(MVDEDFiles ${MVDEDFiles} ${MVDEDNullFiles})

	# Defines
	set(MVDEDDefines "DEDICATED")

	# Project
	add_executable(${MVDED} ${MVDEDFiles})
	
	target_compile_definitions(${MVDED}
		PRIVATE ${GlobalDefines} ${MVDEDDefines}
		PRIVATE $<$<CONFIG:Debug>:${DebugDefines}>
		PRIVATE $<$<CONFIG:Release>:${ReleaseDefines}>
		PRIVATE $<$<CONFIG:RelWithDebInfo>:${ReleaseDefines}>
	)

	set_target_properties(${MVDED} PROPERTIES CXX_STANDARD 11)
	set_target_properties(${MVDED} PROPERTIES INCLUDE_DIRECTORIES "${MVDEDIncludeDirs}")
	target_link_libraries(${MVDED} ${MVDEDLibraries} ${MINIZIP_LIBRARIES} ${ZLIB_LIBRARIES})
	set_target_properties(${MVDED} PROPERTIES PROJECT_LABEL "jk2mvded")

	add_dependencies(${MVDED} assetsmv)

	if(BuildMVSDK)
		add_dependencies(${MVDED} assetsmv2)
	endif()

	# installation
	if(WIN32 OR APPLE OR BuildPortableVersion)
		install(TARGETS ${MVDED} RUNTIME DESTINATION ".")
		if (WIN32)
			install(FILES $<TARGET_PDB_FILE:${MVDED}> DESTINATION "." OPTIONAL)
		endif()
	else()
		install(TARGETS ${MVDED} RUNTIME DESTINATION "bin")
	endif()
endif()
