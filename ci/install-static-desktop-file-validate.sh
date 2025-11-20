#! /bin/bash

set -euxo pipefail

if [[ "${1:-}" == "" ]]; then
    echo "Usage: $0 <version>"
    exit 2
fi

version="$1"

build_dir="$(mktemp -d -t desktop-file-utils-build-XXXXXX)"

cleanup () {
    if [ -d "$build_dir" ]; then
        rm -rf "$build_dir"
    fi
}
trap cleanup EXIT

pushd "$build_dir"

# apk add glib-static glib-dev autoconf automake # Moved to build-in-docker.sh

wget -c "https://gitlab.freedesktop.org/xdg/desktop-file-utils/-/archive/"$version"/desktop-file-utils-"$version".tar.gz"

# Verify tarball hash for supply chain security
# Hash for version 0.28
expected_hash="379ecbc1354d0c052188bdf5dbbc4a020088ad3f9cab54487a5852d1743a4f3b"
if [[ "$version" == "0.28" ]]; then
    echo "Verifying desktop-file-utils tarball hash..."
    echo "$expected_hash  desktop-file-utils-$version.tar.gz" | sha256sum -c || {
        echo "ERROR: desktop-file-utils tarball hash verification failed"
        echo "Expected: $expected_hash"
        echo "Got:      $(sha256sum desktop-file-utils-$version.tar.gz)"
        exit 1
    }
    echo "Tarball hash verified successfully"
else
    echo "Warning: No hash verification available for desktop-file-utils version $version"
fi

tar xf desktop-file-utils-*.tar.gz
cd desktop-file-utils-*/

# setting LDFLAGS as suggested in https://mesonbuild.com/Creating-Linux-binaries.html#building-and-installing
env LDFLAGS=-static meson setup build --prefer-static --default-library=static

if [[ "${GITHUB_ACTIONS:-}" != "" ]]; then
    jobs="$(nproc)"
else
    jobs="$(nproc --ignore=1)"
fi

ninja -C build -j "$jobs" -v
ninja -C build -j 1 -v install
