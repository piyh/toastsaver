#!/bin/sh
# Offline install from a USB stick, building from source:
#   sh /media/$USER/<stick>/toastsaver/extras/deploy-from-usb.sh
# Copies the toastsaver folder to ~/toastsaver on the Pi, then runs install.sh.
set -eu

SRC=$(cd "$(dirname "$0")/.." && pwd)
DEST="$HOME/toastsaver"

if [ "$SRC" != "$DEST" ]; then
    echo "Copying $SRC -> $DEST"
    rm -rf "$DEST"
    cp -r "$SRC" "$DEST"
fi

cd "$DEST"
find . -name '._*' -delete                       # macOS junk files from the stick
chmod +x install.sh uninstall.sh extras/deploy-from-usb.sh scripts/toastsaver-idle   # USB sticks drop these
./install.sh
