#!/bin/sh
# Publishes a release from this machine, with no CI service involved.
#   sh packaging/release.sh             publish the version at the top of debian/changelog
#   sh packaging/release.sh --dry-run   everything up to publishing: checks, full tests,
#                                       packages, Mac app, signed APT repo (in a temp folder)
# Steps:
#   1. checks: on main, clean tree, versions agree, tag not used yet, signing key readable
#   2. tests: sh tests/run.sh full (this also builds the three .deb files)
#   3. the Mac app zip (when run on macOS)
#   4. tag vX.Y.Z and push main and the tag to GitHub
#   5. a GitHub release with the packages attached (a gh API call, not a workflow)
#   6. the signed APT repository, pushed to the gh-pages branch (served by GitHub Pages)
#   7. the AUR PKGBUILD's checksum for the new tarball, committed and pushed
#   8. once the site has updated: a fresh install and an upgrade from the live repository
# The APT signing key never leaves this machine: it's read from TOASTSAVER_SIGNING_KEY
# (a file; default ~/toastsaver-apt-signing-key.PRIVATE.asc) or the output of
# TOASTSAVER_SIGNING_KEY_CMD (e.g. a password manager's CLI), mounted read-only into a
# throwaway container and imported into a temporary keyring there.
set -eu
cd "$(dirname "$0")/.."
DRY=0
[ "${1:-}" = "--dry-run" ] && DRY=1

say() { printf '\n==> %s\n' "$*"; }
die() { printf 'release: %s\n' "$*" >&2; exit 1; }

V=$(sed -n '1s/.*(\(.*\)).*/\1/p' debian/changelog)
PREV=$(sed -n 's/^toastsaver (\(.*\)).*/\1/p' debian/changelog | sed -n 2p)
TAG="v$V"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

say "1. checks for $TAG (previous: v$PREV)"
[ "$(git rev-parse --abbrev-ref HEAD)" = main ] || die "not on main"
[ -z "$(git status --porcelain)" ] || die "uncommitted changes; commit or stash them first"
grep -q "#define VERSION \"$V\"" src/engine.h || die "src/engine.h VERSION is not $V"
grep -q "\"toastsaver $V\"" man/toastsaver.1 || die "man/toastsaver.1 is not $V"
if [ "$DRY" = 0 ]; then
    git fetch -q origin
    [ -z "$(git ls-remote --tags origin "$TAG")" ] || die "$TAG is already on GitHub"
    [ "$(git rev-list --count HEAD..origin/main)" = 0 ] || die "origin/main has commits this checkout doesn't"
    if ! { command -v gh >/dev/null && gh auth status >/dev/null 2>&1; }; then die "gh isn't signed in (gh auth login)"; fi
fi
KEYFILE="$WORK/signing-key.asc"
if [ -n "${TOASTSAVER_SIGNING_KEY_CMD:-}" ]; then
    sh -c "$TOASTSAVER_SIGNING_KEY_CMD" >"$KEYFILE" || die "TOASTSAVER_SIGNING_KEY_CMD failed"
else
    cp "${TOASTSAVER_SIGNING_KEY:-$HOME/toastsaver-apt-signing-key.PRIVATE.asc}" "$KEYFILE" 2>/dev/null \
        || die "signing key not found (set TOASTSAVER_SIGNING_KEY or TOASTSAVER_SIGNING_KEY_CMD)"
fi
chmod 600 "$KEYFILE"
grep -q "BEGIN PGP PRIVATE KEY BLOCK" "$KEYFILE" || die "the signing key isn't an armored private key"
echo "ok"

say "2. full tests (builds the packages)"
rm -f dist/toastsaver_*.deb dist/Toastsaver-mac.zip
sh tests/run.sh full || die "tests failed"
for a in amd64 arm64 armhf; do
    [ -f "dist/toastsaver_${V}_$a.deb" ] || die "dist/toastsaver_${V}_$a.deb missing after the tests"
done
ASSETS="dist/toastsaver_${V}_amd64.deb dist/toastsaver_${V}_arm64.deb dist/toastsaver_${V}_armhf.deb"

if [ "$(uname)" = Darwin ]; then
    say "3. Mac app"
    make -s >/dev/null && sh packaging/macos/build-app.sh | tail -1
    ASSETS="$ASSETS dist/Toastsaver-mac.zip"
else
    say "3. Mac app: skipped (not on macOS)"
fi

