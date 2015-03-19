#!/bin/bash

type cmake >/dev/null 2>&1 || { echo >&2 "Can't find cmake."; exit 1; }

cmake -G "Unix Makefiles" -DUseInternalPNG=OFF -DUseInternalJPEG=OFF -DUseInternalZLIB=OFF -DUseInternalCURL=OFF ..
make