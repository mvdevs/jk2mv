if(WIN32)
	cmake_minimum_required(VERSION 3.1.0) # assembler stuff
else()
	cmake_minimum_required(VERSION 2.8.8)
endif()

if(POLICY CMP0043)
	cmake_policy(SET CMP0043 OLD) # debian wheezy still uses 2.8.9
endif()
if(POLICY CMP0042)
	cmake_policy(SET CMP0042 OLD)
endif()

# For checks in subdirectories
set(InMV TRUE)

set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE TYPE INTERNAL FORCE)

# Project name & version
set(ProjectName "JK2MV" CACHE STRING "Project Name")
project(${ProjectName})

set(MV_MAJOR_VERSION 1)
set(MV_MINOR_VERSION 0)
set(MV_PATCH_VERSION 2)
set(MV_EXT_VERSION "")
set(MV_ECMP_VERSION 8) # increasing number to detect new version without parsing stuff like 1.0b1
set(MV_VERSION "${MV_MAJOR_VERSION}.${MV_MINOR_VERSION}.${MV_PATCH_VERSION}${MV_EXT_VERSION}")

# Languages
if(WIN32)
	enable_language(C CXX ASM_MASM)
else()
	enable_language(C CXX)
endif()

# Customizable options
option(BuildPortableVersion "Build portable version (does not read or write files from your user/home directory" OFF)
option(BuildMVMP "Whether to create projects for the client (jk2mvmp & jk2mvmenu)" ON)
option(BuildMVDED "Whether to create projects for the dedicated server (jk2mvded)" ON)

if(WIN32)
	option(BuildPack "Whether to create the installer package" OFF)
elseif(APPLE)
	option(BuildPack "Whether to create the app package" ON)
endif()

if(WIN32)
	set(UseInternalPNG ON)
elseif(APPLE)
	option(UseInternalPNG "Whether to use the included libpng instead of a locally installed one" ON)
else()
	option(UseInternalPNG "Whether to use the included libpng instead of a locally installed one" OFF)
endif()

if(WIN32)
	set(UseInternalJPEG ON)
elseif(APPLE)
	option(UseInternalJPEG "Whether to use the included libjpeg instead of a locally installed one" ON)
else()
	option(UseInternalJPEG "Whether to use the included libjpeg instead of a locally installed one" OFF)
endif()

if(WIN32)
	set(UseInternalZLIB ON)
else()
	option(UseInternalZLIB "Whether to use the included zlib instead of a locally installed one" OFF)
endif()

if(WIN32)
	set(UseInternalCURL ON)
else()
	option(UseInternalCURL "Whether to use the included libcurl instead of a locally installed one" OFF)
endif()

# Paths
set(SRCDir "${CMAKE_SOURCE_DIR}/src")
set(LIBDir "${CMAKE_SOURCE_DIR}/libs")
set(RESDir "${CMAKE_SOURCE_DIR}/res")

# Custom CMake Modules
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_SOURCE_DIR}/tools")
list(INSERT CMAKE_MODULE_PATH 0 "${RESDir}")

# Architecture
set(CMAKE_OSX_ARCHITECTURES "i386" "x86_64") # always building both on osx

if(CMAKE_SIZEOF_VOID_P MATCHES "8")
	set(ARCH_BITS 64)
	if(WIN32)
		set(Architecture "x64")
		set(WIN64 TRUE)
	elseif(APPLE)
		set(Architecture "universal")
	else()
		set(Architecture "amd64")
	endif()

	if(NOT APPLE)
		set(GlobalDefines ${GlobalDefines} "_M_X64")
	endif()
else()
	set(ARCH_BITS 32)
	if(WIN32)
		set(Architecture "x86")
		set(WIN64 FALSE)
	elseif(APPLE)
		set(Architecture "universal")
	else()
		set(Architecture "i386")
	endif()

	if(NOT APPLE)
		set(GlobalDefines ${GlobalDefines} "_M_IX86")
	endif()
endif()

message(STATUS "Architecture is ${Architecture}")

# Binary names
if(NOT APPLE)
	set(MVMP "jk2mvmp_${Architecture}")
	set(MVMENU "jk2mvmenu_${Architecture}")
	set(MVDED "jk2mvded_${Architecture}")
