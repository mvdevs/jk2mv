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
- Minimizer: Press the Windows key / Command key in fullscreen mode to minimize
- Improved gamma correction
- Tons of other fixes and improvements in the engine, see the changelog for detailed information
- Opensource (GPLv2)

# Build
| Windows | Linux / OSX |
|---------|-------------|
| [![Linux/OSX Build Status](https://ci.appveyor.com/api/projects/status/bwkb8nfl5w6s53u4?svg=true)](https://ci.appveyor.com/project/ouned/jk2mv/history) | [![Windows Build Status](https://api.travis-ci.org/mvdevs/jk2mv.svg)](https://travis-ci.org/mvdevs/jk2mv/builds)
[Download Automated Builds](https://jk2mv.org/builds)

1. Get CMake from either https://cmake.org or, in case of Linux, from the repositories of your distribution.
2. Dependencies
  * Windows: Requires at least Visual Studio 2010, required libraries are shipped with JK2MV in the `libs` directory.
    * If you plan to build the installer package get NSIS from http://nsis.sourceforge.net
  * Linux: OpenGL, OpenAL, SDL2 and depending on your configuration libjpeg, libpng, libcurl.
    * Ubuntu/Debian: apt-get install debhelper devscripts libsdl2-dev libgl1-mesa-dev libopenal-dev libjpeg8-dev libpng12-dev
  * MacOSX: XCode on MacOSX >= 10.6
    * Configure / Build SDL2
      1. `curl -O https://www.libsdl.org/release/SDL2-2.0.4.tar.gz`
      2. `tar xzf SDL2-2.0.4.tar.gz`
      3. `cd SDL2-2.0.4/Xcode/SDL`
      4. `sed -i -e 's/@rpath//g' SDL.xcodeproj/project.pbxproj`
      5. `xcodebuild ARCHS="i386 x86_64" ONLY_ACTIVE_ARCH=NO -configuration Release`
      6. `mkdir -p ~/Library/Frameworks/`
      7. ``ln -s `pwd`/build/Release/SDL2.framework ~/Library/Frameworks``
3. Configuration
  * Either
    * Use the CMake GUI to configure JK2MV
    * Generate the default configuration by using the build scripts in the `build` directory.
  * Important Options
    * `BuildPortableVersion` Build portable version (does not read or write files from your user/home directory)
    * `BuildMVMP` Whether to create targets for the client (jk2mvmp & jk2mvmenu)
    * `BuildMVDED` Whether to create targets for the dedicated server (jk2mvded)
    * `CMAKE_BUILD_TYPE=Debug/Release` Debugging symbols enabled/disabled.
4. Building
  * Unix-Makefiles
    * `make` Build all previously selected binaries.
    * `make install` Installs JK2MV to `/usr` on Linux. On MacOSX it finishes the App-Package.
    * `make package` Generates rpm/deb packages on Linux and a dmg image on MacOSX.

# License
JK2MV is licensed under GPLv2 as free software. You are free to use, modify and redistribute JK2MV following the terms in the LICENSE file. Please be aware of the implications of the GPLv2 licence. In short, be prepared to share your code under the same GPLv2 licence.

# Credits
- openjk (https://github.com/JACoders/OpenJK) (SDL port, engine fixes, improvements etc.)
- ioq3 (https://github.com/ioquake/ioq3/) (SDL port, x64 qvm, engine fixes, improvements etc.)
- xLAva (https://github.com/xLAva/JediOutcastLinux) (openal fixes)
- meerkat webserver (https://github.com/cesanta/meerkat) (http downloads serverside)
- libcurl (http://curl.haxx.se/libcurl/) (http downloads clientside)
- Thoroughbred-Of-Sin (http://thoroughbred-of-sin.deviantart.com/) (icon)
