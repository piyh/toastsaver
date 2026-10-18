#!/bin/sh
# Installs the toastsaver screensavers for the current user.
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

echo "==> Installing to /usr/local"
sudo make install

echo "==> Turning on at login"
mkdir -p "$HOME/.config/autostart"
cat > "$HOME/.config/autostart/toastsaver.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=toastsaver idle watcher
Comment=Shows a screensaver when the computer is idle
Exec=/usr/local/bin/toastsaver-idle
NoDisplay=true
X-GNOME-Autostart-enabled=true
DESKTOP

echo "==> Adding the Screensavers icon (app menu and desktop)"
PICKER="$HOME/.local/share/applications/toastsaver-picker.desktop"
mkdir -p "$(dirname "$PICKER")"
cat > "$PICKER" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Screensavers
Comment=Pick your screensaver
Exec=/usr/local/bin/toastsaver-picker
Icon=/usr/local/share/toastsaver/icon.png
Categories=Game;
Terminal=false
DESKTOP
if [ -d "$HOME/Desktop" ]; then
    cp "$PICKER" "$HOME/Desktop/Screensavers.desktop"
    chmod +x "$HOME/Desktop/Screensavers.desktop"
    gio set "$HOME/Desktop/Screensavers.desktop" metadata::trusted true 2>/dev/null || true
fi

CONF="$HOME/.config/toastsaver.conf"
if [ ! -f "$CONF" ]; then
    cat > "$CONF" <<SETTINGS
# toastsaver settings. The Screensavers picker changes SAVER and NAME for you.
IDLE_MINUTES=5          # minutes of no input before the screensaver appears
SCREEN_OFF_MINUTES=30   # minutes before the display turns off (0 = never)
SAVER=toasters          # toasters pipes starfield mystify matrix aquarium lava name random
NAME='HELLO'            # shown by the name saver
SETTINGS
fi

if [ -n "${WAYLAND_DISPLAY:-}" ]; then
    pkill -f 'swayidle.*toastsaver' 2>/dev/null || true
    nohup /usr/local/bin/toastsaver-idle >/dev/null 2>&1 &
    sleep 1
    echo "==> Started."
else
    echo "==> Installed. It will start the next time you log in to the desktop."
fi
echo
echo "Pick a screensaver:  double-click 'Screensavers' on the desktop (or: toastsaver-picker)"
echo "Try one now:         toastsaver-idle --now pipes      (move the mouse to exit)"
echo "Check everything:    toastsaver-idle --check"
