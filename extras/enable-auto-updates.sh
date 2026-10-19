#!/bin/sh
# Keeps toastsaver up to date by itself: once a day a systemd timer checks the toastsaver
# apt repository and upgrades toastsaver only (nothing else on the system is touched).
#   curl -fsSL https://raw.githubusercontent.com/piyh/toastsaver/main/extras/enable-auto-updates.sh | sh
# Undo:  sudo systemctl disable --now toastsaver-update.timer
#        sudo rm /etc/systemd/system/toastsaver-update.service /etc/systemd/system/toastsaver-update.timer
set -eu

if [ ! -f /etc/apt/sources.list.d/toastsaver.list ]; then
    echo "The toastsaver apt repository isn't set up yet. Install it first:"
    echo "  curl -fsSL https://piyh.github.io/toastsaver/setup.sh | sh"
    exit 1
fi

echo "==> Adding a daily toastsaver update check (asks for your password)"
sudo tee /etc/systemd/system/toastsaver-update.service >/dev/null <<'EOF'
[Unit]
Description=Update toastsaver from its apt repository
Wants=network-online.target
After=network-online.target

[Service]
Type=oneshot
Environment=DEBIAN_FRONTEND=noninteractive
# Refresh only toastsaver's package list (keeping the others), then upgrade only toastsaver.
ExecStart=/usr/bin/apt-get update -o Dir::Etc::sourcelist=sources.list.d/toastsaver.list -o Dir::Etc::sourceparts=- -o APT::Get::List-Cleanup=0
ExecStart=/usr/bin/apt-get install -y --only-upgrade -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold toastsaver
EOF
sudo tee /etc/systemd/system/toastsaver-update.timer >/dev/null <<'EOF'
[Unit]
Description=Check for toastsaver updates once a day

[Timer]
OnCalendar=daily
RandomizedDelaySec=1h
Persistent=true

[Install]
WantedBy=timers.target
EOF
sudo systemctl daemon-reload
sudo systemctl enable --now toastsaver-update.timer
echo "Done. toastsaver now updates itself once a day."
echo "Check on it:  systemctl list-timers toastsaver-update.timer"
echo "Update now:   sudo systemctl start toastsaver-update.service"
