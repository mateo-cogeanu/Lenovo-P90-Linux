#!/bin/sh
set -eu

: "${P90_MAINLINE_TREE:?Set P90_MAINLINE_TREE to a Linux 6.x source tree}"
: "${P90_MAINLINE_OUT:?Set P90_MAINLINE_OUT to a new or empty build directory}"

jobs=${P90_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}

test -x "$P90_MAINLINE_TREE/scripts/config" || {
    echo "Missing scripts/config below $P90_MAINLINE_TREE" >&2
    exit 1
}

mkdir -p "$P90_MAINLINE_OUT"
make -C "$P90_MAINLINE_TREE" O="$P90_MAINLINE_OUT" x86_64_defconfig

config="$P90_MAINLINE_TREE/scripts/config"
"$config" --file "$P90_MAINLINE_OUT/.config" \
    --enable X86_EXTENDED_PLATFORM \
    --enable X86_PLATFORM_DEVICES \
    --enable X86_INTEL_MID \
    --enable PCI \
    --enable PCI_MSI \
    --enable DEVTMPFS \
    --enable DEVTMPFS_MOUNT \
    --enable BLK_DEV_INITRD \
    --enable RD_GZIP \
    --enable EXT4_FS \
    --enable TMPFS \
    --enable FW_LOADER \
    --enable MMC \
    --enable MMC_SDHCI \
    --enable MMC_SDHCI_PCI \
    --enable USB_SUPPORT \
    --enable USB \
    --enable USB_XHCI_HCD \
    --enable USB_DWC3 \
    --enable USB_DWC3_PCI \
    --enable SERIAL_8250 \
    --enable SERIAL_8250_CONSOLE \
    --enable SERIAL_8250_MID \
    --enable PINCTRL \
    --enable PINCTRL_MOOREFIELD \
    --enable GPIOLIB \
    --enable GPIO_MERRIFIELD \
    --enable INTEL_SCU_PCI \
    --enable INTEL_MID_WATCHDOG \
    --disable DRM_I915 \
    --disable LOCALVERSION_AUTO \
    --set-str LOCALVERSION "-p90-mainline-test"

make -C "$P90_MAINLINE_TREE" O="$P90_MAINLINE_OUT" olddefconfig
make -C "$P90_MAINLINE_TREE" O="$P90_MAINLINE_OUT" -j"$jobs" bzImage

test -s "$P90_MAINLINE_OUT/arch/x86/boot/bzImage"
sha256sum "$P90_MAINLINE_OUT/arch/x86/boot/bzImage" "$P90_MAINLINE_OUT/.config"
