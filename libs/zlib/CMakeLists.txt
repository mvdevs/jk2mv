# We only execute this script if the user wants to use our bundled zlib/minizip.
#
# It is built as a static relocatable library, and linked into any target that
# requires it.
#
# The bundled copy can be produced from a zlib tarball by copying the c and h
# files from the root of the tarball to this folder, and then moving the public
# header files (zlib.h, zconf.h) into the include/ subfolder.
#
# The current bundled copy comes from the zlib 1.2.8 release.

include_directories(include/)

if(MSVC)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif(MSVC)

add_library(zlib STATIC adler32.c
                                compress.c
                                crc32.c
                                deflate.c
                                gzclose.c
                                gzlib.c
                                gzread.c
                                gzwrite.c
                                infback.c
                                inffast.c
                                inflate.c
                                inftrees.c
                                trees.c
                                uncompr.c
                                zutil.c)

# Let consumers get at our bundled copy in the standard CMake way.  These
# variables are not set in the cache, but instead shadow the variables in the
# cache.  These are the same variables exported by the built-in FindZLIB module.
set(ZLIB_LIBRARIES zlib PARENT_SCOPE)
set(ZLIB_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/zlib/include/ PARENT_SCOPE)
set(ZLIB_FOUND TRUE)

mark_as_advanced(ZLIB_LIBRARIES ZLIB_INCLUDE_DIRS)

# Build as position-independent code, since it will probably be linked into a
# dynamic library.
set_property(TARGET zlib PROPERTY POSITION_INDEPENDENT_CODE True)
set_property(TARGET zlib PROPERTY FOLDER "libs")
