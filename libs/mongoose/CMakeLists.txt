# Build our bundled mongoose.
#
# It is built as a static relocatable library, and linked into any target that
# requires it.  Other targets in JK2MV that consume need to add
# MONGOOSE_INCLUDE_DIRS and MONGOOSE_LIBRARIES to their lists of include
# directories and target link libraries.
#
# The bundled copy is produced by taking the files mongoose.{c,h}
# from the mongoose release tarball.

# These settings only apply to this directory.
include_directories(include/)

if(MSVC)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif(MSVC)

add_library(mongoose STATIC mongoose.c)

# Let consumers get at our bundled copy in the standard CMake way.  These
# variables are not set in the cache, but instead shadow the variables in the
# cache.
set(MONGOOSE_LIBRARIES mongoose PARENT_SCOPE)
set(MONGOOSE_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/mongoose/include/ PARENT_SCOPE)
set(MONGOOSE_FOUND TRUE)

mark_as_advanced(MONGOOSE_LIBRARIES MONGOOSE_INCLUDE_DIRS)

# We need to build it as position-independent code, because it might get linked
# into dynamic libraries.
set_property(TARGET mongoose PROPERTY POSITION_INDEPENDENT_CODE True)
set_property(TARGET mongoose PROPERTY FOLDER "libs")
