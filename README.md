# JK2MV
https://jk2mv.org

JK2MV (Multi Version) is a modification for Jedi Knight II: Jedi Outcast. It supports all three game versions and comes with various features and optimizations.

Main Features:
- Supports 1.02, 1.03 & 1.04 in a single executable
- Supports most mods made for JK2 (maps, skins, (code)mods etc.)
- Fast ingame HTTP-Downloads with a dialogue asking you for permission before downloading files to your computer
- Multiplatform: Windows, Linux, MacOSX
- Multiarchitecture: 32 and 64 bit support on all platforms
- Dynamic glow: Better looking lightsabers with the dynamic glow feature from JKA
- EAX/OpenAL sound fixed
- Support for modern screen resolutions
- Fixes for all known security bugs
- Minimizer: Press the Windows key in fullscreen mode to minimize (Windows only)
- Improved gamma correction
- Tons of other fixes and improvements in the engine, see the changelog for detailed information
- Opensource (GPLv2)

# Build
| Windows | Linux / OSX |
|---------|-------------|
| [![Linux/OSX Build Status](https://ci.appveyor.com/api/projects/status/bwkb8nfl5w6s53u4?svg=true)](https://ci.appveyor.com/project/ouned/jk2mv/history) | [![Windows Build Status](https://api.travis-ci.org/mvdevs/jk2mv.svg)](https://travis-ci.org/mvdevs/jk2mv/builds)
[Download Builds](https://jk2mv.org/builds)

JK2MV uses the cmake build system. CMake generates project files for a lot of different IDE's like Visual Studio, Codeblocks, XCode etc.

Windows:
* Get cmake from http://www.cmake.org
* Either run one of the visual studio project generators in the "build" directory or configure yourself via the cmake gui.

Linux:
* Get cmake from your distributions repository's.
* Run one of the build scripts in the "build" directory. They create unix makefiles and then trigger the make command.

MacOSX:
* Get cmake from http://www.cmake.org
* create project files via the cmake gui.
* For the resulting app to actually work you need to build the "install" target.

# License
JK2MV is licensed under GPLv2 as free software. You are free to use, modify and redistribute JK2MV following the terms in the LICENSE file. Please be aware of the implications of the GPLv2 licence. In short, be prepared to share your code under the same GPLv2 licence.

# Credits
- openjk (https://github.com/JACoders/OpenJK) (SDL port, engine fixes, improvements etc.)
- ioq3 (https://github.com/ioquake/ioq3/) (SDL port, x64 qvm, engine fixes, improvements etc.)
- xLAva (https://github.com/xLAva/JediOutcastLinux) (openal fixes)
- meerkat webserver (https://github.com/cesanta/meerkat) (http downloads serverside)
- libcurl (http://curl.haxx.se/libcurl/) (http downloads clientside)
- Thoroughbred-Of-Sin (http://thoroughbred-of-sin.deviantart.com/) (icon)
- syZn (french, spanish translation)
