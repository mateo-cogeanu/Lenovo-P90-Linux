#!/bin/sh
set -eu

: "${P90_LINUX_TREE:?Set P90_LINUX_TREE to the complete extracted Lenovo linux directory}"
: "${P90_BZIMAGE:?Set P90_BZIMAGE to the rebuilt vendor bzImage}"

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
output=${P90_RESCUE_IMAGE:-p90-vendor-root-adb.img}

python3 "$script_dir/stitch_intel_image.py" \
    --cmdline "$P90_LINUX_TREE/boot_cmdline" \
    --bootstub "$P90_LINUX_TREE/bootstub" \
    --kernel "$P90_BZIMAGE" \
    --initrd "$P90_LINUX_TREE/ramdisk.img" \
    --spi-suppress 0 \
    --spi-type 2 \
    --output "$output"

shasum -a 256 "$output"
echo "This image contains Lenovo's supplied ro.secure=0 ramdisk."
echo "Test only with: fastboot -i 0x8087 boot $output"
echo "Do not flash it."
