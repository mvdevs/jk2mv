#!/bin/bash

type cmake >/dev/null 2>&1 || { echo >&2 "Can't find cmake."; exit 1; }

BUILD_DIR="`uname`-`uname -m`"
mkdir "$BUILD_DIR"
cd "$BUILD_DIR"
cmake -G "Unix Makefiles" -DUseInternalPNG=ON -DUseInternalJPEG=ON -DUseInternalZLIB=ON -DUseInternalCURL=ON ../..
make