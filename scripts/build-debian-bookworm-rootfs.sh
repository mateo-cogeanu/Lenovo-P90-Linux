#!/bin/sh
set -eu

: "${P90_ROOTFS_OUT:?Set P90_ROOTFS_OUT to an empty output directory}"

suite=${P90_DEBIAN_SUITE:-bookworm}
mirror=${P90_DEBIAN_MIRROR:-https://deb.debian.org/debian}
arch=amd64

command -v debootstrap >/dev/null 2>&1 || {
    echo "Install debootstrap first." >&2
    exit 1
}

test ! -e "$P90_ROOTFS_OUT" || {
    echo "Refusing to overwrite $P90_ROOTFS_OUT" >&2
    exit 1
}

sudo debootstrap --arch="$arch" --variant=minbase "$suite" "$P90_ROOTFS_OUT" "$mirror"

sudo chroot "$P90_ROOTFS_OUT" /bin/sh -eu <<'CHROOT'
export DEBIAN_FRONTEND=noninteractive
printf '%s\n' \
    'deb https://deb.debian.org/debian bookworm main non-free-firmware' \
    'deb https://deb.debian.org/debian-security bookworm-security main non-free-firmware' \
    'deb https://deb.debian.org/debian bookworm-updates main non-free-firmware' \
    > /etc/apt/sources.list

apt-get update
apt-get install -y --no-install-recommends \
    ca-certificates dbus dbus-user-session init-system-helpers kmod udev \
    iproute2 iputils-ping isc-dhcp-client network-manager openssh-server \
    sudo locales console-setup keyboard-configuration less nano vim-tiny \
    busybox-static evtest libinput-tools pciutils usbutils rfkill \
    mesa-utils mesa-utils-extra phosh phoc

apt-get clean
rm -rf /var/lib/apt/lists/*

printf '%s\n' lenovo-p90 > /etc/hostname
printf '%s\n' '127.0.0.1 localhost' '127.0.1.1 lenovo-p90' > /etc/hosts
useradd --create-home --shell /bin/bash p90
passwd --lock root
passwd --lock p90
CHROOT

sudo tar --numeric-owner --xattrs --acls -C "$P90_ROOTFS_OUT" \
    -cJf "${P90_ROOTFS_OUT%/}.tar.xz" .
sha256sum "${P90_ROOTFS_OUT%/}.tar.xz"

echo "The p90 and root accounts are locked by design."
echo "Add an SSH public key or set a local password from the rescue environment."
