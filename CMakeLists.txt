cmake_minimum_required(VERSION 3.1.0)

# For checks in subdirectories
set(InMV TRUE)

# Paths
set(LIBDir "${CMAKE_SOURCE_DIR}/libs")
set(RESDir "${CMAKE_SOURCE_DIR}/res")

# Custom CMake Modules
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_SOURCE_DIR}/tools")
list(INSERT CMAKE_MODULE_PATH 0 "${RESDir}")

# Project & Versioning
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(ProjectName "JK2MV" CACHE STRING "Project Name")
set(ProjectURL "https://jk2mv.org")
project(${ProjectName})

include(GetGitRevisionDescription)
git_describe(MV_VERSION --tags)
if(NOT MV_VERSION OR MV_VERSION STREQUAL "GIT-NOTFOUND" OR MV_VERSION STREQUAL "HEAD-HASH-NOTFOUND")
	set(MV_VERSION "UNKNOWN")
	message(WARNING "Could not find git or git repository. Install git and make sure you actually cloned the repository (Directory named .git must exist) for correct versioning.")
endif()

message(STATUS "${ProjectName} Version: ${MV_VERSION}")

# Languages
enable_language(C CXX)
if(MSVC)
	enable_language(ASM_MASM)
endif()

if(CMAKE_CONFIGURATION_TYPES)
	set(MultiConfigGenerator TRUE)
endif()

# Customizable options
option(BuildPortableVersion "Build portable version (does not read or write files from your user/home directory" ON)
option(BuildMVMP "Whether to create targets for the client (jk2mvmp & jk2mvmenu)" ON)
option(BuildMVDED "Whether to create targets for the dedicated server (jk2mvded)" ON)
option(BuildMVSDK "Whether to build and integrate the mvsdk modules" ON)

set(SystemProcessor "${CMAKE_SYSTEM_PROCESSOR}" CACHE STRING "Option to override CMAKE_SYSTEM_PROCESSOR")

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/mvsdk/code/ui/CMakeLists.txt")
	message(FATAL_ERROR "Could not find CMakeLists.txt in \"src/mvsdk/code/ui\". Unable to continue. Make sure you cloned jk2mv including submodules.")
endif()

if(BuildMVSDK AND NOT EXISTS "${CMAKE_SOURCE_DIR}/src/mvsdk/CMakeLists.txt")
    message(WARNING "Could not find CMakeLists.txt in \"src/mvsdk\". Make sure you cloned jk2mv including submodules. Continuing with BuildMVSDK=OFF.")
    set(BuildMVSDK OFF)
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

	if(UseInternalLibs)
		set(UseInternalPNGDefault ON)
		set(UseInternalJPEGDefault ON)
		set(UseInternalZLIBDefault ON)
		set(UseInternalMiniZipDefault ON)
	elseif(MINGW)
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

	option(UseInternalLibs "Whether to use included libraries instead of locally installed ones")
	option(UseInternalPNG "Whether to use the included libpng instead of a locally installed one" ${UseInternalPNGDefault})
	option(UseInternalJPEG "Whether to use the included libjpeg instead of a locally installed one" ${UseInternalJPEGDefault})
	option(UseInternalZLIB "Whether to use the included zlib instead of a locally installed one" ${UseInternalZLIBDefault})
	option(UseInternalMiniZip "Whether to use the included minizip instead of a locally installed one" ${UseInternalMiniZipDefault})
endif()

# Platform & Architecture
if(WIN32)
	set(ARCH_X86 ON)
	set(GlobalDefines ${GlobalDefines} "WIN32")
	if(CMAKE_SIZEOF_VOID_P MATCHES "8")
		set(GlobalDefines ${GlobalDefines} "ARCH_X86_64" "WIN64")
		set(Architecture "x64")
		set(ARCH_X86_64 ON)
	else()
		set(GlobalDefines ${GlobalDefines} "ARCH_X86")
		set(Architecture "x86")
		set(ARCH_X86_32 ON)
	endif()
elseif(APPLE)
	set(ARCH_X86 ON)
	set(GlobalDefines ${GlobalDefines} "MACOS_X")
	if(SystemProcessor MATCHES "^i.86$")
		set(GlobalDefines ${GlobalDefines} "ARCH_X86")
		set(Architecture "i386")
		set(ARCH_X86_32 ON)
	elseif(SystemProcessor MATCHES "^x86.64$")
		set(GlobalDefines ${GlobalDefines} "ARCH_X86_64")
		set(Architecture "x86_64")
		set(ARCH_X86_64 ON)
	endif()
