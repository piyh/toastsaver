#!/bin/sh
# Adds the toastsaver APT repository and installs toastsaver.
#   curl -fsSL https://piyh.github.io/toastsaver/setup.sh | sh
# It does exactly what the README's manual steps do:
set -eu
REPO=https://piyh.github.io/toastsaver
echo "Adding the toastsaver signing key and repository (asks for your password)..."
sudo install -d -m 0755 /etc/apt/keyrings
curl -fsSL "$REPO/toastsaver.gpg" | sudo tee /etc/apt/keyrings/toastsaver.gpg >/dev/null
echo "deb [signed-by=/etc/apt/keyrings/toastsaver.gpg] $REPO stable main" \
    | sudo tee /etc/apt/sources.list.d/toastsaver.list >/dev/null
sudo apt-get update
sudo apt-get install -y toastsaver
echo
echo "Installed. It starts automatically the next time you log in."
echo "Open 'Screensavers' from the menu (hold its gear for settings), or run: toastsaver-idle --restart"
