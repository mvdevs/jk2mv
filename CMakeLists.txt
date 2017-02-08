if(MSVC)
	cmake_minimum_required(VERSION 3.1.0) # assembler stuff
else()
	cmake_minimum_required(VERSION 2.8.9)
endif()

if(POLICY CMP0043)
	cmake_policy(SET CMP0043 OLD) # debian wheezy still uses 2.8.9
endif()
if(POLICY CMP0042)
	cmake_policy(SET CMP0042 OLD)
endif()

# For checks in subdirectories
set(InMV TRUE)

# Paths
set(LIBDir "${CMAKE_SOURCE_DIR}/libs")
set(RESDir "${CMAKE_SOURCE_DIR}/res")

# Custom CMake Modules
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_SOURCE_DIR}/tools")
list(INSERT CMAKE_MODULE_PATH 0 "${RESDir}")

# Project name & version
set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE TYPE INTERNAL FORCE)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(ProjectName "JK2MV" CACHE STRING "Project Name")
project(${ProjectName})
set(ProjectURL "https://jk2mv.org")

include(GetGitRevisionDescription)
git_describe(MV_VERSION --tags)
if(NOT MV_VERSION)
	set(MV_VERSION "UNDEFINED")
endif()

message(STATUS "${ProjectName} Version: ${MV_VERSION}")

# Languages
if(MSVC)
	enable_language(C CXX ASM_MASM)
else()
	enable_language(C CXX)
endif()

set(CMAKE_C_STANDARD 90)
set(CMAKE_CXX_STANDARD 11)

# Customizable options
option(BuildPortableVersion "Build portable version (does not read or write files from your user/home directory" ON)
option(BuildMVMP "Whether to create targets for the client (jk2mvmp & jk2mvmenu)" ON)
option(BuildMVDED "Whether to create targets for the dedicated server (jk2mvded)" ON)

if(NOT APPLE AND NOT WIN32)
	option(BuildPackDEB "Whether to create the DEB package" OFF)
	option(BuildPackRPM "Whether to create the RPM package" OFF)
	
	if (BuildPackRPM AND CMAKE_MAJOR_VERSION LESS 3)
		message(WARNING "Generated RPM file will not work on newer distributions")
	endif()
	
	if ((BuildPackDEB OR BuildPackRPM) AND BuildPortableVersion)
		message(FATAL_ERROR "Packaging the portable version is not supported.")
	endif()
endif()

option(LegacyHTTPDownloads "Activate this for legacy JK2MF http downloads. Bad code due to bad implementation in jk2mf" OFF)
option(NTClientWorkaround "Disable loading of NT-Mod pk3 files to prevent crashing the game" ON)

# Internal Libs
if(MSVC)
	set(UseInternalPNG ON)
	set(UseInternalJPEG ON)
	set(UseInternalZLIB ON)
	set(UseInternalMiniZip ON)
	set(UseInternalSDL2 ON)
	set(UseInternalOpenAL ON)
else()
	set(UseInternalSDL2 OFF)
	set(UseInternalOpenAL OFF)

	if(MINGW)
		set(UseInternalPNGDefault OFF)
		set(UseInternalJPEGDefault OFF)
		set(UseInternalZLIBDefault OFF)
		set(UseInternalMiniZipDefault OFF)
	elseif(APPLE)
		set(UseInternalPNGDefault ON)
		set(UseInternalJPEGDefault ON)
		set(UseInternalZLIBDefault ON)
		set(UseInternalMiniZipDefault ON)
	else() # Linux
		set(UseInternalPNGDefault OFF)
		set(UseInternalJPEGDefault OFF)
		set(UseInternalZLIBDefault OFF)
		set(UseInternalMiniZipDefault OFF)
		set(UseInternalSDL2 OFF)
	endif()
	
	option(UseInternalPNG "Whether to use the included libpng instead of a locally installed one" ${UseInternalPNGDefault})
	option(UseInternalJPEG "Whether to use the included libjpeg instead of a locally installed one" ${UseInternalJPEGDefault})
	option(UseInternalZLIB "Whether to use the included zlib instead of a locally installed one" ${UseInternalZLIBDefault})
	option(UseInternalMiniZip "Whether to use the included minizip instead of a locally installed one" ${UseInternalMiniZipDefault})
endif()


if(WIN32)
	set(X86 ON)
	set(GlobalDefines ${GlobalDefines} "WIN32")
	if(CMAKE_SIZEOF_VOID_P MATCHES "8")
		set(GlobalDefines ${GlobalDefines} "ARCH_X86_64")
		set(Architecture "x64")
		set(WIN64 TRUE)
	else()
		set(GlobalDefines ${GlobalDefines} "ARCH_X86")
		set(Architecture "x86")
		set(WIN64 FALSE)
	endif()
