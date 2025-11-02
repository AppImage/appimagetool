#! /bin/bash

set -euxo pipefail

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

# store repo root as variable
repo_root="$(readlink -f "$(dirname "${BASH_SOURCE[0]}")"/..)"
old_cwd="$(readlink -f "$PWD")"

pushd "$build_dir"

cmake "$repo_root" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_STATIC=ON

if [[ "${GITHUB_ACTIONS:-}" != "" ]]; then
    jobs="$(nproc)"
else
    jobs="$(nproc --ignore=1)"
fi

make -j"$jobs"

make install DESTDIR=AppDir

find AppDir

cp "$(which desktop-file-validate)" AppDir/usr/bin
cp "$(which mksquashfs)" AppDir/usr/bin
cp "$(which zsyncmake)" AppDir/usr/bin

cp "$repo_root"/resources/AppRun.sh AppDir/AppRun
chmod +x AppDir/AppRun

wget https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-"$ARCH"

# NOTE: Hash verification for continuous releases has limitations:
# - Continuous releases are updated regularly, causing hash mismatches
# - This will break when type2-runtime is updated
# - For production use, consider:
#   1. Using versioned/tagged releases instead of continuous, OR
#   2. Implementing GPG signature verification (download .sig and verify with GPG), OR
#   3. Automatically updating hashes when type2-runtime changes
# For now, we print the hash for transparency but skip strict verification.

# Print runtime information for transparency
echo "Runtime file: runtime-$ARCH"
echo "Runtime SHA256: $(sha256sum runtime-$ARCH | awk '{print $1}')"
echo "Runtime size: $(stat -c%s runtime-$ARCH) bytes"

pushd AppDir
ln -s usr/share/applications/appimagetool.desktop .
ln -s usr/share/icons/hicolor/128x128/apps/appimagetool.png .
ln -s usr/share/icons/hicolor/128x128/apps/appimagetool.png .DirIcon
popd

find AppDir
cat AppDir/appimagetool.desktop

AppDir/AppRun --runtime-file runtime-"$ARCH" AppDir

mv appimagetool-*.AppImage "$old_cwd"
