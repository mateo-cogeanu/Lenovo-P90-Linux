# Power-on Debian initramfs

## Result

A board-specific, Android-free early userspace is now built for the P90. Its
`/init` is a static x86_64 BusyBox shell script. It creates the device nodes
Lenovo's kernel cannot create by itself, loads the matching factory display
modules, mounts userdata, enters `/data/local/p90-debian`, and starts the proven
Xorg -> Phoc -> Phosh display stack.

The complete candidate is
`work/p90-native-port/artifacts/p90-debian-initramfs/p90-stock-kernel-debian-initramfs.img`,
SHA-256
`cc64ada9dc383910490d6c79868514a3cab535dc871a3d869cc37a75f1bfdebf`.

This is a real replacement-userspace payload: if a trusted kernel entry loads
it, PID 1 never starts Android init, Zygote, or SurfaceFlinger. It is not yet a
proven power-on replacement because every currently known boot route either
rejects unsigned images or resets during the second-kernel handoff. A controlled
boot-slot test is documented below; it was rejected and exact stock restored.

## Why this initramfs is P90-specific

The exact factory kernel config has `CONFIG_BLK_DEV_INITRD=y`, `CONFIG_RD_GZIP=y`,
`CONFIG_EXT4_FS=y`, and `CONFIG_TMPFS=y`, but `CONFIG_DEVTMPFS` is disabled.
Consequently, mounting `devtmpfs` or using a generic Debian initramfs would not
produce the nodes needed to find userdata or start graphics.

The source therefore creates only the confirmed nodes needed for first boot:

- userdata: `/dev/block/mmcblk0p9`, block major/minor `179:9`;
- framebuffer: `/dev/graphics/fb0`, character `29:0`;
- DRM card/control: `226:0` and `226:64`;
- Synaptics touchscreen: `/dev/input/event2`, character `13:66`;
- the console, tty, pseudo-terminal, random and null devices required by Debian.

The live phone confirmed that p9 is the ext4 filesystem mounted as `/data`, the
framebuffer is `psbfb`, and event2 is `synaptics_dsx` at 1080x1920. These values
are explicit so a failure enters a rescue shell instead of guessing another
partition.

## Display sequence

The initramfs contains the exact S149 `tngdisp.ko` and `dfrgx.ko` modules. Their
SHA-256 hashes are respectively:

- `d79a394a9d73679ebe9408091609f11f987f96d6c53fbc966c04fd63177791cf`
- `ede2e8da64da96c100f54464e4f8259370d58843d93675cdee5f78d7edf82dce`

`tngdisp` is loaded first, matching `init.mofd_v1.rc`; `dfrgx` follows. The
module pair is tied to the exact S149 kernel, whose extracted bzImage SHA-256 is
`05e453aa92bf38b0ae499fc25744c697e82998bf5889e2938f9bfc59cb75429f`.
The first native payload intentionally does not combine factory modules with the
GCC 4.9 rebuild or the Linux 6.18 diagnostic kernel.

After mounting Debian, PID 1 provides the libinput udev record already proven on
the phone. Xorg drives `/dev/graphics/fb0` with the retained patched
`libfbdevhw.so`. Phoc uses its X11 backend and pixman renderer, and Phosh runs as
the Wayland shell. Because Android never starts, Xorg owns the framebuffer from
the beginning; no SurfaceFlinger stop or executable replacement is involved.

## Source and reproducible build

The complete editable source is under
`outputs/p90-native-linux-port/initramfs/p90-debian/`:

- `init` is PID 1, device setup, module loading, userdata mounting and chroot;
- `p90-xorg.conf` is the proven fbdev and touchscreen configuration;
- `p90-session.sh` supervises Xorg and starts Phoc/Phosh.

The build entry point is:

```sh
./outputs/p90-native-linux-port/scripts/build-p90-debian-initramfs.sh "$PWD"
```

Run it in an x86_64 Linux environment with GNU cpio. It uses the pinned Debian
BusyBox artifact at
`work/p90-native-port/artifacts/p90-debian-initramfs/busybox-static-amd64`.
That binary identifies itself as Debian `1:1.35.0-4+deb12u1+b1`, is statically
linked for x86-64, and has SHA-256
`3d9f2889d6782537624a4e1a10e68a2ddd53e0ee8bac02676f27308f42ec6bf6`.
It came from `./usr/bin/busybox` in the retained Debian rootfs archive.

The build normalizes every archive timestamp to the Unix epoch, sorts the cpio
input, fixes ownership to root, uses reproducible `newc`, and runs `gzip -n`.
Two consecutive builds were byte-identical at SHA-256
`769f6f850caa7d4ac72b25988ff240b98cfa34c721f0259db28800ccc3b845f9`.

The archive is 1,635,457 bytes and contains only static BusyBox, the three source
files, and the two factory modules. `gzip -t` passed, its complete cpio listing
was inspected, and BusyBox was checked to provide `chroot`, `cp`, `insmod`,
`mknod`, `mount`, and `sh`.

The Intel image was produced with `stitch_intel_image.py`, the retained Lenovo
bootstub and command-line block, the exact S149 bzImage, and this initramfs. Its
header records kernel size 6,180,864 and initrd size 1,635,457. Extracting those
regions from the final image reproduces both input hashes exactly, with no
trailing bytes.

## Failure behavior and next gate

Failure to load `tngdisp`, mount p9, or find Xorg/Phoc opens a local BusyBox
rescue shell. If Xorg or Phosh exits, logs remain in Debian at
`/var/log/Xorg.p90-native.log` and `/var/log/p90-native-boot.log`, followed by a
rescue shell. This is useful once a trusted entry path exists; it does not by
itself create USB recovery access.

Do not repeat the unchanged unsigned-image flash to boot, recovery, Droidboot,
or raw p1. The
next gate remains a genuinely trusted and non-destructive kernel-entry method.
Once that exists, test this exact hash in RAM first. Only after an observable
RAM boot should Wi-Fi, charging, audio, suspend, and a newer kernel be brought up
one subsystem at a time.

## Controlled boot-slot result

With explicit authorization to write `boot` and restore stock, the inner image
was first rejected by the flash command because it lacked the outer OSIP
metadata. Wrapping the exact inner hash with `make_intel_osip.py` produced:

`work/p90-native-port/artifacts/p90-debian-initramfs/p90-stock-kernel-debian-initramfs-osip.img`

SHA-256:
`56de0342c9c74bb4abe4b1546877af449702a70100185378bf124b35aec8958b`

The wrapper passed its magic, header XOR, load/entry address, payload length and
byte-exact inner-image checks. Droidboot then reported both transfer and write
`OKAY`. Reboot entered Lenovo Recovery in `fota` mode; Debian PID 1 never became
observable. This confirms that correct OSIP packaging is insufficient to pass
the production execution trust check.

Recovery ignored both ADB reboot-to-bootloader variants. After the physical
Power + Volume Down route returned to Droidboot, the exact signed S149 boot
image was written back with transfer and write `OKAY`. Android subsequently
reported `main`, build `P90_S149_160504_ROW`, completed boot, running
SurfaceFlinger, and both kexec slots unloaded. No wipe occurred. The complete
transcript is retained at
`work/p90-native-port/live-test/2026-08-10-native-debian-power-on/boot-slot-test.txt`.