elseif(APPLE)
	set(X86 ON)
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "^i.86$")
		set(GlobalDefines ${GlobalDefines} "ARCH_X86")
		set(Architecture "i386")
	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^x86.64$")
		set(GlobalDefines ${GlobalDefines} "ARCH_X86_64")
		set(Architecture "x86_64")
	endif()
else()
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
		set(GlobalDefines ${GlobalDefines} "ARCH_ARM32")
		set(Architecture "arm")
		set(ARM ON)
	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^i.86$")
		set(GlobalDefines ${GlobalDefines} "ARCH_X86")
		set(X86 ON)
		set(Architecture "i386")
	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^x86.64$")
		set(GlobalDefines ${GlobalDefines} "ARCH_X86_64")
		set(X86 ON)
		set(Architecture "amd64")
	else()
		set(Architecture "${CMAKE_SYSTEM_PROCESSOR}")
		set(GlobalDefines ${GlobalDefines} "NO_VM_COMPILED")
	endif()
endif()

message(STATUS "Architecture is ${Architecture}")

# Binary names
set(MVMP "jk2mvmp")
set(MVMENU "jk2mvmenu_${Architecture}")
set(MVDED "jk2mvded")

# OSX install target is more like a finalization step
if(APPLE)
	set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/out/bundle")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
set(CPACK_PACKAGE_DIRECTORY ${CMAKE_BINARY_DIR}/out)

# Compiler defines / settings
if(NOT CMAKE_BUILD_TYPE)
	message(STATUS "No build type selected, default to Release")
	set(CMAKE_BUILD_TYPE "Release")
endif()

set(GlobalDefines ${GlobalDefines} "BOTLIB")
set(GlobalDefines ${GlobalDefines} "_JK2")

if(NOT MSVC)
	# CMake already defines _DEBUG for MSVC.
	set(DebugDefines ${DebugDefines} "_DEBUG")
endif()
set(DebugDefines ${DebugDefines} "DEBUG")
set(ReleaseDefines "FINAL_BUILD" "NDEBUG")

if(MSVC)
	# Warnings
	# 4244 = conversion' conversion from 'type1' to 'type2', possible loss of data
	# 4018 = 'expression' : signed/unsigned mismatch
	# 4996 = 'function': was declared deprecated
	# 4267 = 'var' : conversion from 'size_t' to 'type', possible loss of data
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4244 /wd4018 /wd4996 /wd4267")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4244 /wd4018 /wd4996 /wd4267")
	
	# Multithreaded
	set(CMAKE_CXX_FLAGS_RELEASE "/MT" CACHE TYPE INTERNAL FORCE)
	set(CMAKE_C_FLAGS_RELEASE "/MT" CACHE TYPE INTERNAL FORCE)

	# Platform
	set(GlobalDefines ${GlobalDefines} "UNICODE" "_UNICODE")

	# Optimization
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MP") # multiprocessor compilation
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MP")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2 /Oi /Ot /Ob2 /fp:fast") # maximize speed
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Oi /Ot /Ob2 /fp:fast")

	if(WIN64)
		set(GlobalDefines ${GlobalDefines} "WIN64")
	else()
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /arch:SSE2")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:SSE2")

		set(CMAKE_ASM_MASM_FLAGS "${CMAKE_ASM_MASM_FLAGS} /safeseh") # safe exception handler
	endif()
