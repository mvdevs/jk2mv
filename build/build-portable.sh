#!/bin/sh

type cmake >/dev/null 2>&1 || { echo >&2 "Can't find cmake."; exit 1; }

BUILD_DIR="`uname`-`uname -m`-portable"
mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

cmake -G "Unix Makefiles" -DUseInternalPNG=OFF -DUseInternalJPEG=OFF -DUseInternalZLIB=OFF -DUseInternalMiniZip=OFF -DCMAKE_BUILD_TYPE=Release ../..

make
