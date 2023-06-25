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
    *)
        echo "unknown architecture: $ARCH"
        exit 2
        ;;
esac

image="$image_prefix"/alpine:3.18

repo_root="$(readlink -f "$(dirname "${BASH_SOURCE[0]}")"/..)"

# run the build with the current user to
#   a) make sure root is not required for builds
#   b) allow the build scripts to "mv" the binaries into the /out directory
uid="$(id -u)"

# mount workspace read-only, trying to make sure the build doesn't ever touch the source code files
# of course, this only works reliably if you don't run this script from that directory
# but it's still not the worst idea to do so
docker run --platform "$platform" \
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
apk add bash git gcc g++ cmake make musl-dev gpgme-dev libgcrypt-dev argp-standalone file desktop-file-utils wget zstd-dev zstd-static
bash -euxo pipefail /source/ci/build.sh
chown "$OUT_UID" appimagetool*.AppImage
EOF
