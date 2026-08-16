# Relocatable crash kernel and signed-command-line boot candidate

## Why this route exists

The stock kernel accepts a normal `kexec_load`, but the normal restart path
re-arms the Intel SCU watchdog and the second kernel never reaches observable
userspace. The crash-kexec path is materially different: Linux preloads a
relocatable kernel into memory reserved at first boot, then enters it from the
panic path without running the ordinary restart notifier chain.

This is a reversible experiment, not yet an Android replacement. Android stays
on storage and the custom kernel/initramfs remain RAM-only. The sole persistent
candidate change is a logical `boot` image derived from the exact signed stock
image, with a crash-memory reservation appended in Intel's separate engineering
command-line field.

## Reproducible kernel configuration

The build starts from Lenovo's `x86_64_moor_defconfig` and uses:

```text
CONFIG_KEXEC=y
CONFIG_CRASH_DUMP=y
CONFIG_RELOCATABLE=y
CONFIG_PROC_VMCORE=y
CONFIG_PHYSICAL_START=0x1000000
CONFIG_PHYSICAL_ALIGN=0x1000000
```

The entry point is:

```text
outputs/p90-native-linux-port/scripts/build-vendor-relocatable-crash-kernel.sh
```

The historical Android GCC 4.9 `collect2` binary asks for a bare `ld` during
some x86 links. The retained compiler wrapper sets `COMPILER_PATH` to its own
target-tool directory, which contains `ld` as a symlink to
`x86_64-linux-android-ld.bfd`. This scopes the old linker lookup to target GCC;
host build utilities continue using the VM's native linker. The build script
also supports an explicit `P90_TARGET_LINKER_DIR` containing `ld`.

## Case-sensitive source preservation

Lenovo's Linux archive contains files that differ only by case. A normal macOS
filesystem collapses these pairs during extraction:

| Match ABI/source | Target ABI/source |
|---|---|
| `ipt_ttl.h` | `ipt_TTL.h` |
| `ip6t_hl.h` | `ip6t_HL.h` |
| `xt_hl.c` | `xt_HL.c` |
| `xt_connmark.h` | `xt_CONNMARK.h` |

The retained source uses collision-safe internal names:

```text
include/linux/netfilter_ipv4/ipt_ttl_match.h
include/linux/netfilter_ipv4/ipt_ttl_target.h
include/linux/netfilter_ipv6/ip6t_hl_match.h
include/linux/netfilter_ipv6/ip6t_hl_target.h
net/netfilter/xt_HL.c             (match implementation retained from extraction)
net/netfilter/xt_hl_target.c      (target implementation)
```

`net/netfilter/Makefile` maps the match and target configuration symbols to
these unique object names. `xt_connmark.c` includes the retained uppercase
header containing the required ABI. The definitions were checked against the
official Linux stable 3.10 source. Both HL objects compiled independently as
x86-64 relocatable ELF objects before the complete build.

## Verified output

The complete build returned exit code 0 and its retained log has zero matches
for fatal compiler errors, undefined references, or failed make rules.

| Artifact | SHA-256 |
|---|---|
| `work/p90-native-port/artifacts/vendor-relocatable-crash/bzImage` | `a90f06d4163300f4d067e136775628108903a709d22cb5adcb7485ea2e33f4be` |
| `work/p90-native-port/artifacts/vendor-relocatable-crash/config` | `09698ba48c5132c445d4ac654fedfd48e314411a17a3d33318891ba10cdaad45` |
| `work/p90-native-port/artifacts/vendor-relocatable-crash/build-final.log` | `080ab306b0f14d342d990ce0f19e03c5536751ac96f58343a276e930d75ba636` |

`file` identifies the result as Linux 3.10.20, x86 bzImage protocol 2.12,
relocatable, with a legacy 64-bit entry point and support for loading above 4G.
Its size is 7,057,408 bytes.

## Signed stock boot candidate

The immutable input is the exact S149 signed boot image:

```text
0bc13d50f88c1f32297e932c3aff63c013c52dcc214255a5c5192b0d25c6646b
work/p90-stock-firmware/extracted/boot.img
```

The candidate was generated with:

```sh
python3 outputs/p90-native-linux-port/scripts/patch-signed-osip-cmdline.py \
  --input work/p90-stock-firmware/extracted/boot.img \
  --output work/p90-native-port/artifacts/stock-s149/boot-crashkernel-128M-at-1G.img \
  --append 'crashkernel=128M@0x40000000'
```

Candidate SHA-256:

```text
ec43ec11fd40aa5072af9d7ac52d0118d0a14f97d384a6c311f9569ee29ff16e
```

Byte-level verification found exactly 29 changed bytes, from `0x5f0` through
`0x60c`. Every change is inside the command-line field `0x4d8..0x8d7`.
The entire prefix before `0x4d8`, including the `$MOSS` header at `0x280`, is
identical. The entire suffix from `0x8d8` through EOF is identical. Image size,
OSIP metadata, signed attribute `0x00`, authenticated header and kernel/ramdisk
payload are unchanged.

## Guarded live-test procedure

No step below authorizes a raw p1 write, a wipe, or an unsigned image.

1. Confirm Android is fully booted in `main` mode, battery is comfortably above
   50%, USB is stable, and the exact stock boot image and hashes are local.
2. Rehash both stock and candidate images. Reject the test if the stock hash or
   byte-range comparison differs from the values above.
3. Use only the retained legacy Intel-aware fastboot client and the logical
   `boot` target. Never address p1 or any raw block device.
4. Boot Android and verify `/proc/cmdline` contains
   `crashkernel=128M@0x40000000`. Verify `/proc/iomem` contains a 128 MiB
   crash-kernel reservation at the requested address. If either check fails,
   restore exact stock boot and stop.
5. Acquire temporary root only through the already documented reversible
   `dumpstate` method. Before any trigger, restore `/system/bin/dumpstate`, pull
   it back, and require SHA-256
   `1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c`.
6. Perform only a `kexec --panic` load/unload probe first. A load failure is a
   stop condition; do not panic and do not improvise another write.
7. If load/unload succeeds, reload the verified vendor crash bzImage with the
   retained root-ADB vendor ramdisk and a verbose rescue command line. Restore
   and verify factory `dumpstate` again before the trigger.
8. Trigger one controlled kernel panic. Observe USB/ADB and serial/log paths.
   If Android returns by watchdog or the phone becomes unavailable, use the
   documented recovery flow and restore the exact signed stock logical `boot`.
9. After every outcome, verify Android build, boot mode, battery, filesystems,
   factory executable hashes, and archive logs plus all exact commands.

Only after the rescue kernel reaches observable root ADB should Debian be
introduced, initially from RAM or a non-destructive external/rootfs path. Phosh
and a newer kernel remain later hardware-enablement milestones.
