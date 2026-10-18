#!/bin/sh
# Builds the .deb inside a Debian container for one architecture and runs lintian.
# Usage (from the repo):  sh packaging/build-deb.sh arm64|armhf|amd64 [bookworm]
# Output: dist/toastsaver_<version>_<arch>.deb
set -eu
ARCH=${1:?arch}
SUITE=${2:-bookworm}
case "$ARCH" in
    amd64) PLATFORM=linux/amd64 ;;
    arm64) PLATFORM=linux/arm64 ;;
    armhf) PLATFORM=linux/arm/v7 ;;
    *) echo "unknown arch $ARCH" >&2; exit 2 ;;
esac
mkdir -p dist
docker run --rm --platform "$PLATFORM" -v "$PWD":/src "debian:$SUITE" sh -euc '
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq >/dev/null
    apt-get install -y -qq --no-install-recommends build-essential debhelper pkgconf libsdl2-dev lintian >/dev/null
    cp -r /src /build && cd /build && rm -rf bin dist
    dpkg-buildpackage -us -uc -b >/tmp/build.log 2>&1 || { tail -40 /tmp/build.log; exit 1; }
    cp ../toastsaver_*.deb /src/dist/
    echo "--- lintian"
    lintian --tag-display-limit 0 ../toastsaver_*.deb || true
'
ls -l dist/*_"$ARCH".deb
