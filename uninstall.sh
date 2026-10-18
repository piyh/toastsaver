#!/bin/sh
# Removes a source install of toastsaver (from ./install.sh).
# For the apt package use: sudo apt remove toastsaver
set -u
cd "$(dirname "$0")" || exit 1
[ -x /usr/local/bin/toastsaver-idle ] && /usr/local/bin/toastsaver-idle --stop
pkill -x toastsaver 2>/dev/null
sudo make uninstall PREFIX=/usr/local
# Files written by older versions, and the desktop icon.
rm -f "$HOME/.config/autostart/toastsaver.desktop" \
      "$HOME/.local/share/applications/toastsaver-picker.desktop" \
      "$HOME/.local/share/applications/toastsaver-settings.desktop" \
      "$HOME/Desktop/Screensavers.desktop"
sudo rm -rf /usr/local/share/toastsaver    # icon location used by older versions
echo "Removed. Your settings file ~/.config/toastsaver.conf was kept."
