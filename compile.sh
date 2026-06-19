#!/bin/bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
WATCOM_DIR="${WATCOM_DIR:-$(dirname "$REPO_DIR")/watcom}"

# Locate DOSBox-X
DOSBOX="${DOSBOX_X:-}"
if [ -z "$DOSBOX" ]; then
    if [ -f "/Applications/DOSBox-X.app/Contents/MacOS/dosbox-x" ]; then
        DOSBOX="/Applications/DOSBox-X.app/Contents/MacOS/dosbox-x"
    elif command -v dosbox-x >/dev/null 2>&1; then
        DOSBOX="$(command -v dosbox-x)"
    else
        echo "ERROR: DOSBox-X not found. Install from https://dosbox-x.com"
        echo "       or set DOSBOX_X=/path/to/dosbox-x"
        exit 1
    fi
fi

if [ ! -d "$WATCOM_DIR" ]; then
    echo "ERROR: Open Watcom not found at: $WATCOM_DIR"
    echo "       Download from https://github.com/open-watcom/open-watcom-v2/releases"
    echo "       or set WATCOM_DIR=/path/to/watcom"
    exit 1
fi

CONF=$(mktemp /tmp/dosbox-compile-XXXXXX.conf)
cat > "$CONF" << ENDCONF
[dosbox]
machine=vga

[cpu]
cputype=486dlc
cycles=max

[autoexec]
@ECHO OFF
MOUNT C "$REPO_DIR"
MOUNT D "$WATCOM_DIR"
SET WATCOM=D:
SET PATH=%PATH%;D:\BINW
SET INCLUDE=D:\H;D:\H\DOS
SET LIB=D:\LIB286;D:\LIB286\DOS
C:
WMAKE
ECHO.
ECHO ===== Build complete. Press any key to close. =====
PAUSE
EXIT
ENDCONF

"$DOSBOX" -conf "$CONF"
rm -f "$CONF"