say "6. signed APT repository (built in a container)"
# Start from what's published, so older versions stay installable.
REPO="$WORK/gh-pages"
git fetch -q origin gh-pages
git worktree add -q --detach "$REPO" origin/gh-pages
mkdir -p "$WORK/debs" && cp dist/toastsaver_"${V}"_*.deb "$WORK/debs/"
docker run --rm -v "$PWD":/src:ro -v "$WORK/debs":/debs:ro -v "$REPO":/repo -v "$KEYFILE":/key.asc:ro \
    -w /src toastsaver-test:bookworm-src sh -euc '
    export GNUPGHOME=$(mktemp -d)
    gpg --batch --quiet --import /key.asc
    key=$(gpg --list-secret-keys --with-colons | awk -F: "/^fpr/ {print \$10; exit}")
    sh packaging/make-apt-repo.sh /debs /repo "$key"
    cp site/setup.sh site/index.html /repo/ && touch /repo/.nojekyll'
git -C "$REPO" add -A
git -C "$REPO" commit -q -m "APT repository for $TAG"
echo "gh-pages commit ready: $(git -C "$REPO" log --oneline -1)"

if [ "$DRY" = 1 ]; then
    say "dry run: stopping before anything is published"
    echo "would tag $TAG, push main and the tag, create the GitHub release with:"
    for f in $ASSETS; do echo "  $f"; done
    echo "and push the gh-pages commit above. Nothing was published."
    git worktree remove --force "$REPO"
    exit 0
fi

say "4. tag $TAG and push"
git tag -a "$TAG" -m "toastsaver $V"
git push -q --no-verify origin main "$TAG"   # the full tests already ran

say "5. GitHub release"
sed -n '3,/^ --/p' debian/changelog | sed '$d' >"$WORK/notes.md"
# shellcheck disable=SC2086  # one argument per file is intended
gh release create "$TAG" $ASSETS --title "toastsaver $TAG" --notes-file "$WORK/notes.md"

say "6. publish the APT repository"
git -C "$REPO" push -q --no-verify origin HEAD:gh-pages
git worktree remove --force "$REPO"

say "7. AUR checksum"
curl -fsSL -o "$WORK/src.tar.gz" "https://github.com/piyh/toastsaver/archive/refs/tags/$TAG.tar.gz"
SUM=$(shasum -a 256 "$WORK/src.tar.gz" 2>/dev/null || sha256sum "$WORK/src.tar.gz")
SUM=${SUM%% *}
sed -i.bak -e "s/^pkgver=.*/pkgver=$V/" -e "s/^sha256sums=.*/sha256sums=('$SUM')/" packaging/aur/PKGBUILD
sed -i.bak -e "s/pkgver = .*/pkgver = $V/" -e "s/sha256sums = .*/sha256sums = $SUM/" \
    -e "s|toastsaver-[0-9.]*\.tar\.gz::\(.*\)/v[0-9.]*\.tar\.gz|toastsaver-$V.tar.gz::\1/$TAG.tar.gz|" packaging/aur/.SRCINFO
rm -f packaging/aur/PKGBUILD.bak packaging/aur/.SRCINFO.bak
git add packaging/aur/PKGBUILD packaging/aur/.SRCINFO
git commit -q -m "AUR: $V with its real checksum"
git push -q --no-verify origin main

say "8. check the live repository (waiting for GitHub Pages to update)"
i=0
until curl -fsSL "https://piyh.github.io/toastsaver/dists/stable/main/binary-arm64/Packages" 2>/dev/null | grep -q "^Version: $V$"; do
    i=$((i + 1)); [ $i -lt 60 ] || die "the site still doesn't list $V after 10 minutes"
    sleep 10
done
docker run --rm debian:bookworm sh -c "
    apt-get update -qq >/dev/null && apt-get install -y -qq curl ca-certificates sudo >/dev/null 2>&1
    curl -fsSL https://piyh.github.io/toastsaver/setup.sh | sh >/dev/null 2>&1
    echo \"fresh install: \$(dpkg-query -W -f='\${Version}' toastsaver)\"
    apt-get install -y -qq --allow-downgrades toastsaver=$PREV >/dev/null 2>&1
    apt-get upgrade -y -qq >/dev/null 2>&1
    echo \"upgrade from $PREV: \$(dpkg-query -W -f='\${Version}' toastsaver)\"" | tee "$WORK/check"
if ! { grep -q "fresh install: $V" "$WORK/check" && grep -q "upgrade from $PREV: $V" "$WORK/check"; }; then
    die "live check failed"
fi
say "released $TAG"
