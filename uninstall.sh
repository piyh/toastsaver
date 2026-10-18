#!/bin/sh
# Removes toastsaver.
set -u
pkill -f 'swayidle.*toastsaver' 2>/dev/null
pkill -x toastsaver 2>/dev/null
rm -f "$HOME/.config/autostart/toastsaver.desktop" \
      "$HOME/.local/share/applications/toastsaver-picker.desktop" \
      "$HOME/Desktop/Screensavers.desktop"
sudo rm -f /usr/local/bin/toastsaver /usr/local/bin/toastsaver-picker /usr/local/bin/toastsaver-idle
sudo rm -rf /usr/local/share/toastsaver
echo "Removed. Your settings file ~/.config/toastsaver.conf was kept."
