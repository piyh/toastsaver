# Debian images with what the tests need already in place, so a run downloads nothing.
# tests/run.sh builds them (Docker caches them after the first time). Two kinds:
#   KIND=src  compiler, SDL, debhelper and the test tools all installed; apt's sources
#             are removed, so install.sh's "apt-get install" finds everything at once.
#             Used for the from-source suite, building the .deb, and building and
#             signing the APT repository (packaging/release.sh).
#   KIND=deb  only the test tools installed. The package's libraries are downloaded into
#             apt's cache but NOT installed, so "apt install ./toastsaver.deb" still has
#             to pull them in through its own Depends (that's part of what's tested).
ARG SUITE=trixie
FROM debian:${SUITE}
ARG KIND=src
ENV DEBIAN_FRONTEND=noninteractive
# Pi OS ships labwc; plain Debian bookworm doesn't, so it gets sway (also wlroots).
RUN apt-get update -qq \
 && if apt-cache show labwc >/dev/null 2>&1; then C=labwc; else C=sway; fi \
 && apt-get install -y -qq $C grim wtype shellcheck procps sudo >/dev/null \
 && if [ "$KIND" = src ]; then \
        apt-get install -y -qq build-essential pkg-config libsdl2-dev debhelper lintian swayidle \
            apt-utils gnupg >/dev/null \
     && (apt-get install -y -qq wlopm >/dev/null 2>&1 || true) \
     && rm -f /etc/apt/sources.list /etc/apt/sources.list.d/*; \
    else \
        rm -f /etc/apt/apt.conf.d/docker-clean \
     && apt-get install -y -qq --download-only libsdl2-2.0-0 swayidle libegl1 libgl1 libegl-mesa0 libgl1-mesa-dri >/dev/null \
     && (apt-get install -y -qq --download-only wlopm >/dev/null 2>&1 || true); \
    fi \
 && echo "$KIND" > /etc/toastsaver-test-image
