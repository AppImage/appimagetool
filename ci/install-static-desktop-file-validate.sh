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
