#!/bin/sh
# Builds toastsaver from source and installs it under /usr/local.
# (Most people should use the apt package instead; see the README.)
# Run from this folder as your normal user:  ./install.sh
set -eu
cd "$(dirname "$0")"

if [ "$(id -u)" -eq 0 ]; then
    echo "Run this as your normal user (not with sudo); it asks for your password when needed."
    exit 1
fi

echo "==> Installing build tools and libraries (asks for your password)"
sudo apt-get update -qq
sudo apt-get install -y build-essential pkg-config libsdl2-dev swayidle
# Optional: lets toastsaver turn the display off later. Fine if unavailable.
sudo apt-get install -y wlopm >/dev/null 2>&1 || echo "    (wlopm not available; screen-off timer disabled)"

echo "==> Building"
make clean >/dev/null
make

echo "==> Installing to /usr/local (autostart for every user, menu entries, icon)"
sudo make install PREFIX=/usr/local

# Older versions put these in your home folder; the system-wide copies replace them.
rm -f "$HOME/.config/autostart/toastsaver.desktop" \
      "$HOME/.local/share/applications/toastsaver-picker.desktop" \
      "$HOME/.local/share/applications/toastsaver-settings.desktop"

if [ -d "$HOME/Desktop" ]; then
    /usr/local/bin/toastsaver --desktop-icon >/dev/null
fi

if [ -n "${WAYLAND_DISPLAY:-}" ]; then
    /usr/local/bin/toastsaver-idle --restart
    echo "==> Started."
else
    echo "==> Installed. It will start the next time you log in to the desktop."
fi
echo
echo "Pick a screensaver:  'Screensavers' on the desktop or in the menu"
echo "Settings:            the gear in its top-left corner"
echo "Check everything:    toastsaver-idle --check"
