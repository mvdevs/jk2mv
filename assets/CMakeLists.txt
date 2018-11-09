# Make sure the user is not executing this script directly
if(NOT InMV)
	message(FATAL_ERROR "Use the top-level cmake script!")
endif(NOT InMV)

add_subdirectory(jk2mv)
add_subdirectory(jk2pro)
