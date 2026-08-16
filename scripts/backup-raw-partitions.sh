#!/bin/sh
set -eu

adb_bin=${ADB:-adb}
backup_root=${P90_RAW_BACKUP:-outputs/p90-backup/raw-partitions}

device_id=$($adb_bin get-serialno)
test "$device_id" = "MedfieldE0F225F9" || {
    echo "Refusing: unexpected device serial '$device_id'" >&2
    exit 1
}

remote_uid=$($adb_bin shell id 2>/dev/null | tr -d '\r')
case "$remote_uid" in
    uid=0*) ;;
    *)
        echo "Refusing: ADB is not root: $remote_uid" >&2
        echo "Boot the non-flashed rescue image first." >&2
        exit 1
        ;;
esac

mkdir -p "$backup_root/metadata"

$adb_bin shell 'cat /proc/partitions' | tr -d '\r' > "$backup_root/metadata/proc-partitions.txt"
$adb_bin shell 'cat /proc/mounts' | tr -d '\r' > "$backup_root/metadata/proc-mounts.txt"
$adb_bin shell 'for p in /sys/class/block/mmcblk0 /sys/class/block/mmcblk0p* /sys/class/block/mmcblk0boot*; do echo "[$p]"; for f in dev partition start size ro; do if test -r "$p/$f"; then echo "$f=$(cat "$p/$f")"; fi; done; if test -r "$p/uevent"; then cat "$p/uevent"; fi; done' \
    | tr -d '\r' > "$backup_root/metadata/sysfs-block-map.txt"
$adb_bin shell 'ls -la /dev/block /dev/block/by-name 2>&1' | tr -d '\r' \
    > "$backup_root/metadata/dev-block-list.txt"

for partition in 1 2 3 4 5 6 7 8; do
    output="$backup_root/mmcblk0p${partition}.img"
    test ! -e "$output" || {
        echo "Refusing to overwrite existing $output" >&2
        exit 1
    }
    echo "Reading mmcblk0p${partition}..."
    $adb_bin exec-out "cat /dev/block/mmcblk0p${partition}" > "$output"
done

for boot_area in boot0 boot1; do
    output="$backup_root/mmcblk0${boot_area}.img"
    test ! -e "$output" || {
        echo "Refusing to overwrite existing $output" >&2
        exit 1
    }
    echo "Reading mmcblk0${boot_area}..."
    $adb_bin exec-out "cat /dev/block/mmcblk0${boot_area}" > "$output"
done

(cd "$backup_root" && shasum -a 256 mmcblk0*.img > SHA256SUMS)
echo "Raw preservation set written to $backup_root"
echo "Review sizes, partition labels and SHA256SUMS before any write operation."