elseif ((${CMAKE_C_COMPILER_ID} STREQUAL GNU) OR (${CMAKE_C_COMPILER_ID} STREQUAL Clang))
	if(APPLE)
		set(GlobalDefines ${GlobalDefines} "MACOS_X")
	endif()

	# Additional flags for debug configuration
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb3")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb3")

	# we need sse optimizations in debug configuration, too because of the inline assembler which is sse code
	if(X86)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2")
	endif()

	# Optimization
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -ffast-math")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -ffast-math")

	# General
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")

	# Warnings
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-unknown-pragmas")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-unknown-pragmas -Wno-write-strings")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-sign-compare")

	if(${CMAKE_C_COMPILER_ID} STREQUAL GNU)
		if(X86)
			set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -mfpmath=sse")
		endif()

		# Link only libs actually needed
		set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Wl,--as-needed")
	elseif("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
	endif()

	if(${CMAKE_CXX_COMPILER_ID} STREQUAL GNU)
		if(X86)
			set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mfpmath=sse")
		endif()

		# Link only libs actually needed
		# workaround for older gcc versions and c++11 std::thread: explicitly link lpthread
		# https://bugs.launchpad.net/ubuntu/+source/gcc-defaults/+bug/1228201
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--no-as-needed -pthread -lpthread -Wl,--as-needed")

		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility-inlines-hidden")

		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wformat=2 -Wno-unused-but-set-variable")
	elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-char-subscripts -Wno-self-assign")
	endif()
else()
	message(FATAL_ERROR "Unsupported compiler")
endif()

if(LegacyHTTPDownloads)
	set(GlobalDefines ${GlobalDefines} "MV_MFDOWNLOADS")
endif()

if(NTClientWorkaround)
	set(GlobalDefines ${GlobalDefines} "NTCLIENT_WORKAROUND")
endif()

if(BuildPortableVersion)
	set(GlobalDefines ${GlobalDefines} "PORTABLE")
else()
	set(GlobalDefines ${GlobalDefines} "INSTALLED")
endif()

# Libraries
if(UseInternalZLIB)
	set(GlobalDefines ${GlobalDefines} "INTERNAL_ZLIB")
	add_subdirectory(libs/zlib)
endif()

if(UseInternalMiniZip)
	set(GlobalDefines ${GlobalDefines} "INTERNAL_MINIZIP")
	add_subdirectory(libs/minizip)
endif()

if(UseInternalJPEG)
	set(GlobalDefines ${GlobalDefines} "INTERNAL_JPEG")
	add_subdirectory(libs/jpeg-9a)
endif()

if(UseInternalPNG)
	set(GlobalDefines ${GlobalDefines} "INTERNAL_PNG")
	add_subdirectory(libs/libpng)
endif()

add_subdirectory(libs/mongoose)

# Package
set(CPACK_PACKAGE_NAME ${ProjectName})
set(CPACK_PACKAGE_FILE_NAME "jk2mv-v${MV_VERSION}")
set(CPACK_PACKAGE_VENDOR "jk2mv.org")
set(CPACK_PACKAGE_CONTACT "jk2mv.org <contact@jk2mv.org>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Jedi Knight II: Multi Version")
set(CPACK_PACKAGE_VERSION "${MV_VERSION}")

if(WIN32)
	set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-win32-${Architecture}")

	if(BuildPortableVersion)
		set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-portable")

		set(CPACK_GENERATOR "ZIP")
	else()
		set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-installer")

		set(CPACK_GENERATOR "NSIS")
		set(CPACK_NSIS_URL_INFO_ABOUT ${ProjectURL})

		configure_file(
			${RESDir}/NSIS.definitions.nsh.in
			${CMAKE_BINARY_DIR}/res/NSIS.definitions.nsh
		)
		string(REGEX REPLACE "/" "\\\\\\\\" CPACK_NSIS_INCLFILE "${CMAKE_BINARY_DIR}/res/NSIS.definitions.nsh")
	endif()
	
	include(CPack)
elseif(APPLE)
	set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-macosx-${Architecture}")

	set(CPACK_GENERATOR "DragNDrop")
	set(CPACK_DMG_FORMAT "UDBZ")

	include(CPack)
else()
	set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-linux-${Architecture}")

	# DEB
	if(BuildPackDEB)
		set(CPACK_GENERATOR ${CPACK_GENERATOR} "DEB")
		set(CPACK_DEBIAN_PACKAGE_NAME "jk2mv") # debian packages are lowercase
		set(CPACK_DEBIAN_PACKAGE_SECTION "games")
		set(CPACK_DEBIAN_PACKAGE_HOMEPAGE ${ProjectURL})
		set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE ${Architecture})
		set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.0), libgcc1 (>= 1:4.0), libstdc++6 (>= 4.0), libsdl2-2.0-0 (>= 2.0), libopenal1 (>= 1:1.0)")
	endif()

	# RPM
	if(BuildPackRPM)
		set(CPACK_GENERATOR ${CPACK_GENERATOR} "RPM")
		set(CPACK_RPM_PACKAGE_NAME "jk2mv")
		set(CPACK_RPM_PACKAGE_DESCRIPTION "JK2MV (Multi Version) is a modification for Jedi Knight II: Jedi Outcast. It supports all three game versions and comes with various features and optimizations.")
		set(CPACK_RPM_PACKAGE_URL ${ProjectURL})
		set(CPACK_RPM_PACKAGE_LICENSE "GPLv2")
		set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
		set(CPACK_RPM_PACKAGE_REQUIRES "glibc, libgcc, libstdc++, SDL2, openal-soft")
		set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION /usr/share/applications /usr/share/pixmaps)
	endif()
	
	if(BuildPackDEB OR BuildPackRPM)
		include(CPack)
	endif()
endif()

add_subdirectory(src)
