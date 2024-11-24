#! /bin/sh

set -euxo pipefail

source arch.src

if [[ "${ARCH:-}" == "" ]]; then
    echo "Usage: env ARCH=... bash $0"
    exit 1
fi

build_dir="$(mktemp -d -t appimagetool-build-XXXXXX)"

cleanup () {
    if [ -d "$build_dir" ]; then
        rm -rf "$build_dir"
    fi
}
trap cleanup EXIT


apk add bash git gcc g++ cmake make file desktop-file-utils wget \
    gpgme-dev libgcrypt-dev libgcrypt-static argp-standalone zstd-dev zstd-static util-linux-static \
    glib-static libassuan-static zlib-static libgpg-error-static \
    curl-dev curl-static nghttp2-static libidn2-static openssl-libs-static brotli-static c-ares-static libunistring-static


# store repo root as variable
#repo_root="$(readlink -f "$(dirname "${BASH_SOURCE[0]}")"/..)"
repo_root=/src
old_cwd="$(readlink -f "$PWD")"

#pushd "$build_dir"

/bin/bash /scripts/install-static-mksquashfs.sh 4.6.1

cmake "$repo_root" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_STATIC=ON

if [[ "${GITHUB_ACTIONS:-}" != "" ]]; then
    jobs="$(nproc)"
else
    jobs="$(nproc --ignore=1)"
fi

make -j"$jobs"

make install DESTDIR=AppDir

find AppDir

cp "$(which mksquashfs)" AppDir/usr/bin

cp "$repo_root"/resources/AppRun.sh AppDir/AppRun
chmod +x AppDir/AppRun

if [[ "$ARCH" == "x86" ]]; then
RELEASE_ARCH="i686"
fi

if [[ "$ARCH" == "x86_64" ]]; then
RELEASE_ARCH=${ARCH}
fi

wget https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-"$RELEASE_ARCH"

#pushd AppDir
ln -s usr/share/applications/appimagetool.desktop AppDir
ln -s usr/share/icons/hicolor/128x128/apps/appimagetool.png AppDir
ln -s usr/share/icons/hicolor/128x128/apps/appimagetool.png AppDir/.DirIcon
#popd

find AppDir
cat AppDir/appimagetool.desktop

AppDir/AppRun --runtime-file runtime-"$RELEASE_ARCH" AppDir

mv appimagetool-*.AppImage "$old_cwd"
