#!/bin/sh
# Builds a signed APT repository in REPO_DIR from the .deb files in DEB_DIR.
# Existing packages in REPO_DIR/pool are kept, so older versions stay installable.
# Needs: apt-utils (apt-ftparchive), gnupg, and the signing key already imported.
# Usage: sh packaging/make-apt-repo.sh DEB_DIR REPO_DIR KEY_ID
set -eu
DEBS=$1 REPO=$2 KEY=$3
POOL="$REPO/pool/main/t/toastsaver"
mkdir -p "$POOL"
cp "$DEBS"/*.deb "$POOL"/

cd "$REPO"
for arch in amd64 arm64 armhf; do
    dir=dists/stable/main/binary-$arch
    mkdir -p "$dir"
    apt-ftparchive --arch "$arch" packages pool > "$dir/Packages"
    gzip -9kf "$dir/Packages"
done
apt-ftparchive \
    -o APT::FTPArchive::Release::Origin=toastsaver \
    -o APT::FTPArchive::Release::Label=toastsaver \
    -o APT::FTPArchive::Release::Suite=stable \
    -o APT::FTPArchive::Release::Codename=stable \
    -o APT::FTPArchive::Release::Architectures="amd64 arm64 armhf" \
    -o APT::FTPArchive::Release::Components=main \
    -o APT::FTPArchive::Release::Description="toastsaver screensavers" \
    release dists/stable > dists/stable/Release
rm -f dists/stable/InRelease dists/stable/Release.gpg
gpg --batch --yes --default-key "$KEY" --clearsign -o dists/stable/InRelease dists/stable/Release
gpg --batch --yes --default-key "$KEY" -abs -o dists/stable/Release.gpg dists/stable/Release
gpg --batch --yes --export "$KEY" > toastsaver.gpg
gpg --batch --yes --armor --export "$KEY" > toastsaver.asc
echo "repo ready: $(find pool -name '*.deb' | wc -l) packages"
