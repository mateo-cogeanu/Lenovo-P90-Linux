# Vendor kernel build and Intel boot image

## Source and compiler

Lenovo's archive contains a complete `linux/` layout. Keep that layout intact:
the kernel tree has relative links into sibling `modules/` directories.

- Kernel: Linux 3.10.20, `arch/x86/configs/x86_64_moor_defconfig`
- Compiler currently used: Google's prebuilt `x86_64-linux-android-4.9`, tag
  `android-9.0.0_r8`, commit `ad4f7b7fe78ca4baf15ffd968c30ef61a9e7e7df`
- Build host: x86_64 Debian VM under Lima, because the historical compiler is an
  x86_64 Linux executable and the host is macOS/Apple Silicon.

Linux 3.10's `arch/x86/tools/relocs` relies on tentative common symbols. Modern
host GCC defaults to `-fno-common`, causing a duplicate `per_cpu_load_addr` link
error. The build therefore adds `-fcommon` to `HOSTCFLAGS`; this changes a host
build utility only, not target kernel code generation.

Intel's `sound/soc/intel/Makefile` forces `-Werror`. GCC 4.9 cannot prove that
four `pipe_id` locals are initialized through the checked `sst_get_pipe_id()`
helper, so patch `0001-intel-sst-initialize-pipe-id.patch` initializes those
bytes to zero. This has no effect on valid paths and makes the compatibility
change explicit rather than globally disabling warnings-as-errors.

Lenovo's archive also references but omits
`include/uapi/linux/netfilter/xt_connmark.h`. Patch
`0002-restore-missing-xt-connmark-uapi.patch` restores it verbatim from the
official Linux stable `v3.10.20` tag. This is missing-source repair, not a local
API invention.

The same packaging omission affects `ip6t_HL.h`; patch
`0003-restore-missing-ip6t-HL-uapi.patch` restores the unmodified header from
the same stable tag.

Finally, `net/netfilter/Makefile` enables `xt_hl.o` but Lenovo omitted
`net/netfilter/xt_hl.c`. Patch `0004-restore-missing-xt-hl-source.patch`
restores that implementation verbatim from upstream `v3.10.20`.

That matcher uses the distinct lowercase `ipt_ttl.h`, also listed but omitted by
Lenovo. Patch `0005-restore-missing-ipt-ttl-uapi.patch` restores the upstream
header; it must not be confused with the existing uppercase `ipt_TTL.h` target
header.

The installed kernel identifies its compiler as GCC 4.7 and its complete release
as `3.10.20-x86_64_moor`. Lenovo's build script also names a private/omitted
Android GCC 4.7 prebuilt. Google's public repository with that exact path has
historical Android 4.4 refs but resolves to an empty placeholder commit, so it
does not supply the binary. The GCC 4.9 rebuild must therefore not be described
as bit-reproducible stock. The installed Intel boot image is now preserved
byte-for-byte as `mmcblk0p1.img`; strings in that image identify the bootstub,
bzImage path and vendor command line.

Lenovo's supplied ramdisk modules report vermagic
`3.10.20-x86_64_moor-dirty`. They may not load into a differently suffixed
rebuild. The initial rescue path depends only on built-in eMMC, DWC3 USB gadget,
initramfs and Android ADB support; optional display/audio/radio modules are not an
acceptance condition for the backup boot.

## Intel/Lenovo image format

This is not a standard Android boot image. Lenovo's `stitch.sh` lays it out as:

| Offset | Content |
|---:|---|
| `0x0000` | 4 KiB kernel command-line block |
| `0x0400` | little-endian bzImage size |
| `0x0404` | little-endian initrd size |
| `0x0408` | SPI log-suppression flag |
| `0x040c` | SPI controller selector |
| `0x1000` | 4 KiB Intel bootstub block |
| `0x2000` | bzImage immediately followed by gzip/cpio initrd |

The supplied command line starts `init=/init`, selects Moorefield hardware, and
sets a USB gadget IP pair. Preserve the device-specific Android boot parameters
for initial tests.

The source `bootstub` file is 8 KiB, but Lenovo's `dd bs=4096 count=1 seek=1`
copies only its first 4 KiB. The Python stitcher intentionally reproduces that
truncation and rejects an overlong command line.

## Safe first boot

The first image should reuse Lenovo's bootstub, rebuilt vendor kernel and a
modified copy of Lenovo's ramdisk. The ramdisk should retain Intel USB setup but
start a root ADB service and contain static rescue tools. Send it with the legacy
fastboot client's temporary boot operation:

```sh
fastboot -i 0x8087 boot p90-rescue.img
```

This command is intentionally not `flash`. Testing showed that Droidboot accepts
the transfer but immediately returns to its menu without executing either the
stitched Lenovo image or a standard kernel+ramdisk request. Do not substitute a
flash command. The preservation set is now complete, but restoration cannot be
considered proven until a non-writing boot route exists.

## Completed vendor build artifacts

- `work/p90-native-port/artifacts/vendor-stock/bzImage`
  - SHA-256: `4932a18b3bc85e645d37904151e9637f03ef747d0c0319adc78b392ff51ba075`
- `work/p90-native-port/artifacts/vendor-stock/p90-vendor-root-adb.img`
  - SHA-256: `8981d4467faf15f3b1cf2e96c731ffa55ab1868d979912fdc690aa62a5528f06`

The rescue image is an unflashed engineering artifact. It is not a replacement
for p1 and must not be installed merely because its USB transfer succeeds.

## Reproducibility

Run `scripts/build-vendor-kernel.sh` inside an x86_64 Linux environment with:

```sh
P90_LINUX_TREE=/path/to/linux \
P90_TOOLCHAIN=/path/to/x86_64-linux-android-4.9 \
P90_BUILD_OUT=/path/to/out \
./scripts/build-vendor-kernel.sh
```

The script copies the exact Lenovo defconfig, uses out-of-tree build products and
prints SHA-256 checksums for the final kernel and config.

The scripted target is `bzImage`, not the default `all`. This deliberately skips
loadable camera/audio/radio modules during rescue-kernel construction; it does
not disable any driver configured as built-in. A complete `modules` build is a
later hardware-enablement artifact.
