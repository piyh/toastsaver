#!/bin/sh
# Runs the tests in parallel, in prepared Debian images (tests/Dockerfile).
#   sh tests/run.sh         quick, well under a minute: make check and the end-to-end
#                           suite from source on trixie and bookworm, plus the native
#                           .deb built and put through the suite on trixie, all at once
#   sh tests/run.sh full    also the .deb on bookworm, lintian, and the emulated armhf
#                           and amd64 packages (built and install-checked; minutes)
# The first run builds the images (a few minutes); after that Docker reuses them, and a
# run downloads nothing.
# shellcheck disable=SC2317  # the job functions below run indirectly, through job()
set -u
cd "$(dirname "$0")/.." || exit 1
MODE=${1:-quick}
V=$(sed -n '1s/.*(\(.*\)).*/\1/p' debian/changelog)
case "$(docker info --format '{{.Architecture}}')" in
    aarch64|arm64) NATIVE=arm64 ;;
    *) NATIVE=amd64 ;;
esac
LOGS=$(mktemp -d)
START=$(date +%s)

# image SUITE KIND [PLATFORM]: the prepared image's tag (build it first with prepare).
image() { echo "toastsaver-test:$1-$2${3:+-$(echo "$3" | tr / -)}"; }
prepare() {
    docker build -q ${3:+--platform "$3"} --build-arg SUITE="$1" --build-arg KIND="$2" \
        -t "$(image "$@")" - <tests/Dockerfile >/dev/null
}

# job NAME COMMAND...: runs in the background, output and exit code saved under $LOGS.
job() {
    name=$1; shift
    ( "$@" >"$LOGS/$name.log" 2>&1; echo $? >"$LOGS/$name.rc" ) &
}

e2e() {   # e2e SUITE [DEB]: from source, or the given package from dist/
    kind=src; [ -n "${2:-}" ] && kind=deb
    docker run --rm ${2:+-e DEB="$2"} -v "$PWD":/src "$(image "$1" "$kind")" sh /src/tests/pi-os-test.sh
}

native_deb() {   # build the native .deb, then run the suite on it (both suites for full)
    IMAGE=$(image bookworm src) LINTIAN=$LINT sh packaging/build-deb.sh "$NATIVE" bookworm || return 1
    deb="toastsaver_${V}_${NATIVE}.deb"
    if [ "$MODE" = full ]; then
        e2e trixie "$deb" >"$LOGS/deb-trixie.out" 2>&1 & t=$!
        e2e bookworm "$deb" >"$LOGS/deb-bookworm.out" 2>&1 & b=$!
        wait "$t"; rt=$?; wait "$b"; rb=$?
        cat "$LOGS/deb-trixie.out" "$LOGS/deb-bookworm.out"
        [ "$rt" = 0 ] && [ "$rb" = 0 ]
    else
        e2e trixie "$deb"
    fi
}

emulated_deb() {   # emulated_deb ARCH PLATFORM: build under QEMU, then install and render
    IMAGE=$(image bookworm src "$2") LINTIAN=1 sh packaging/build-deb.sh "$1" bookworm || return 1
    docker run --rm --platform "$2" -v "$PWD":/src "$(image bookworm deb "$2")" sh -c "
        export DEBIAN_FRONTEND=noninteractive
        apt-get install -y -qq /src/dist/toastsaver_${V}_$1.deb >/dev/null 2>&1 || { echo FAIL: install; exit 1; }
        toastsaver --version && toastsaver --list | tr '\n' ' ' && echo
        toastsaver --snapshot /tmp/p.png --size 320x180 --seconds 3 pipes && echo 'render ok'"
}

case "$MODE" in
    quick) LINT=0 ;;
    full) LINT=1 ;;
    *) echo "usage: sh tests/run.sh [quick|full]"; exit 2 ;;
esac

echo "Preparing test images..."
prepare trixie src & prepare bookworm src & prepare trixie deb &
if [ "$MODE" = full ]; then
    prepare bookworm deb &
    for p in linux/arm/v7 linux/amd64; do prepare bookworm src $p & prepare bookworm deb $p & done
fi
wait
READY=$(date +%s)

echo "Running ($MODE)..."
job source-trixie e2e trixie
job source-bookworm e2e bookworm
job "deb-$NATIVE" native_deb
if [ "$MODE" = full ]; then
    job deb-armhf emulated_deb armhf linux/arm/v7
    job deb-amd64 emulated_deb amd64 linux/amd64
fi
wait

bad=0
for rc in "$LOGS"/*.rc; do
    name=$(basename "$rc" .rc); log="$LOGS/$name.log"
    if [ "$(cat "$rc")" = 0 ] && ! grep -qE "^(FAIL|WARN)" "$log"; then
        echo "PASS  $name"
    else
        echo "FAIL  $name"; bad=1
        grep -E "^(FAIL|WARN)|^E:" "$log" | sed 's/^/      /'; tail -5 "$log" | sed 's/^/      /'
    fi
    grep -E "ALL PASSED|all dice OK|render ok|^[EW]: " "$log" | sed 's/^/      /'
done
END=$(date +%s)
echo "$( [ $bad = 0 ] && echo "All passed" || echo "FAILED" ) in $((END - READY)) s (images ready after $((READY - START)) s). Logs: $LOGS"
exit $bad
