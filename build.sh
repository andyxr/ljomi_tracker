#!/bin/sh
# Usage (from PowerShell or Git Bash):  sh build.sh [make targets]   e.g.  sh build.sh devinstall
# Runs make inside the MSYS2 MinGW64 environment that Rack plugins require on Windows.
HERE="$(cd "$(dirname "$0")" && pwd -W 2>/dev/null || pwd)"
RUD="$(cygpath -m "$LOCALAPPDATA")/Rack2"
MSYSTEM=MINGW64 /c/msys64/usr/bin/bash.exe -lc "cd \"\$(cygpath -u '$HERE')\" && make -j8 RACK_USER_DIR='$RUD' $*"
