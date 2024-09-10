# We only execute this script if the user wants to use our bundled libpng.
#
# It is built as a static relocatable library, and linked into any target that
# requires it.

# This only has effect in this directory (lib/png).
include_directories(include/)
include_directories(${ZLIB_INCLUDE_DIRS})

if(MSVC)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif(MSVC)

if(ARCH_X86)
	add_definitions(-DPNG_INTEL_SSE_OPT=1)
	add_definitions(-DPNG_INTEL_SSE_IMPLEMENTATION=1) # only SSE2
	set(libpng_intel_sources
		intel/intel_init.c
		intel/filter_sse2_intrinsics.c)
endif(ARCH_X86)

# there is also SIMD for ARM and other architectures that can be enabled

add_library(libpng STATIC include/png.h
                                  include/pngconf.h
                                  include/pnglibconf.h
                                  png.c
                                  pngdebug.h
                                  pngerror.c
                                  pngget.c
                                  pnginfo.h
                                  pngmem.c
                                  pngpread.c
                                  pngpriv.h
                                  pngread.c
                                  pngrio.c
                                  pngrtran.c
                                  pngrutil.c
                                  pngset.c
                                  pngstruct.h
                                  pngtrans.c
                                  pngwio.c
                                  pngwrite.c
                                  pngwtran.c
                                  pngwutil.c
								  ${libpng_arm_sources}
								  ${libpng_intel_sources}
							  )

# Let consumers get at our bundled library in the standard CMake way.  These
# variables are not set in the cache, but instead shadow the variables in the
# cache.  (These are the same variables exported by the standard CMake FindPNG
# module).
set(PNG_LIBRARIES libpng PARENT_SCOPE)
set(PNG_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/libpng/include/ PARENT_SCOPE)
set(PNG_FOUND TRUE)

mark_as_advanced(PNG_LIBRARIES PNG_INCLUDE_DIRS)

# Build as position-independent code, since it will probably be linked into a
# dynamic library.
set_property(TARGET libpng PROPERTY POSITION_INDEPENDENT_CODE True)
set_property(TARGET libpng PROPERTY FOLDER "libs")
