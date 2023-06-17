# JK2MV
https://jk2mv.org

JK2MV (Multi Version) is a modification for Jedi Knight II: Jedi Outcast. It supports all three game versions and comes with various features and optimizations.

Main Features:
- Supports 1.02, 1.03 & 1.04 in a single executable
- Supports most mods made for JK2 (maps, skins, (code)mods etc.)
- Supports some original and custom Jedi Academy assets (maps, models etc.)
- Raw mouse input
- Fast ingame HTTP-Downloads with a dialogue asking you for permission before downloading files to your computer
- Multiplatform: Windows, Linux, MacOSX, FreeBSD
- Multiarchitecture: x86, x86-64, ARM
- Dynamic glow: Better looking lightsabers with the dynamic glow feature from JKA
- EAX/OpenAL sound fixed
- Supports modern screen resolutions and desktop scaling
- Fixes for all known security bugs
- Minimizer: Press the Windows key / Command key in fullscreen mode to minimize
- Improved gamma correction
- Performance improvements
- High resolution fonts and widescreen UI
- Fast AVI recording from demos
- Opensource (GPLv2)
- Engine extensions for JK2 modifications (maps, mods etc.)
- Modernized, portable codebase and build system
- Tons of other fixes and improvements in the engine, see the changelog for detailed information

# Automated Builds
These builds are automatically generated on every push to the repository. For testing purposes only.

| GitHub Actions |
| -------------- |
| [![GitHub Actions Badge](https://github.com/mvdevs/jk2mv/actions/workflows/build.yml/badge.svg)](https://github.com/mvdevs/jk2mv/actions?query=branch%3Amaster) |

# Project Goals

1. To provide an open source JK2 multiplayer client/server distribution for modern systems.
2. To support all three major JK2 multiplayer versions: 1.02, 1.03 and 1.04.
3. To benefit JK2 multiplayer community by providing necessary engine bugfixes, enchancements and changes to keep the game playable online using JK2MV.
4. To benefit JK2 modding community by providing engine bugfixes, enchancements and documentation.
5. To maintain compatibility with modifications created for original JK2 client/server.

Note that MVSDK is a separate project with its own goals.

# Howto Build JK2MV

1. Clone the JK2MV repository
Clone the JK2MV repository including submodules:
	* `git clone --recursive https://github.com/mvdevs/jk2mv`
2. Get CMake from either https://cmake.org or, in case of Linux, from the repositories of your distribution.
3. Dependencies
 	* Windows: Requires at least Visual Studio 2013, required libraries are shipped with JK2MV in the `libs` directory.
		* If you plan to build the installer package get NSIS from http://nsis.sourceforge.net
	* Linux/FreeBSD: OpenGL, OpenAL, SDL2 and depending on your configuration libjpeg, libpng, libminizip, zlib.
		* Ubuntu/Debian: `apt-get install git debhelper devscripts libsdl2-dev libgl1-mesa-dev libopenal-dev libjpeg-dev libpng-dev zlib1g-dev libminizip-dev`
		* Fedora: `dnf install git SDL2-devel mesa-libGL-devel openal-soft-devel libjpeg-turbo-devel libpng-devel zlib-devel minizip-devel`
	* MacOSX: XCode on MacOSX >= 10.9
		* Configure / Build SDL2:
			1. `curl -O https://www.libsdl.org/release/SDL2-2.0.10.tar.gz`
			2. `tar xzf SDL2-2.0.10.tar.gz && cd SDL2-2.0.10/Xcode/SDL`
			4. `sed -i -e 's/@rpath//g' SDL.xcodeproj/project.pbxproj` (packaging fails otherwise)
			5. `xcodebuild -configuration Release`
			6. `mkdir -p ~/Library/Frameworks/`
			7. ``ln -s `pwd`/build/Release/SDL2.framework ~/Library/Frameworks``
4. Configuration
	* Either
		* Use the CMake GUI to configure JK2MV
		* Generate the default configuration by using the build scripts in the `build` directory.
	* Important Options
		* `BuildPortableVersion` Build portable version (does not read or write files from your user/home directory)
		* `BuildMVMP` Whether to create targets for the client (jk2mvmp & jk2mvmenu)
		* `BuildMVDED` Whether to create targets for the dedicated server (jk2mvded)
		* `BuildMVSDK` Whether to build and integrate the mvsdk modules.
		* `CMAKE_BUILD_TYPE=Debug/Release` Build for development/release.
5. Building
	* Unix-Makefiles
		* `make` Build all previously selected binaries.
		* `make install` Installs JK2MV to `/usr` on Linux. On MacOSX it finishes the App-Package.
		* `make package` Generates rpm/deb packages on Linux and a dmg image on MacOSX.

# Contributing

Code contributions are welcome as GitHub pull requests, however they must meet some conditions:

1. Change must adhere to general project goals outlined earlier.
2. Source code must pass a review from JK2MV maintainer.
3. Source code must be published under GPL2 licence.

When in doubt, it is best to ask JK2MV developers directly if your idea has a chance of being accepted.

# License
JK2MV is licensed under GPLv2 as free software. You are free to use, modify and redistribute JK2MV following the terms in the LICENSE file. Please be aware of the implications of the GPLv2 licence. In short, be prepared to share your code under the same GPLv2 licence.

# Credits
- openjk (https://github.com/JACoders/OpenJK) (SDL port, engine fixes, improvements etc.)
- ioq3 (https://github.com/ioquake/ioq3/) (SDL port, x64 qvm, engine fixes, improvements etc.)
- xLAva (https://github.com/xLAva/JediOutcastLinux) (openal fixes)
- Thoroughbred-Of-Sin (http://thoroughbred-of-sin.deviantart.com/) (icon)

# Maintainers

- Daggo
- fau
- ouned

JK2MV maintainers can be contacted on GitHub or on JK2 Discord server.

![JK2 Discord](https://discordapp.com/api/guilds/220358272538902528/widget.png?style=banner2)