else()
	set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/out/install")

	set(MVMP "jk2mvmp")
	set(MVMENU "jk2mvmenu")
	set(MVDED "jk2mvded")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
set(CPACK_PACKAGE_DIRECTORY ${CMAKE_BINARY_DIR}/out)

# Compiler defines / settings
set(GlobalDefines ${GlobalDefines} "BOTLIB")
set(GlobalDefines ${GlobalDefines} "_JK2")

if(NOT MSVC)
	# CMake already defines _DEBUG for MSVC.
	set(DebugDefines ${DebugDefines} "_DEBUG")
endif()
set(DebugDefines ${DebugDefines} "DEBUG")

set(ReleaseDefines "FINAL_BUILD" "NDEBUG")

if(WIN32)
	if(MSVC)
		set(GlobalDefines ${GlobalDefines} "_CRT_SECURE_NO_WARNINGS")

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
	endif()
else()
	if(APPLE)
		set(GlobalDefines ${GlobalDefines} "MACOS_X")
	endif()

	# Additional flags for debug configuration
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb")

	# ouned: need sse optimizations in debug configuration, too because of the inline assembler which is sse code
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2")

	# Optimization
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -ffast-math")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -ffast-math")

	if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
		set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -mfpmath=sse -fexpensive-optimizations")

		# Link only libs actually needed
		set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Wl,--as-needed,-s")

		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
	elseif("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
		set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -flto")

		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-comment -Wno-reorder -Wunused-value -Wswitch")
	endif()

	if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mfpmath=sse -fexpensive-optimizations")

		# Link only libs actually needed
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wl,--as-needed,-s")

		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility-inlines-hidden")

		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-write-strings")
	elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto")

		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-comment -Wno-deprecated-writable-strings -Wno-reorder -Wunused-value -Wswitch")
	endif()
endif()

if(NOT CMAKE_BUILD_TYPE)
	message(STATUS "No build type selected, default to Release")
	set(CMAKE_BUILD_TYPE "Release")
endif()

if(UseInternalPNG)
	set(GlobalDefines ${GlobalDefines} "INTERNAL_PNG")
endif()

if(UseInternalJPEG)
	set(GlobalDefines ${GlobalDefines} "INTERNAL_JPEG")
endif()

if(UseInternalZLIB)
	set(GlobalDefines ${GlobalDefines} "INTERNAL_ZLIB")
endif()

if(UseInternalCURL)
	set(GlobalDefines ${GlobalDefines} "INTERNAL_CURL")
endif()

if(BuildPortableVersion)
	set(GlobalDefines ${GlobalDefines} "PORTABLE")
endif()

# game_version.h preparation
include(GetGitRevisionDescription)
get_git_head_revision(GIT_REFSPEC GIT_SHA1)

configure_file("${SRCDir}/qcommon/game_version.h.in" "${SRCDir}/qcommon/game_version.h" @ONLY)

# Package
set(CPACK_PACKAGE_NAME ${ProjectName})
set(CPACK_PACKAGE_VENDOR "jk2mv.org")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "JK2 MultiVersion")
set(CPACK_PACKAGE_VERSION "${MV_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${MV_MAJOR_VERSION}")
set(CPACK_PACKAGE_VERSION_MINOR "${MV_MINOR_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

if(BuildPack)
	if(WIN32)
		set(CPACK_GENERATOR "NSIS")
		set(CPACK_NSIS_URL_INFO_ABOUT "http://jk2mv.org")

		configure_file(
			${RESDir}/NSIS.definitions.nsh.in
			${CMAKE_CURRENT_BINARY_DIR}/res/NSIS.definitions.nsh
		)
		string(REGEX REPLACE "/" "\\\\\\\\" CPACK_NSIS_INCLFILE "${CMAKE_CURRENT_BINARY_DIR}/res/NSIS.definitions.nsh")

		include(CPack)
	elseif(APPLE)
		# TODO
		set(CPACK_GENERATOR "Bundle")
		set(CPACK_BUNDLE_NAME ${ProjectName})
	endif()
endif()

add_subdirectory(${SRCDir})