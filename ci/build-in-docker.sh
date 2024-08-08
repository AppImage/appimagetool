#! /bin/bash

set -euxo pipefail

if [[ "${ARCH:-}" == "" ]]; then
    echo "Usage: env ARCH=... bash $0"
    exit 1
fi

case "$ARCH" in
    x86_64)
        image_prefix=amd64
        platform=linux/amd64
        ;;
    i686)
        image_prefix=i386
        platform=linux/i386
        ;;
    armhf)
        image_prefix=arm32v7
        platform=linux/arm/v7
        ;;
    aarch64)
        image_prefix=arm64v8
        platform=linux/arm64/v8
        ;;
    loong64)
        # create docker image
        mkdir -p tmp
        cd tmp
        # edge-9c13e7  v3.18   Neither of them is able to be used.
        wget -c https://dev.alpinelinux.org/~loongarch/edge/releases/loongarch64/alpine-minirootfs-edge-240514-loongarch64.tar.gz
        # Actually: Alpine Linux 3.20.0_alpha20240329 (edge)
        docker import alpine-minirootfs-*.tar.gz "loong64/alpine:3.19" --platform "linux/loong64"
        cd ..
        # create docker image end
        image_prefix=loong64 # official unsatble
        platform=linux/loong64
        ;;
    *)
        echo "unknown architecture: $ARCH"
        exit 2
        ;;
esac

# libassuan-static is supported only from 3.19 onwards
image="$image_prefix"/alpine:3.19

repo_root="$(readlink -f "$(dirname "${BASH_SOURCE[0]}")"/..)"

# run the build with the current user to
#   a) make sure root is not required for builds
#   b) allow the build scripts to "mv" the binaries into the /out directory
uid="$(id -u)"

if [ "$ARCH" != "loong64" ];then # loong64 is created locally, so it cannot be pulled.
    # make sure Docker image is up to date
    docker pull "$image"
fi

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

apk add --no-cache bash git gcc g++ cmake make file desktop-file-utils wget \
    gpgme-dev libgcrypt-dev libgcrypt-static argp-standalone zstd-dev zstd-static util-linux-static \
    glib-static libassuan-static zlib-static libgpg-error-static \
    curl-dev curl-static nghttp2-static libidn2-static openssl-libs-static brotli-static c-ares-static libunistring-static \
    libunistring-dev libpsl-static

# libcurl's pkg-config scripts are broken. everywhere, everytime.
# these additional flags have been collected from all the .pc files whose libs are mentioned as -l<lib> in Libs.private
# first, let's make sure there is no Requires.private section
grep -qv Requires.private /usr/lib/pkgconfig/libcurl.pc
# now, let's add one
echo "Requires.private: libcares libnghttp2 libidn2 libssl openssl libcrypto libbrotlicommon zlib" | tee -a /usr/lib/pkgconfig/libcurl.pc

# in a Docker container, we can safely disable this check
git config --global --add safe.directory '*'

bash -euxo pipefail /source/ci/install-static-mksquashfs.sh 4.6.1

bash -euxo pipefail /source/ci/build.sh

chown "$OUT_UID" appimagetool*.AppImage

EOF
