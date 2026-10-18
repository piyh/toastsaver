#!/bin/sh
# End-to-end test in a throwaway Debian ARM64 container (the base of Raspberry Pi OS).
# It builds and installs with the real installer, starts a headless compositor and checks:
#   1. the screensaver appears after the idle timeout and leaves on a key press
#   2. an app that inhibits idle (like a playing video) stops it from starting
#   3. a screensaver over the picker can be dismissed without closing the picker
#   4. toastsaver-idle --now pipes starts that saver
#   5. settings: --set keeps the file's comments, --restart swaps the watcher,
#      the settings window opens, and a saver runs with the clock on
# Usage (from the repo):  docker run --rm -v "$PWD":/src debian:trixie sh /src/tests/pi-os-test.sh
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
# Pi OS ships labwc; plain Debian bookworm doesn't, so fall back to sway (also wlroots).
COMP=labwc; apt-cache show labwc >/dev/null 2>&1 || COMP=sway
apt-get install -y -qq build-essential pkg-config libsdl2-dev swayidle $COMP grim wtype \
    shellcheck procps util-linux sudo libegl-mesa0 libgl1-mesa-dri >/dev/null
echo "--- shellcheck"
shellcheck -s sh /src/install.sh /src/uninstall.sh /src/deploy.sh /src/scripts/toastsaver-idle && echo "clean"

# A disposable user with passwordless sudo, so the installer can run unattended (container only).
useradd -m pi
mkdir -p /home/pi/Desktop
cp -r /src /home/pi/toastsaver && rm -rf /home/pi/toastsaver/bin
chown -R pi:pi /home/pi/toastsaver /home/pi/Desktop
echo "pi ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/pi

su pi -s /bin/sh -c "COMP=$COMP; "'
set -eu
fail() { echo "FAIL: $*"; exit 1; }
count() { pgrep -c -x toastsaver || true; }
cd ~/toastsaver
echo "--- build warnings"; make clean >/dev/null
W=$(make 2>&1 | grep -Ei "warning|error" || true)
if [ -n "$W" ]; then echo "$W" | sed "s/^/WARN /"; else echo "none"; fi
echo "--- installer"; ./install.sh 2>&1 | grep "==>"
ls ~/.config/autostart/toastsaver.desktop ~/.local/share/applications/toastsaver-picker.desktop \
   ~/.local/share/applications/toastsaver-settings.desktop /usr/local/bin/toastsaver-settings \
   ~/Desktop/Screensavers.desktop /usr/local/share/toastsaver/icon.png /usr/local/bin/toastsaver-picker
echo "savers: $(toastsaver --list | tr "\n" " ")"

export XDG_RUNTIME_DIR=/tmp/xdg; mkdir -p -m 700 $XDG_RUNTIME_DIR
export WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1
$COMP >/tmp/compositor.log 2>&1 &
sleep 3
export WAYLAND_DISPLAY=$(ls $XDG_RUNTIME_DIR | grep -m1 "^wayland-[0-9]*$")
echo "--- compositor $COMP on $WAYLAND_DISPLAY"

IDLE_SECONDS=3 SCREEN_OFF_SECONDS=0 toastsaver-idle &   # fast timings for the first checks
sleep 1
toastsaver-idle; [ "$(pgrep -c swayidle)" = 1 ] || fail "more than one idle watcher"
echo "single instance: ok"

echo "--- 1. idle -> screensaver -> key -> gone"
sleep 6
[ "$(count)" = 1 ] || fail "screensaver did not start"
sleep 1; grim /tmp/s1.png
wtype a; sleep 2
[ "$(count)" = 0 ] || fail "screensaver still running after a key"
echo "ok"

echo "--- 2. an idle inhibitor (like a playing video) blocks it"
toastsaver --inhibit --windowed mystify &
INH=$!
sleep 7
[ "$(count)" = 1 ] || fail "screensaver started even though idle was inhibited ($(count) running)"
echo "blocked while inhibited: ok"
kill $INH; sleep 6
[ "$(count)" = 1 ] || fail "screensaver did not start after the inhibitor went away"
wtype a; sleep 2
[ "$(count)" = 0 ] || fail "screensaver still running"
echo "starts again once the inhibitor is gone: ok"

echo "--- 3. picker survives a screensaver dismissed over it"
toastsaver-picker &
PICK=$!
sleep 7
[ "$(count)" = 1 ] || fail "screensaver did not appear over the picker"
wtype a; sleep 2
[ "$(count)" = 0 ] || fail "screensaver still running"
kill -0 $PICK 2>/dev/null || fail "picker was closed too"
echo "ok"
kill $PICK; sleep 1

echo "--- 4. toastsaver-idle --now pipes"
toastsaver-idle --now pipes &
sleep 4
pgrep -a -x toastsaver | grep -q pipes || fail "pipes not running"
grim /tmp/pipes.png
pkill -x toastsaver
echo "ok"

echo "--- 5. settings"
toastsaver --set CLOCK=1 >/dev/null && toastsaver --set IDLE_MINUTES=10 >/dev/null
grep -q "^CLOCK=1 .*# 1 = show a clock" ~/.config/toastsaver.conf || fail "comment lost or CLOCK not saved"
grep -q "^IDLE_MINUTES=10 .*# minutes" ~/.config/toastsaver.conf || fail "IDLE_MINUTES not saved with its comment"
echo "--set keeps comments: ok"
OLD=$(pgrep -x swayidle)
toastsaver-idle --restart; sleep 1
NEW=$(pgrep -x swayidle || true)
[ -n "$NEW" ] && [ "$NEW" != "$OLD" ] && [ "$(pgrep -c swayidle)" = 1 ] || fail "--restart did not replace the watcher"
pgrep -a swayidle | grep -q "timeout 600" || fail "new watcher is not using IDLE_MINUTES=10"
echo "--restart picks up new timings: ok"
toastsaver-settings &
SET=$!; sleep 2
kill -0 $SET 2>/dev/null || fail "settings window did not stay open"
kill $SET
toastsaver-idle --now aquarium &
sleep 3; pgrep -x toastsaver >/dev/null || fail "saver with clock did not run"
grim /tmp/clock.png; pkill -x toastsaver
echo "settings window and clock: ok"

cp /tmp/s1.png /src/tests/wayland-toasters.png
cp /tmp/pipes.png /src/tests/wayland-pipes.png
toastsaver-idle --check
'
echo "--- ALL PASSED ($COMP)"
