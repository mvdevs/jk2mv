@REM Create JK2MV projects for Visual Studio 2013 using CMake
@echo off
for %%X in (cmake.exe) do (set FOUND=%%~$PATH:X)
if not defined FOUND (
	echo CMake was not found on your system. Please make sure you have installed CMake
	echo from http://www.cmake.org/ and cmake.exe is installed to your system's PATH
	echo environment variable.
	echo.
	pause
	exit /b 1
) else (
	echo Found CMake!
)
mkdir msvc10_x64
cd msvc10_x64
cmake -G "Visual Studio 10 Win64" -D CMAKE_INSTALL_PREFIX=./install ../..
pause