#!/bin/sh
# Deploy from a USB stick:  sh /media/$USER/<stick>/toastsaver/deploy.sh
# Copies this folder to ~/toastsaver on the Pi, then runs the installer.
set -eu

SRC=$(cd "$(dirname "$0")" && pwd)
DEST="$HOME/toastsaver"

if [ "$SRC" != "$DEST" ]; then
    echo "Copying $SRC -> $DEST"
    rm -rf "$DEST"
    cp -r "$SRC" "$DEST"
fi

cd "$DEST"
find . -name '._*' -delete                       # macOS junk files from the stick
chmod +x install.sh uninstall.sh deploy.sh scripts/toastsaver-idle   # USB sticks drop these
./install.sh
