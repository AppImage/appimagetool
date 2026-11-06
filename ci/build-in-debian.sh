#! /bin/bash

# Builds appimagetool in a Debian (ports) system, for the architectures that
# have no Alpine Linux, e.g. big endian ppc64. Debian has no static builds of
# all the dependencies, so appimagetool is linked dynamically; the libraries
# it needs, but glibc, go into the AppImage (usr/lib), found through AppRun.
# mksquashfs, zsyncmake and desktop-file-validate come from Debian, with
# their libraries too. The runtime is downloaded as in ci/build.sh.
#
# Usage (as root, in the repository, e.g. in a container): env ARCH=ppc64 bash ci/build-in-debian.sh
# The AppImage is written into the current directory.

set -euxo pipefail

if [[ "${ARCH:-}" == "" ]]; then
    echo "Usage: env ARCH=... bash $0"
    exit 1
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    ca-certificates git wget file gcc g++ cmake make pkg-config \
    libglib2.0-dev libgpgme-dev libgcrypt20-dev libcurl4-openssl-dev \
    squashfs-tools zsync desktop-file-utils

repo_root="$(readlink -f "$(dirname "${BASH_SOURCE[0]}")"/..)"
git config --global --add safe.directory "$repo_root"
old_cwd="$(readlink -f "$PWD")"

build_dir="$(mktemp -d -t appimagetool-build-XXXXXX)"
trap 'rm -rf "$build_dir"' EXIT
cd "$build_dir"

cmake "$repo_root" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr
make -j"$(nproc)"
make install DESTDIR=AppDir

cp /usr/bin/mksquashfs /usr/bin/zsyncmake /usr/bin/desktop-file-validate AppDir/usr/bin/

# the libraries of the programs, but those of glibc, which the machine has
mkdir -p AppDir/usr/lib
glibc='^(ld64\.so.*|ld-linux.*|libc\.so.*|libm\.so.*|libdl\.so.*|libpthread\.so.*|librt\.so.*|libresolv\.so.*|libutil\.so.*|libnss_.*)$'
for f in AppDir/usr/bin/*; do
    ldd "$f"
done | awk '$2 == "=>" && $3 ~ /^\// {print $1 " " $3}' | sort -u | while read -r name path; do
    if ! echo "$name" | grep -q -E "$glibc"; then
        cp -L "$path" AppDir/usr/lib/"$name"
    fi
done
ls AppDir/usr/lib

# AppRun: the bundled programs first in PATH (as resources/AppRun.sh), and their libraries
cat > AppDir/AppRun <<'APPRUN'
#! /bin/sh

set -e

this_dir="$(dirname -- "$(readlink -f -- "$0")")"

export PATH="$this_dir"/usr/bin:"$PATH"
export LD_LIBRARY_PATH="$this_dir"/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

exec "$this_dir"/usr/bin/appimagetool "$@"
APPRUN
chmod +x AppDir/AppRun

# nothing but glibc is left outside the AppImage
missing=$(LD_LIBRARY_PATH=AppDir/usr/lib ldd AppDir/usr/bin/* | awk '$2 == "=>" && $3 ~ /^\// {print $1 " " $3}' \
    | awk '{print $1}' | { grep -v -E "$glibc" || true; } | sort -u)
if [ -n "$missing" ]; then
    echo "Libraries not in the AppImage: $missing" >&2
    exit 1
fi

wget https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-"$ARCH"

pushd AppDir
ln -s usr/share/applications/appimagetool.desktop .
ln -s usr/share/icons/hicolor/128x128/apps/appimagetool.png .
ln -s usr/share/icons/hicolor/128x128/apps/appimagetool.png .DirIcon
popd

AppDir/AppRun --runtime-file runtime-"$ARCH" AppDir

mv appimagetool-*.AppImage "$old_cwd"
