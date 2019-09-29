#!/bin/bash

type cmake >/dev/null 2>&1 || { echo >&2 "Can't find cmake."; exit 1; }

BUILD_DIR="`uname`-`uname -m`-appimage"
mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DUseInternalLibs=OFF -DBuildPortableVersion=OFF -DBuildMVDED=OFF ../..
make
make install DESTDIR=vinstall

wget -q https://raw.githubusercontent.com/AppImage/pkg2appimage/master/functions.sh -O ./functions.sh
. ./functions.sh

APP="jk2mv"
LOWERAPP="org.mvdevs.jk2mv"
VERSION=$(git describe --tags)
ARCH=$(uname -m)

rm -r $APP.AppDir
mkdir $APP.AppDir
cd $APP.AppDir

########################################################################
# Copy install output
########################################################################
cp -a ../vinstall/* .
mv ./usr/local/* ./usr
rm -r ./usr/local

########################################################################
# Copy desktop and icon file to AppDir for AppRun to pick them up
########################################################################

get_apprun
get_desktop
get_icon

# For some reason appimage cant deal with our icon names
sed -i "s/$LOWERAPP/jk2mv/g" $LOWERAPP.desktop
mv $LOWERAPP.png jk2mv.png
find ./usr/share/icons -type f -execdir mv -i {} jk2mv.png \;


########################################################################
# Copy in the dependencies that cannot be assumed to be available
# on all target systems
########################################################################

copy_deps

move_lib
find ./usr/lib/* -name '*.so*' -exec mv -it ./usr/lib {} +
find ./usr/lib -type d -empty -delete

########################################################################
# Delete stuff that should not go into the AppImage
########################################################################

# https://github.com/probonopd/AppImages/blob/master/excludelist
delete_blacklisted

rm ./usr/lib/libpulse* # pulseaudio libs should be provided by the os
rm ./usr/lib/libxcb* # X stuff

########################################################################
# desktopintegration asks the user on first run to install a menu item
########################################################################

get_desktopintegration $LOWERAPP

########################################################################
# AppDir complete
# Now packaging it as an AppImage
########################################################################

cd ..
generate_type2_appimage
mv ../out/*.AppImage out/
mv out/*.AppImage out/jk2mv-v$VERSION-linux-$ARCH.AppImage

