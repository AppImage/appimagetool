#!/bin/sh

set -ex

cleanup () {
    echo "clean"
}
trap cleanup EXIT

mkdir -p tmp
cd tmp
mkdir -p qemu-user-static
cd qemu-user-static
wget -c https://archlinux.org/packages/extra/x86_64/qemu-user-static/download/ -O qemu-user-static.pkg.tar.zst
unzstd qemu-user-static.pkg.tar.zst
tar -xf qemu-user-static.pkg.tar
sudo mv ./usr/bin/* /usr/bin
ls -l /usr/bin/qemu-*
cd ..

mkdir -p qemu-user-static-binfmt
cd qemu-user-static-binfmt
wget -c https://archlinux.org/packages/extra/x86_64/qemu-user-static-binfmt/download/ -O qemu-user-static-binfmt.pkg.tar.zst
unzstd qemu-user-static-binfmt.pkg.tar.zst
tar -xf qemu-user-static-binfmt.pkg.tar
sudo mv ./usr/lib/binfmt.d/* /usr/lib/binfmt.d
ls -l /usr/lib/binfmt.d/qemu-*
cd ..

sudo systemctl restart systemd-binfmt.service

# from https://packages.ubuntu.com/noble/amd64/qemu-user-static/download
# wget -c http://kr.archive.ubuntu.com/ubuntu/pool/universe/q/qemu/qemu-user-static_8.2.2+ds-0ubuntu1_amd64.deb
# sudo dpkg -i qemu-user-static_8.2.2+ds-0ubuntu1_amd64.deb