else()
	if(SystemProcessor MATCHES "^arm")
		set(GlobalDefines ${GlobalDefines} "ARCH_ARM32")
		set(Architecture "arm")
		set(ARCH_ARM32 ON)
	elseif(SystemProcessor MATCHES "^i.86$")
		set(GlobalDefines ${GlobalDefines} "ARCH_X86")
		set(Architecture "i386")
		set(ARCH_X86 ON)
		set(ARCH_X86_32 ON)
	elseif(SystemProcessor MATCHES "^(x86.64|amd64)$")
		set(GlobalDefines ${GlobalDefines} "ARCH_X86_64")
		set(Architecture "amd64")
		set(ARCH_X86 ON)
		set(ARCH_X86_64 ON)
	else()
		set(GlobalDefines ${GlobalDefines} "NO_VM_COMPILED")
		set(Architecture "${SystemProcessor}")
		set(ARCH_UNKNOWN ON)
	endif()
endif()

message(STATUS "Architecture is ${Architecture}")
if(ARCH_UNKNOWN)
	message(WARNING "Building for unsupported Architecture ${Architecture}.")
endif()

# Binary names
set(MVMP "jk2mvmp")
set(MVMENU "jk2mvmenu_${Architecture}")
set(MVDED "jk2mvded")

# Directories
if(MultiConfigGenerator)
	# Visual Studio, Xcode etc. support selecting build type in the IDE and automatically append
	# the configuration type to the path
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
	set(CPACK_PACKAGE_DIRECTORY ${CMAKE_BINARY_DIR}/out)

	if(APPLE)
		set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/out/bundle)
	endif()
else()
	if(NOT CMAKE_BUILD_TYPE)
		message(STATUS "No build type selected, default to Release")
		set(CMAKE_BUILD_TYPE "Release")
	endif()

	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out/${CMAKE_BUILD_TYPE})
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out/${CMAKE_BUILD_TYPE})
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out/${CMAKE_BUILD_TYPE})
	set(CPACK_PACKAGE_DIRECTORY ${CMAKE_BINARY_DIR}/out)

	if(APPLE)
		set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/out/bundle/${CMAKE_BUILD_TYPE})
	endif()
endif()

# Compiler defines / settings
set(GlobalDefines ${GlobalDefines} "BOTLIB" "_JK2")
set(DebugDefines ${DebugDefines} "DEBUG")
set(ReleaseDefines "FINAL_BUILD" "NDEBUG")

if(MSVC)
	# Warnings
	# 4244 = conversion' conversion from 'type1' to 'type2', possible loss of data
	# 4996 = 'function': was declared deprecated
	# 4267 = 'var' : conversion from 'size_t' to 'type', possible loss of data
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4244 /wd4996 /wd4267")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4244 /wd4996 /wd4267")
	
	# Multithreaded
	set(CMAKE_CXX_FLAGS_RELEASE "/MT" CACHE TYPE INTERNAL FORCE)
	set(CMAKE_C_FLAGS_RELEASE "/MT" CACHE TYPE INTERNAL FORCE)

	# Platform
	set(GlobalDefines ${GlobalDefines} "UNICODE" "_UNICODE")

	# Optimization
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MP") # multiprocessor compilation
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MP")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2 /Oi /Ot /Ob2") # maximize speed
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Oi /Ot /Ob2")

	if(ARCH_X86_32)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /arch:SSE2")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:SSE2")

		# dont use safe exception handler
		# openal does not support it on X86_32
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /safeseh:no")
	endif()

	# Set the same options for the build including symbols
	set(CMAKE_C_FLAGS_RELWITHDEBINFO ${CMAKE_C_FLAGS_RELEASE})
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO ${CMAKE_CXX_FLAGS_RELEASE})

	# Debugging
	# /Zi = create PDB with symbols, linenumbers etc.
	# /DEBUG:FULL = Generate Debug Information optimized for sharing and publishing (/DEBUG:FULL)
	set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} /Zi")
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Zi")
	set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /DEBUG:FULL")
