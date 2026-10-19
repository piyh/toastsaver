#!/bin/sh
# End-to-end test in a throwaway Debian container (the base of Raspberry Pi OS).
# Installs toastsaver (from source with install.sh, or a .deb with apt if DEB is
# set), starts a headless compositor, and checks that:
#   1. the screensaver appears after the idle timeout and leaves on a key press
#   2. an app that inhibits idle (like a playing video) stops it from starting
#   3. a screensaver over the picker can be dismissed without closing the picker
#   4. toastsaver-idle --now pipes starts that saver
#   5. settings: --set keeps the file's comments, --restart swaps the watcher,
#      ENABLED=0 stops it, the app opens, a saver runs with the clock on
# Usually run through tests/run.sh, which uses prepared images (tests/Dockerfile) and
# runs the suites in parallel. It also works on a plain Debian image, as CI does:
#   docker run --rm -v "$PWD":/src debian:trixie sh /src/tests/pi-os-test.sh
#   docker run --rm -e DEB=toastsaver_1.0.0_arm64.deb -v "$PWD":/src debian:trixie sh /src/tests/pi-os-test.sh
set -eu
export DEBIAN_FRONTEND=noninteractive
DEB=${DEB:-}
if [ -f /etc/toastsaver-test-image ]; then
    # Prepared image: everything is already here.
    COMP=labwc; command -v labwc >/dev/null || COMP=sway
else
    apt-get update -qq >/dev/null
    # Pi OS ships labwc; plain Debian bookworm doesn't, so fall back to sway (also wlroots).
    COMP=labwc; apt-cache show labwc >/dev/null 2>&1 || COMP=sway
    # Test tools only. Graphics libraries come from the package's own dependencies (DEB mode).
    apt-get install -y -qq $COMP grim wtype shellcheck procps sudo >/dev/null
    # A source install needs the compiler before the build-warnings check runs.
    [ -n "$DEB" ] || apt-get install -y -qq build-essential pkgconf libsdl2-dev >/dev/null
