#!/bin/bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"

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

if [ ! -f "$REPO_DIR/INVADERS.EXE" ]; then
    echo "ERROR: INVADERS.EXE not found. Run ./compile.sh first."
    exit 1
fi

CONF=$(mktemp /tmp/dosbox-run-XXXXXX.conf)
cat > "$CONF" << ENDCONF
[dosbox]
machine=vga

[cpu]
cputype=pentium
cycles=max

[sblaster]
sbtype=sbpro2
sbbase=220
irq=5
dma=1

[sdl]
windowresolution=640x400

[autoexec]
@ECHO OFF
MOUNT C "$REPO_DIR"
C:
SET BLASTER=A220 I5 D1 T4
INVADERS.EXE
EXIT
ENDCONF

"$DOSBOX" -conf "$CONF"
rm -f "$CONF"