elseif ((${CMAKE_C_COMPILER_ID} STREQUAL GNU) OR (${CMAKE_C_COMPILER_ID} MATCHES Clang|AppleClang))
	# Additional flags for debug configuration
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb3")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb3")

	# we need sse optimizations in debug configuration, too because of the inline assembler which is sse code
	if(ARCH_X86)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2")
	endif()

	# Optimization
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")

	# General
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g1 -fno-omit-frame-pointer")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g1 -fno-omit-frame-pointer")

	# Warnings
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wsign-compare -Wno-unknown-pragmas")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-write-strings")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-pragmas -Wno-attributes")

	if(${CMAKE_C_COMPILER_ID} STREQUAL GNU)
		if(ARCH_X86)
			set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -mfpmath=sse")
		endif()

		# Link only libs actually needed
		set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Wl,--as-needed")
	endif()

	if(${CMAKE_CXX_COMPILER_ID} STREQUAL GNU)
		if(ARCH_X86)
			set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mfpmath=sse")
		endif()

		# Link only libs actually needed
		# workaround for older gcc versions and c++11 std::thread: explicitly link lpthread
		# https://bugs.launchpad.net/ubuntu/+source/gcc-defaults/+bug/1228201
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--no-as-needed -pthread -lpthread -Wl,--as-needed")

		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility-inlines-hidden")

		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wformat=2 -Wno-unused-but-set-variable")
	elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES Clang|AppleClang)
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
elseif(APPLE)
	set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-macosx-${Architecture}")

	if(BuildPortableVersion)
		set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-portable")

		set(CPACK_GENERATOR "TGZ")
	else()
		set(CPACK_GENERATOR "DragNDrop")
		set(CPACK_DMG_FORMAT "UDBZ")
	endif()
else()
	set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-linux-${Architecture}")

	if(BuildPortableVersion)
		set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-portable")

		set(CPACK_GENERATOR "TGZ")
	else()
		set(CPACK_GENERATOR ${CPACK_GENERATOR} "DEB")
		set(CPACK_DEBIAN_PACKAGE_NAME "jk2mv") # debian packages are lowercase
		set(CPACK_DEBIAN_PACKAGE_SECTION "games")
		set(CPACK_DEBIAN_PACKAGE_HOMEPAGE ${ProjectURL})
		set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE ${Architecture})
		set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

		set(CPACK_GENERATOR ${CPACK_GENERATOR} "RPM")
		set(CPACK_RPM_PACKAGE_NAME "jk2mv")
		set(CPACK_RPM_PACKAGE_DESCRIPTION "JK2MV (Multi Version) is a modification for Jedi Knight II: Jedi Outcast. It supports all three game versions and comes with various features and optimizations.")
		set(CPACK_RPM_PACKAGE_URL ${ProjectURL})
		set(CPACK_RPM_PACKAGE_LICENSE "GPLv2")
		set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
		set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
			/usr/share/applications
			/usr/share/metainfo
			/usr/share/icons /usr/share/icons/hicolor
			/usr/share/icons/hicolor/256x256 /usr/share/icons/hicolor/256x256/apps
			/usr/share/icons/hicolor/48x48 /usr/share/icons/hicolor/48x48/apps
		)

		set(CPACK_RPM_PACKAGE_REQUIRES "glibc, libgcc, libstdc++, SDL2, openal-soft")
		if(NOT UseInternalZLIB)
			set(CPACK_RPM_PACKAGE_REQUIRES "${CPACK_RPM_PACKAGE_REQUIRES}, zlib")
		endif()
		if(NOT UseInternalMiniZip)
			set(CPACK_RPM_PACKAGE_REQUIRES "${CPACK_RPM_PACKAGE_REQUIRES}, minizip")
		endif()
		if(NOT UseInternalJPEG)
			set(CPACK_RPM_PACKAGE_REQUIRES "${CPACK_RPM_PACKAGE_REQUIRES}, libjpeg-turbo")
		endif()
		if(NOT UseInternalPNG)
			set(CPACK_RPM_PACKAGE_REQUIRES "${CPACK_RPM_PACKAGE_REQUIRES}, libpng")
		endif()
	endif()
endif()
include(CPack)

add_subdirectory(assets)
add_subdirectory(src)
