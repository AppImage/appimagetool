#! /bin/bash

set -euxo pipefail

if [[ "${2:-}" == "" ]]; then
    echo "Usage: $0 <version> <hash>"
    exit 2
fi

version="$1"
hash="$2"

build_dir="$(mktemp -d -t zsyncmake-build-XXXXXX)"

cleanup () {
    if [ -d "$build_dir" ]; then
        rm -rf "$build_dir"
    fi
}
trap cleanup EXIT

pushd "$build_dir"

# the original zsync homepage has been shut down apparently, but we can fetch the source code from most distros
wget http://deb.debian.org/debian/pool/main/z/zsync/zsync_"$version".orig.tar.bz2
echo "${hash}  zsync_${version}.orig.tar.bz2" | sha256sum -c

tar xf zsync_"$version"*.tar.bz2

cd zsync-*/

find . -type f -exec sed -i -e 's|off_t|size_t|g' {} \;

./configure CFLAGS=-no-pie LDFLAGS=-static --prefix=/usr --build=$(arch)-unknown-linux-gnu

if [[ "${GITHUB_ACTIONS:-}" != "" ]]; then
    jobs="$(nproc)"
else
    jobs="$(nproc --ignore=1)"
fi

make -j"$jobs" install
