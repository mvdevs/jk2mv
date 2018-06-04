#!/bin/sh

type cmake >/dev/null 2>&1 || { echo >&2 "Can't find cmake."; exit 1; }

BUILD_DIR="`uname`-`uname -m`-dedicated"
mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
	  -DUseInternalPNG=OFF -DUseInternalJPEG=OFF -DUseInternalZLIB=On -DUseInternalMiniZip=On \
	  -DBuildPortableVersion=OFF -DBuildMVMP=OFF ../..

make
