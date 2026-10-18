#!/bin/sh
# Runs the end-to-end suite against the built packages, plus install smoke tests
# for the other architectures (emulated). Usage: sh packaging/test-debs.sh
set -u
V=$(sed -n '1s/.*(\(.*\)).*/\1/p' debian/changelog)
for suite in trixie bookworm; do
    echo "== $suite arm64 .deb (full suite)"
    docker run --rm --platform linux/arm64 -e DEB="toastsaver_${V}_arm64.deb" -v "$PWD":/src "debian:$suite" \
        sh /src/tests/pi-os-test.sh 2>&1 | grep -E "FAIL|ALL PASSED|purge clean|Depends:"
done
for pair in linux/arm/v7:armhf linux/amd64:amd64; do
    platform=${pair%:*} arch=${pair#*:}
    echo "== $arch install smoke (bookworm, emulated)"
    docker run --rm --platform "$platform" -v "$PWD":/src debian:bookworm sh -c "
        export DEBIAN_FRONTEND=noninteractive; apt-get update -qq >/dev/null
        apt-get install -y -qq /src/dist/toastsaver_${V}_${arch}.deb >/dev/null 2>&1 || { echo FAIL: install; exit 1; }
        toastsaver --version && toastsaver --list | tr '\n' ' ' && echo
        toastsaver --snapshot /tmp/p.png --size 320x180 --seconds 3 pipes && echo 'render ok'" 2>&1 | grep -v debconf
done
