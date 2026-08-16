#!/bin/sh
set -eu

# Run on Linux. Produces a reproducible gzip-compressed newc initramfs.
repo=${1:-$(pwd)}
source_dir="$repo/outputs/p90-native-linux-port/initramfs/p90-debian"
rootfs_archive=${P90_ROOTFS_ARCHIVE:-"$repo/work/p90-native-port/artifacts/debian-bookworm-phosh-amd64.tar.gz"}
busybox_binary=${P90_BUSYBOX:-"$repo/work/p90-native-port/artifacts/p90-debian-initramfs/busybox-static-amd64"}
module_dir=${P90_MODULE_DIR:-"$repo/work/p90-native-port/rescue-ramdisk/root/lib/modules"}
output_dir=${P90_OUTPUT_DIR:-"$repo/work/p90-native-port/artifacts/p90-debian-initramfs"}
stage="$output_dir/root"
output="$output_dir/p90-debian-initramfs.cpio.gz"

for command in cpio gzip sha256sum; do
    command -v "$command" >/dev/null || {
        echo "missing required command: $command" >&2
        exit 1
    }
done

rm -rf "$stage"
mkdir -p "$stage/bin" "$stage/etc" "$stage/lib/modules" "$output_dir"

# This is the busybox-static binary retained from ./usr/bin/busybox in the
# documented Debian rootfs archive. Set P90_BUSYBOX to override it.
if [ ! -x "$busybox_binary" ]; then
    echo "missing static BusyBox: $busybox_binary" >&2
    echo "extract ./usr/bin/busybox from: $rootfs_archive" >&2
    exit 1
fi
cp "$busybox_binary" "$stage/bin/busybox"
chmod 0755 "$stage/bin/busybox"
cp "$source_dir/init" "$stage/init"
cp "$source_dir/p90-xorg.conf" "$stage/etc/p90-xorg.conf"
cp "$source_dir/p90-session.sh" "$stage/etc/p90-session.sh"
cp "$module_dir/tngdisp.ko" "$module_dir/dfrgx.ko" "$stage/lib/modules/"
chmod 0755 "$stage/init" "$stage/etc/p90-session.sh"
chmod 0644 "$stage/etc/p90-xorg.conf" "$stage/lib/modules/"*.ko

# Normalize metadata and traversal order so identical inputs yield identical bits.
find "$stage" -exec touch -h -d '@0' {} +
(
    cd "$stage"
    find . -print0 | LC_ALL=C sort -z | cpio --null --reproducible \
        --owner=0:0 -o -H newc 2>/dev/null | gzip -n -9 >"$output"
)

gzip -t "$output"
sha256sum "$output"
echo "built $output"