fi
echo "--- shellcheck"
shellcheck -s sh /src/install.sh /src/uninstall.sh /src/extras/*.sh /src/scripts/toastsaver-idle \
    /src/tests/run.sh /src/packaging/build-deb.sh /src/packaging/release.sh /src/.githooks/pre-push || { echo "FAIL: shellcheck"; exit 1; }
echo "clean"

# A disposable user with passwordless sudo, so installs run unattended (container only).
useradd -m pi
mkdir -p /home/pi/Desktop
cp -r /src /home/pi/toastsaver && rm -rf /home/pi/toastsaver/bin
chown -R pi:pi /home/pi/toastsaver /home/pi/Desktop
echo "pi ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/pi

if [ -n "$DEB" ]; then
    echo "--- apt install ./$DEB (pulls in its own dependencies)"
    apt-get install -y -qq "/src/dist/$DEB" >/dev/null
    dpkg -s toastsaver | grep -E "^(Version|Depends):"
    PREFIX=/usr
else
    PREFIX=/usr/local
fi

# Waits are polls ("until the screensaver is running, at most N seconds"), not fixed
# sleeps; the only fixed pauses are the idle timeout itself and the saver's 1.2 s grace
# period, during which it ignores input on purpose.
su pi -s /bin/sh -c "COMP=$COMP; PREFIX=$PREFIX; DEB=$DEB; MAKEFLAGS=-j\$(nproc); export MAKEFLAGS; "'
set -eu
fail() { echo "FAIL: $*"; exit 1; }
count() { pgrep -c -x toastsaver || true; }
savers() { [ "$(count)" = "$1" ]; }
watchers() { [ "$(pgrep -c -x swayidle || true)" = "$1" ]; }
running() { pgrep -a -x toastsaver | grep -q "$1"; }
has_wayland() { ls "$XDG_RUNTIME_DIR" | grep -q "^wayland-[0-9]*$"; }
until_() { n=$(( $1 * 10 )); shift; while ! "$@"; do n=$((n - 1)); [ $n -gt 0 ] || return 1; sleep 0.1; done; }
GRACE=1.4   # a new saver ignores input for its first 1.2 s

cd ~/toastsaver
if [ -z "$DEB" ]; then
    echo "--- build warnings"; make clean >/dev/null
    W=$(make 2>&1 | grep -Ei "warning|error" || true)
    if [ -n "$W" ]; then echo "$W" | sed "s/^/WARN /"; else echo "none"; fi
    echo "--- make check"; make -s check 2>&1 | tail -1
    echo "--- installer"; ./install.sh 2>&1 | grep "==>"
else
    toastsaver --desktop-icon
fi
ls /etc/xdg/autostart/toastsaver.desktop $PREFIX/share/applications/toastsaver-picker.desktop \
   $PREFIX/bin/toastsaver-settings \
   $PREFIX/bin/toastsaver-picker $PREFIX/share/icons/hicolor/128x128/apps/toastsaver.png \
   ~/Desktop/Screensavers.desktop >/dev/null
grep -q "Exec=$PREFIX/bin/toastsaver-idle" /etc/xdg/autostart/toastsaver.desktop || fail "autostart has wrong path"
# Docker images skip installing man pages, so for the package check its file list.
if [ -n "$DEB" ]; then dpkg -L toastsaver | grep -q "man1/toastsaver-picker.1" || fail "man page missing from package"
else [ -e $PREFIX/share/man/man1/toastsaver-picker.1 ] || fail "man page missing"; fi
echo "files in place under $PREFIX: ok"
echo "savers: $(toastsaver --list | tr "\n" " ")"

export XDG_RUNTIME_DIR=/tmp/xdg; mkdir -p -m 700 $XDG_RUNTIME_DIR
export WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1
$COMP >/tmp/compositor.log 2>&1 &
until_ 10 has_wayland || { tail -5 /tmp/compositor.log; fail "compositor did not start"; }
export WAYLAND_DISPLAY=$(ls $XDG_RUNTIME_DIR | grep -m1 "^wayland-[0-9]*$")
echo "--- compositor $COMP on $WAYLAND_DISPLAY"

IDLE_SECONDS=2 SCREEN_OFF_SECONDS=0 toastsaver-idle &   # fast timings for the first checks
until_ 5 watchers 1 || fail "idle watcher did not start"
toastsaver-idle; watchers 1 || fail "more than one idle watcher"
echo "single instance: ok"

echo "--- 1. idle -> screensaver -> key -> gone"
until_ 6 savers 1 || { pgrep -a swayidle; pgrep -a toastsaver; tail -5 /tmp/compositor.log; fail "screensaver did not start"; }
sleep $GRACE; grim /tmp/s1.png
wtype a
until_ 3 savers 0 || fail "screensaver still running after a key"
echo "ok"

echo "--- 2. an idle inhibitor (like a playing video) blocks it"
toastsaver --inhibit --windowed mystify &
INH=$!
sleep 3   # longer than the 2 s idle timeout
savers 1 || fail "screensaver started even though idle was inhibited ($(count) running)"
echo "blocked while inhibited: ok"
kill $INH; wait $INH 2>/dev/null || true
until_ 6 savers 1 || fail "screensaver did not start after the inhibitor went away"
sleep $GRACE; wtype a
until_ 3 savers 0 || fail "screensaver still running"
echo "starts again once the inhibitor is gone: ok"

echo "--- 3. picker survives a screensaver dismissed over it"
toastsaver-picker &
PICK=$!
until_ 8 savers 1 || fail "screensaver did not appear over the picker"
sleep $GRACE; wtype a
until_ 3 savers 0 || fail "screensaver still running"
kill -0 $PICK 2>/dev/null || fail "picker was closed too"
echo "ok"
kill $PICK; wait $PICK 2>/dev/null || true

echo "--- 4. toastsaver-idle --now pipes"
toastsaver-idle --now pipes &
until_ 5 running pipes || fail "pipes not running"
sleep 0.5; grim /tmp/pipes.png
pkill -x toastsaver || true
echo "ok"

echo "--- 5. settings"
toastsaver --set CLOCK=1 >/dev/null && toastsaver --set IDLE_MINUTES=10 >/dev/null
grep -q "^CLOCK=1 .*# 1 = show a clock" ~/.config/toastsaver.conf || fail "comment lost or CLOCK not saved"
grep -q "^IDLE_MINUTES=10 .*# minutes" ~/.config/toastsaver.conf || fail "IDLE_MINUTES not saved with its comment"
echo "--set keeps comments: ok"
OLD=$(pgrep -x swayidle)
# Settled on one new watcher with the new timings. (While the old one exits it briefly
# forks a child to run its resume command, which also shows up as "swayidle".)
replaced() { N=$(pgrep -x swayidle || true); [ -n "$N" ] && [ "$N" != "$OLD" ] && watchers 1 \
    && pgrep -a -x swayidle | grep -q "timeout 600"; }
toastsaver-idle --restart
until_ 5 replaced || { pgrep -a swayidle; fail "--restart did not leave one new watcher using IDLE_MINUTES=10"; }
echo "--restart picks up new timings: ok"
toastsaver --set ENABLED=0 >/dev/null; toastsaver-idle --restart
until_ 5 watchers 0 || fail "watcher still running with ENABLED=0"
toastsaver --set ENABLED=1 >/dev/null; toastsaver-idle --restart
until_ 5 watchers 1 || fail "watcher did not come back with ENABLED=1"
echo "ENABLED switch: ok"
pkill -x toastsaver || true
toastsaver-settings &
SET=$!; sleep 1
kill -0 $SET 2>/dev/null || fail "settings page did not stay open"
kill $SET
toastsaver-idle --now aquarium &
until_ 5 running aquarium || fail "saver with clock did not run"
pkill -x toastsaver || true
echo "settings page and clock: ok"

toastsaver-idle --check
'
# Screenshots for humans to look at (git-ignored); copied by root, not part of pass/fail.
mkdir -p /src/tests/out 2>/dev/null && TAG="$COMP-${DEB:-source}" \
    && cp /tmp/s1.png "/src/tests/out/$TAG-toasters.png" 2>/dev/null \
    && cp /tmp/pipes.png "/src/tests/out/$TAG-pipes.png" 2>/dev/null || true
if [ -n "$DEB" ]; then
    echo "--- apt remove, then purge"
    apt-get remove -y -qq toastsaver >/dev/null
    for f in /usr/bin/toastsaver /usr/bin/toastsaver-idle /usr/share/applications/toastsaver-picker.desktop; do
        [ ! -e "$f" ] || { echo "FAIL: $f still present after remove"; exit 1; }
    done
    # Debian keeps files in /etc until purge; TryExec makes the desktop ignore the leftover entry.
    grep -q "^TryExec=/usr/bin/toastsaver-idle" /etc/xdg/autostart/toastsaver.desktop || { echo "FAIL: leftover autostart lacks TryExec"; exit 1; }
    apt-get purge -y -qq toastsaver >/dev/null
    [ ! -e /etc/xdg/autostart/toastsaver.desktop ] || { echo "FAIL: autostart still present after purge"; exit 1; }
    echo "remove + purge clean: ok"
fi
echo "--- ALL PASSED ($COMP, ${DEB:-source install})"
