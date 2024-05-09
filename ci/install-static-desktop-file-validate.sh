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

apk add glib-static glib-dev
wget -c https://gitlab.freedesktop.org/xdg/desktop-file-utils/-/archive/56d220dd679c7c3a8f995a41a27a7d6f3df49dea/desktop-file-utils-56d220dd679c7c3a8f995a41a27a7d6f3df49dea.tar.gz
tar xf desktop-file-utils-*.tar.gz
cd desktop-file-utils-*/
# The next 2 lines are a workaround for: checking build system type... ./config.guess: unable to guess system type
wget 'http://git.savannah.gnu.org/gitweb/?p=config.git;a=blob_plain;f=config.guess;hb=HEAD' -O config.guess
wget 'http://git.savannah.gnu.org/gitweb/?p=config.git;a=blob_plain;f=config.sub;hb=HEAD' -O config.sub
autoreconf --install # https://github.com/shendurelab/LACHESIS/issues/31#issuecomment-283963819
./configure CFLAGS=-no-pie LDFLAGS=-static

if [[ "${GITHUB_ACTIONS:-}" != "" ]]; then
    jobs="$(nproc)"
else
    jobs="$(nproc --ignore=1)"
fi

make -j"$jobs"

cd src/
gcc -static -o desktop-file-validate keyfileutils.o validate.o validator.o mimeutils.o -lglib-2.0 -lintl
gcc -static -o update-desktop-database  update-desktop-database.o mimeutils.o -lglib-2.0 -lintl
gcc -static -o desktop-file-install keyfileutils.o validate.o install.o mimeutils.o -lglib-2.0 -lintl
strip desktop-file-install desktop-file-validate update-desktop-database

cd ..

make install