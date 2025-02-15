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

# use Debian's patches, too
wget \
    https://sources.debian.org/data/main/z/zsync/0.6.2-6/debian/patches/buildsystem.diff \
    https://sources.debian.org/data/main/z/zsync/0.6.2-6/debian/patches/manpages.diff \
    https://sources.debian.org/data/main/z/zsync/0.6.2-6/debian/patches/clienthttp.diff \
    https://sources.debian.org/data/main/z/zsync/0.6.2-6/debian/patches/fix-build-with-gcc-14.patch
sha256sum -c <<\EOF
f14a7ffcb3bdadba252c9afbe5e8da358fc77f20072ae5b21475bdfc44b50975  buildsystem.diff
085e9b5856bb8ac0d8567e977a9d5fafc6c19df02b7cfb8e011fedaf2862eaad  clienthttp.diff
f04bba2866c47b8597723d49eec3f1fd50e40a08171e261bf0fc262852ba17e5  manpages.diff
35cbce69194743df1f3f4b9bc9ad36a4c440b117c6a17c268023c642c0d50650  fix-build-with-gcc-14.patch
EOF

for i in *.diff *.patch; do
    echo "Applying patch $i..."
    patch -p1 < "$i"
done

find . -type f -exec sed -i -e 's|off_t|size_t|g' {} \;

./configure CFLAGS=-no-pie LDFLAGS=-static --prefix=/usr --build=$(arch)-unknown-linux-gnu

if [[ "${GITHUB_ACTIONS:-}" != "" ]]; then
    jobs="$(nproc)"
else
    jobs="$(nproc --ignore=1)"
fi

make -j"$jobs" install
