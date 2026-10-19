#!/bin/sh
# Switches a source install (./install.sh, or the USB stick installer) over to the apt
# package, so updates arrive with `sudo apt upgrade`. Keeps your settings.
#   curl -fsSL https://raw.githubusercontent.com/piyh/toastsaver/main/extras/migrate-to-apt.sh | sh
# Add  -s -- --auto-updates  after `sh` to also turn on daily automatic updates.
# Run as your normal user; it asks for your password when needed.
set -eu
RAW=https://raw.githubusercontent.com/piyh/toastsaver/main/extras
auto=0
[ "${1:-}" = "--auto-updates" ] && auto=1

if [ "$(id -u)" -eq 0 ]; then
    echo "Run this as your normal user (not with sudo); it asks for your password when needed."
    exit 1
fi

had_icon=0
[ -e "$HOME/Desktop/Screensavers.desktop" ] && had_icon=1

echo "==> Stopping the old screensaver watcher"
# Only toastsaver's own idle watcher; the desktop's screen blanking is left alone.
pkill -f 'swayidle.*toastsaver' 2>/dev/null || true
pkill -f '/usr/local/bin/toastsaver-idle' 2>/dev/null || true
pkill -x toastsaver 2>/dev/null || true

echo "==> Removing the source install from /usr/local (asks for your password)"
# Everything any version of install.sh put there. The autostart file matters most:
# the package installs one at the same path, and an old copy would point at /usr/local.
sudo rm -rf /usr/local/bin/toastsaver /usr/local/bin/toastsaver-picker \
    /usr/local/bin/toastsaver-settings /usr/local/bin/toastsaver-idle \
    /usr/local/share/toastsaver \
    /usr/local/share/applications/toastsaver-picker.desktop \
    /usr/local/share/applications/toastsaver-settings.desktop \
    /usr/local/share/icons/hicolor/128x128/apps/toastsaver.png \
    /usr/local/share/man/man1/toastsaver.1 /usr/local/share/man/man1/toastsaver-picker.1 \
    /usr/local/share/man/man1/toastsaver-settings.1 /usr/local/share/man/man1/toastsaver-idle.1
if [ -f /etc/xdg/autostart/toastsaver.desktop ] && ! dpkg -S /etc/xdg/autostart/toastsaver.desktop >/dev/null 2>&1; then
    sudo rm -f /etc/xdg/autostart/toastsaver.desktop
fi

echo "==> Removing old per-user files (your settings in ~/.config/toastsaver.conf are kept)"
rm -f "$HOME/.config/autostart/toastsaver.desktop" \
      "$HOME/.local/share/applications/toastsaver-picker.desktop" \
      "$HOME/.local/share/applications/toastsaver-settings.desktop" \
      "$HOME/Desktop/Screensavers.desktop"
# The copy the USB installer made of the source folder.
if [ -f "$HOME/toastsaver/install.sh" ] && [ -f "$HOME/toastsaver/src/engine.h" ]; then
    rm -rf "$HOME/toastsaver"
fi
hash -r 2>/dev/null || true

echo "==> Installing the apt package"
curl -fsSL https://piyh.github.io/toastsaver/setup.sh | sh

[ "$had_icon" -eq 1 ] && toastsaver --desktop-icon >/dev/null
[ "$auto" -eq 1 ] && curl -fsSL "$RAW/enable-auto-updates.sh" | sh

if [ -n "${WAYLAND_DISPLAY:-}" ]; then
    toastsaver-idle --restart
    sleep 1
    toastsaver-idle --check || true
fi
echo
echo "Done: toastsaver $(toastsaver --version) from apt. Future updates: sudo apt upgrade"
