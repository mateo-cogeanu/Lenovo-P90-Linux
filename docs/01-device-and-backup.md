# Device inventory and backup ledger

## Identified device

- Product: Lenovo P90
- Serial: `MedfieldE0F225F9`
- Android build: `P90_S149_160504_ROW`
- Android version: 4.4.4
- SoC: Intel Atom Z3560 (Moorefield)
- Running kernel: `3.10.20-x86_64_moor`, built 2016-05-04 with GCC 4.7
- Kernel architecture: x86_64; Android userspace ABI: x86
- Intel Droidboot USB vendor ID: `8087`

Google platform-tools r26.0.2 recognizes Intel Droidboot when fastboot is
invoked with `-i 0x8087`. Standard `oem unlock`, `oem unlock-go`, and
`oem device-info` are unsupported; `flashing unlock` hangs. The bootloader is
therefore not confirmed unlocked.

`fastboot boot` was tested without flashing. Droidboot accepted the USB transfer
but immediately returned to its menu for both Lenovo's stitched image and a
standard kernel-plus-ramdisk request. Temporary boot is not functional on this
firmware as tested.

## GPT and partition map

The primary GPT is preserved in `mmcblk0-first4M.bin`; the backup GPT is in
`mmcblk0-last4M.bin`. `sgdisk` reports this exact map:

| Device | GPT name | Exact bytes | Backup status |
|---|---|---:|---|
| mmcblk0p1 | reserved | 268,435,456 | complete; contains Intel bootstub and installed Linux image |
| mmcblk0p2 | panic | 33,554,432 | complete |
| mmcblk0p3 | factory | 134,217,728 | complete, ext4 |
| mmcblk0p4 | misc | 134,217,728 | complete |
| mmcblk0p5 | config | 134,217,728 | complete, ext4 |
| mmcblk0p6 | cache | 1,610,612,736 | complete, ext4 |
| mmcblk0p7 | logs | 25,165,824 | complete, ext4 |
| mmcblk0p8 | system | 2,684,354,560 | complete, ext4 |
| mmcblk0p9 | data | 57,512,258,560 | filesystem-aware sparse capture; see below |
| mmcblk0boot0 | eMMC boot area 0 | 4,194,304 | complete |
| mmcblk0boot1 | eMMC boot area 1 | 4,194,304 | complete |
| mmcblk0rpmb | protected RPMB | 4,194,304 | intentionally not readable/backed up |

## Completed preservation set

Backup root: `outputs/p90-backup/`

- `shared-storage/`: 1,153 files, 371,797,045 bytes.
- `system/`: 2,129 files, 2,304,998,406 bytes.
- `raw-partitions/p90-root-backup/`: p1-p8, boot0, boot1 and both GPT edges.
- `mmcblk0p9-online.ext4.simg.gz`: allocation-aware Android sparse image of
  userdata, gzip compressed to 427,527,764 bytes.
- `metadata/`: properties, mounts, packages, kernel information and probe data.
- `system-originals/`: factory `run-as` and `dumpstate` binaries used to prove
  restoration after temporary-root work.
- `android-full-backup.EMPTY.ab`: 41-byte evidence that Android Backup Manager
  produced no usable backup; do not treat it as restorable.

All twelve uncompressed binary images matched phone-side and host-side MD5 after
ADB transfer. Every retained artifact has a host-side SHA-256 in
`manifests/BACKUP-SHA256SUMS.txt`.

## How temporary root was obtained

The installed May 2016 kernel predates the upstream Dirty COW fix. The
`timwr/CVE-2016-5195` proof of concept was built for Android x86 with Google NDK
r16b. Overwriting `/system/bin/run-as` did not gain root because Lenovo ships it
as `0750 root:shell`, without setuid. That file was restored byte-for-byte.

The factory init configuration exposes a disabled `dumpstate` one-shot service
which the shell may start using `setprop ctl.start dumpstate`; init runs it as
UID 0. A small, auditable payload was temporarily placed over the first pages of
`/system/bin/dumpstate` through Dirty COW. The payload performed only fixed
backup operations, then the complete 61,720-byte factory executable was restored.
The SHA-256 before and after every use was:

`1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c`

The original `run-as` SHA-256 before and after restoration was:

`9e003e80d58c8143105f84ebb2e797bea715fce07cb5b116780a4d7f94f9f67a`

No partition was written. After backup, all payloads and proof files were removed
from `/data/local/tmp`, `/cache` returned to 2.3 MiB used, and `/data` returned to
about 640 MiB used.

## Userdata snapshot format and limitation

The 53.6 GiB p9 filesystem was mounted and could not be cleanly unmounted while
Android supplied ADB. A custom read-only agent captured ext4 allocation bitmaps,
emitted only allocated blocks as a standard Android sparse image, piped it
through gzip into the separate cache partition, and copied the finished archive
to staging only after capture. The status file records 14,041,078 total blocks,
417,271 captured blocks and 3,722 sparse chunks.

Validation succeeded through gzip, `simg2img`, and all five read-only
`e2fsck -fn` passes. It found 12,070 files and 416,039 filesystem-used blocks.
Because this is an online snapshot, e2fsck reports journal/orphan entries, two
directory entries changed during capture, a handful of bitmap deltas and bogus
timestamps caused by the phone's 2014 clock. Treat it as a crash-consistent
recovery image that requires journal replay/fsck on a copy, not as a clean
unmounted image to flash blindly.

## Preservation gate status

Raw backup, GPT capture, installed kernel preservation, transfer verification
and source retention are complete. A tested restoration *path* is still missing:
Droidboot cannot temporarily boot the rescue image and no standard unlock command
works. Consequently nothing should be flashed until an Intel manufacturing or
signed-image recovery route is identified and tested non-destructively.
