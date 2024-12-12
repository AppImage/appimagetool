#! /bin/bash

set -euxo pipefail

if [[ "${1:-}" == "" ]]; then
    echo "Usage: $0 <version>"
    exit 2
fi

version="$1"

build_dir="$(mktemp -d -t zsyncmake-build-XXXXXX)"

cleanup () {
    if [ -d "$build_dir" ]; then
        rm -rf "$build_dir"
    fi
}
trap cleanup EXIT

pushd "$build_dir"

wget http://zsync.moria.org.uk/download/zsync-"$version".tar.bz2 -q
tar xf zsync-*.tar.bz2

cd zsync-*/

find . -type f -exec sed -i -e 's|off_t|size_t|g' {} \;

./configure CFLAGS=-no-pie LDFLAGS=-static --prefix=/usr --build=$(arch)-unknown-linux-gnu

if [[ "${GITHUB_ACTIONS:-}" != "" ]]; then
    jobs="$(nproc)"
else
    jobs="$(nproc --ignore=1)"
fi

make -j"$jobs" install