#! /bin/bash

set -euxo pipefail

if [[ "${1:-}" == "" ]]; then
    echo "Usage: $0 <version>"
    exit 2
fi

version="$1"

build_dir="$(mktemp -d -t mksquashfs-build-XXXXXX)"

cleanup () {
    if [ -d "$build_dir" ]; then
        rm -rf "$build_dir"
    fi
}
trap cleanup EXIT

pushd "$build_dir"

wget https://github.com/plougher/squashfs-tools/archive/refs/tags/"$version".tar.gz

# Verify tarball hash for supply chain security
expected_hash="9c4974e07c61547dae14af4ed1f358b7d04618ae194e54d6be72ee126f0d2f53"
echo "Verifying mksquashfs tarball hash..."
echo "$expected_hash  $version.tar.gz" | sha256sum -c || {
    echo "ERROR: mksquashfs tarball hash verification failed"
    echo "Expected: $expected_hash"
    echo "Got:      $(sha256sum $version.tar.gz | awk '{print $1}')"
    exit 1
}
echo "Tarball hash verified successfully"

tar xvz -f "$version".tar.gz --strip-components=1

cd squashfs-tools

if [[ "${GITHUB_ACTIONS:-}" != "" ]]; then
    jobs="$(nproc)"
else
    jobs="$(nproc --ignore=1)"
fi

make -j"$jobs" GZIP_SUPPORT=0 XZ_SUPPORT=0 LZO_SUPPORT=0 LZ4_SUPPORT=0 ZSTD_SUPPORT=1 COMP_DEFAULT=zstd LDFLAGS=-static USE_PREBUILT_MANPAGES=y install
