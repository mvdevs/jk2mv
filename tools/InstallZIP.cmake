#=============================================================================
# Copyright 2007-2009 Kitware, Inc.
# Copyright 2015 OpenJK contributors
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
#
# * Neither the names of Kitware, Inc., the Insight Software Consortium,
#   nor the names of their contributors may be used to endorse or promote
#   products derived from this software without specific prior written
#   permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#=============================================================================

include(CMakeParseArguments)

if ("${CMAKE_VERSION}" VERSION_EQUAL "3.3" OR "${CMAKE_VERSION}" VERSION_GREATER "3.3")
	set(ZIP_COMMAND ${CMAKE_COMMAND} -E tar c "<ARCHIVE>" --format=zip -- <FILES>)
else()
	unset(ZIP_EXECUTABLE CACHE)
	if(WIN32)
		if(NOT ZIP_EXECUTABLE)
			find_program(ZIP_EXECUTABLE wzzip PATHS "$ENV{ProgramFiles}/WinZip")
			if(ZIP_EXECUTABLE)
				set(ZIP_COMMAND "${ZIP_EXECUTABLE}" -P "<ARCHIVE>" <FILES>)
			endif()
		endif()
	  
		if(NOT ZIP_EXECUTABLE)
			find_program(ZIP_EXECUTABLE wzzip PATHS "$ENV{ProgramW6432}/WinZip")
			if(ZIP_EXECUTABLE)
				set(ZIP_COMMAND "${ZIP_EXECUTABLE}" -P "<ARCHIVE>" <FILES>)
			endif()
		endif()

		if(NOT ZIP_EXECUTABLE)
			find_program(ZIP_EXECUTABLE 7z PATHS "$ENV{ProgramFiles}/7-Zip")
			if(ZIP_EXECUTABLE)
				set(ZIP_COMMAND "${ZIP_EXECUTABLE}" a -tzip "<ARCHIVE>" <FILES>)
			endif()
		endif()

		if(NOT ZIP_EXECUTABLE)
			find_program(ZIP_EXECUTABLE 7z PATHS "$ENV{ProgramW6432}/7-Zip")
			if(ZIP_EXECUTABLE)
				set(ZIP_COMMAND "${ZIP_EXECUTABLE}" a -tzip "<ARCHIVE>" <FILES>)
			endif()
		endif()

		if(NOT ZIP_EXECUTABLE)
			find_program(ZIP_EXECUTABLE winrar PATHS "$ENV{ProgramFiles}/WinRAR")
			if(ZIP_EXECUTABLE)
				set(ZIP_COMMAND "${ZIP_EXECUTABLE}" a "<ARCHIVE>" <FILES>)
			endif()
		endif()

		if(NOT ZIP_EXECUTABLE)
			find_program(ZIP_EXECUTABLE winrar PATHS "$ENV{ProgramW6432}/WinRAR")
			if(ZIP_EXECUTABLE)
				set(ZIP_COMMAND "${ZIP_EXECUTABLE}" a "<ARCHIVE>" <FILES>)
			endif()
		endif()
	endif()

	if(NOT ZIP_EXECUTABLE)
		if(WIN32)
				find_package(Cygwin)
				find_program(ZIP_EXECUTABLE zip PATHS "${CYGWIN_INSTALL_PATH}/bin")
		else()
				find_program(ZIP_EXECUTABLE zip)
		endif()
	  
		if(ZIP_EXECUTABLE)
			set(ZIP_COMMAND "${ZIP_EXECUTABLE}" -r "<ARCHIVE>" . -i <FILES>)
		endif()
	endif()
	
	if(NOT ZIP_EXECUTABLE)
		if(WIN32)
			message(FATAL_ERROR "No ZIP executable found! Install 7zip, WinZIP or WinRAR or get CMake >= 3.3.")
		else()
			message(FATAL_ERROR "No zip executable found! Install the zip program or get CMake >= 3.3.")
		endif()
	endif()
endif()

function(add_zip_command tmp_output output)
	set(MultiValueArgs DIR FILES DEPENDS)
	cmake_parse_arguments(ARGS "" "" "${MultiValueArgs}" ${ARGN})

	set(ZipCommand ${ZIP_COMMAND})
	string(REPLACE <ARCHIVE> "${tmp_output}" ZipCommand "${ZipCommand}")
	string(REPLACE <FILES> "${ARGS_FILES}" ZipCommand "${ZipCommand}")
	add_custom_command(OUTPUT "${tmp_output}"
		WORKING_DIRECTORY ${ARGS_DIR}
		COMMAND ${CMAKE_COMMAND} -E remove "${tmp_output}"
		COMMAND ${ZipCommand}
		COMMAND ${CMAKE_COMMAND} -E copy "${tmp_output}" "${output}"
		DEPENDS ${ARGS_DEPENDS}
	)
endfunction(add_zip_command)
