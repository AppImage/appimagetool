#! /bin/bash

set -euxo pipefail

if [[ "${ARCH:-}" == "" ]]; then
    echo "Usage: env ARCH=... bash $0"
    exit 1
fi

case "$ARCH" in
    x86_64)
        platform=linux/amd64
        ;;
    i686)
        platform=linux/i386
        ;;
    armhf)
        platform=linux/arm/v7
        ;;
    aarch64)
        platform=linux/arm64/v8
        ;;
    *)
        echo "unknown architecture: $ARCH"
        exit 2
        ;;
esac


# libassuan-static is supported only from 3.19 onwards
image=alpine:3.19

repo_root="$(readlink -f "$(dirname "${BASH_SOURCE[0]}")"/..)"

# run the build with the current user to
#   a) make sure root is not required for builds
#   b) allow the build scripts to "mv" the binaries into the /out directory
uid="$(id -u)"

# make sure Docker image is up to date
docker pull "$image"

# mount workspace read-only, trying to make sure the build doesn't ever touch the source code files
# of course, this only works reliably if you don't run this script from that directory
# but it's still not the worst idea to do so
docker run \
    --platform "$platform" \
    --rm \
    -i \
    -e ARCH \
    -e GITHUB_ACTIONS \
    -e GITHUB_RUN_NUMBER \
    -e OUT_UID="$uid" \
    -v "$repo_root":/source:ro \
    -v "$PWD":/out \
    -w /out \
    "$image" \
    sh <<\EOF

set -euxo pipefail

apk add bash git gcc g++ cmake make file wget \
    gpgme-dev libgcrypt-dev libgcrypt-static argp-standalone zstd-dev zstd-static util-linux-static \
    glib-static libassuan-static zlib-static libgpg-error-static \
    curl-dev curl-static nghttp2-static libidn2-static openssl-libs-static brotli-static c-ares-static libunistring-static \
    glib-static glib-dev autoconf automake meson \
    libpsl-dev libpsl-static patch

# libcurl's pkg-config scripts are broken. everywhere, everytime.
# these additional flags have been collected from all the .pc files whose libs are mentioned as -l<lib> in Libs.private
# first, let's make sure there is no Requires.private section
grep -qv Requires.private /usr/lib/pkgconfig/libcurl.pc
# now, let's add one
echo "Requires.private: libcares libnghttp2 libidn2 libssl openssl libcrypto libbrotlicommon zlib" | tee -a /usr/lib/pkgconfig/libcurl.pc

# in a Docker container, we can safely disable this check
git config --global --add safe.directory '*'

bash -euxo pipefail /source/ci/install-static-desktop-file-validate.sh 0.28
bash -euxo pipefail /source/ci/install-static-mksquashfs.sh 4.6.1
bash -euxo pipefail /source/ci/install-static-zsyncmake.sh 0.6.2 0b9d53433387aa4f04634a6c63a5efa8203070f2298af72a705f9be3dda65af2

bash -euxo pipefail /source/ci/build.sh

chown "$OUT_UID" appimagetool*.AppImage

EOF
