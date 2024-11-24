#! /bin/sh

set -ex

if [ -z "${ALPINE_ARCH}" ]; then
    echo "Usage: env ALPINE_ARCH=<arch> $0"
    echo "Example values: x86_64 x86 armhf aarch64"
    exit 2
fi

# build in a temporary directory
# this makes sure that subsequent runs do not influence each other
# also makes cleaning up easier: just dump the entire directory
tempdir="$(mktemp -d)"

# need to memorize the repository root directory's path so that we can copy files from it
repo_root_dir=$(dirname "$(readlink -f "$0")")/../../

# cleanup takes care of unmounting and removing all downloaded files
cleanup() {
    for i in dev proc sys; do
        sudo umount "$tempdir"/miniroot/"$i"
    done
    sudo rm -rf "$tempdir"
}
trap cleanup EXIT

cd "$tempdir"

#############################################
# Download and extract minimal Alpine system
#############################################

wget "http://dl-cdn.alpinelinux.org/alpine/v3.19/releases/${ALPINE_ARCH}/alpine-minirootfs-3.19.4-${ALPINE_ARCH}.tar.gz"
mkdir -p ./miniroot
cd ./miniroot
sudo tar xf ../alpine-minirootfs-*-"${ALPINE_ARCH}".tar.gz
cd -

#############################################
# Prepare chroot
#############################################
sudo mkdir miniroot/src
sudo cp -r "$repo_root_dir" miniroot/src
#sudo cp -r "$repo_root_dir"/patches miniroot/patches

sudo mount -o bind /dev miniroot/dev
sudo mount -t proc none miniroot/proc
sudo mount -t sysfs none miniroot/sys
sudo cp -p /etc/resolv.conf miniroot/etc/

#############################################
# Run build.sh in chroot
#############################################

# copy build scripts so that they are available within the chroot environment
# build.sh combines existing scripts shared by all available build environments
sudo cp -R "$repo_root_dir"/scripts miniroot/scripts

if [ "$ALPINE_ARCH" = "x86" ] || [ "$ALPINE_ARCH" = "x86_64" ]; then
    echo "Architecture is x86 or x86_64, hence not using qemu-arm-static"
    echo "export ARCH=${ALPINE_ARCH}" > miniroot/arch.src
    sudo chroot miniroot /bin/sh -ex /scripts/chroot/build.sh
elif [ "$ALPINE_ARCH" = "aarch64" ] ; then
    echo "Architecture is aarch64, hence using qemu-aarch64-static"
    sudo cp "$(which qemu-aarch64-static)" miniroot/usr/bin
    sudo chroot miniroot qemu-aarch64-static /bin/sh -ex /scripts/chroot/build.sh
elif [ "$ALPINE_ARCH" = "armhf" ] ; then
    echo "Architecture is armhf, hence using qemu-arm-static"
    sudo cp "$(which qemu-arm-static)" miniroot/usr/bin
    sudo cp "$repo_root_dir"/scripts/chroot/build.sh miniroot/build.sh && sudo chroot miniroot qemu-arm-static /bin/sh -ex /scripts/chroot/build.sh
else
    echo "Edit chroot_build.sh to support this architecture as well, it should be easy"
    exit 1
fi

#############################################
# Copy build artifacts out
#############################################

# Use the same architecture names as https://github.com/AppImage/AppImageKit/releases/
case "$ALPINE_ARCH" in
    x86)
        appimage_arch=i686
        ;;
    *)
        appimage_arch="$ALPINE_ARCH"
        ;;
esac

cd "$repo_root_dir"
mkdir -p ./out/
sudo find "$tempdir"/miniroot/ -type f -executable -name "appimagetool-${appimage_arch}.AppImage"  -exec cp {} "out/appimagetool-${appimage_arch}.AppImage" \;
# sudo find "$tempdir"/miniroot/ -type f -executable -name "runtime-${appimage_arch}.debug"  -exec cp {} "out/runtime-${appimage_arch}.debug" \;
