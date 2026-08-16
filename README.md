# Lenovo P90 native Linux port

This directory documents a preservation-first native Linux port for
the Lenovo P90 (`P90_S149_160504_ROW`, Intel Atom Z3560 / Moorefield).

## Current result

- The phone still boots its original Android 4.4.4 installation.
- A complete Debian 12.15 amd64 rootfs is installed at
  `/data/local/p90-debian` and proven through a real root `chroot`, not PRoot or
  emulation. An ADB-forwarded loopback shell ran interactively as Debian root.
  This is native Linux userspace, but it still uses the signed Lenovo kernel and
  Android init as a temporary launcher.
- Lenovo's complete GPL source archive has been obtained and its 3.10.20 x86_64
  Moorefield kernel builds successfully. It is a source-compatible
  recovery baseline, not yet a byte-identical copy of the installed 2016 kernel.
- Shared storage, `/system`, GPT edges, p1-p8, both eMMC boot areas and a
  filesystem-aware userdata snapshot are backed up and hashed.
- A Debian 12 amd64 rootfs containing Phosh/Phoc is built and verified at
  `work/p90-native-port/artifacts/debian-bookworm-phosh-amd64.tar.xz`.
- A Linux 6.18.41 Moorefield/MID diagnostic kernel has been built and hashed,
  with its exact configuration and source kept separate from the vendor recovery
  kernel. It has not been booted on the phone.
- A real RAM-only kexec handoff is now proven. Both the rebuilt vendor kernel
  and an exact factory-kernel control are reset by Lenovo's Intel SCU watchdog
  before second-kernel USB appears. Persistent `emmc_ipanic` logging is readable
  and archived for the next controlled handoff capture. No unsigned image
  remains flashed; the exact signed S149 boot and recovery images are installed.
- A fresh-panic exact-stock control cleared the old crash record before the
  handoff. Android returned by watchdog without creating a new ipanic record,
  placing the blocker before normal second-kernel initialization rather than in
  Debian or Phosh.
- A second vendor 3.10.20 kernel has now been built specifically as a
  relocatable crash kernel with `KEXEC`, `CRASH_DUMP`, `PROC_VMCORE` and
  `RELOCATABLE` enabled. Its complete config and build log are retained and the
  build log contains no fatal errors.
- A separate candidate copy of the exact signed S149 boot image reserves
  `128M@0x40000000` through the Intel engineering command-line field. Only 29
  bytes in that 0x400-byte field differ; the signed MOSS header, payload and
  every byte after the field remain identical to stock. A guarded test entered
  Lenovo Recovery without reserving crash memory; exact stock was restored and
  Android was verified.
- Original IntelAndroid-FBRL has been validated as a RAM-only Droidboot root
  transport. It proved full image load/unload after correcting the 32-bit
  userspace architecture flag in our temporary loader. A 2026-08-09 exact-stock
  control with CPUs 1-3 offline still returned by watchdog, ruling out the
  secondary-CPU stop as the normal-kexec blocker.
- A native static x86_64 Android build of kexec-tools 2.0.32 now runs on the
  phone without the earlier architecture patch. Its load-only preserve-context
  probe proved that the production kernel lacks `CONFIG_KEXEC_JUMP` and rejects
  that flag with `EINVAL`.
- Debian Xorg now drives the native 1080x1920 `psbfb` framebuffer through a
  retained source-level `fbdevhw` compatibility patch. Phoc runs nested on that
  X server with pixman rendering, and Phosh reaches a complete ready state with
  its lock screen, panel, home and background surfaces. Xorg also recognizes
  the real Synaptics touchscreen through libinput. The successful guarded run
  lasted 20 seconds. A subsequent bind-launch test stopped SurfaceFlinger and
  kept Phosh alive for an eight-second exclusive framebuffer interval, then
  restored Android and removed every temporary display node.
- A reproducible Android-free initramfs now turns that proven runtime into a
  power-on Debian payload. PID 1 creates the P90's devices, loads the exact S149
  graphics modules, mounts the installed Debian tree and starts Xorg/Phoc/Phosh.
  It is packaged with the exact factory bzImage. One explicitly authorized
  boot-slot test entered Lenovo Recovery instead of Debian; exact signed stock
  was restored and Android verified. No trusted route capable of executing the
  unsigned image is yet available.
- A signed-stock-kernel fallback now boots the proven Debian graphics session
  automatically. Android init remains only as the trusted hardware/startup
  shim; its root `flash_recovery` service execs the dedicated Debian launcher.
  After a real reboot, Xorg acquired `psbfb`, Phosh became ready in 1.20 seconds,
  SurfaceFlinger stopped, and the Linux screen remained active with ADB recovery
  available. A non-root disable marker restores normal Android on the next boot.

## Intended progression

1. Do not repeat normal, crash or preserve-context kexec on the production
   kernel; chapter 11 records why each route is closed.
2. Identify a genuinely different trusted kernel-entry path while preserving
   the exact signed stock rollback images.
3. Turn the proven native Debian chroot and loopback shell into a reproducible,
   reversible launcher that does not depend on repeated manual probes.
4. Turn the proven Xorg/fbdev nested Phosh route into a persistent reversible
   service, then validate exclusive display ownership and end-to-end touch.
5. Bring up Wi-Fi and power management one subsystem at a time.
6. Treat a newer/mainline kernel as a separate hardware-enablement project.

## Important limitation

The CPU being 64-bit does not make this a PC-compatible phone. Boot, display,
touch, modem, cameras, sensors, charging and suspend depend on Lenovo/Intel board
support. The supplied kernel is 3.10.20 and contains the device-specific drivers;
a recent generic Debian kernel cannot simply replace it. Phosh normally expects
a modern Wayland/DRM graphics stack, so a native command-line Debian system is a
much nearer milestone than a fully accelerated Phosh phone.

## Layout

- `STATUS.md` — concise completed/not-completed state and flashing decision.
- `docs/01-device-and-backup.md` — discovered hardware, partitions and backups.
- `docs/02-build-and-boot.md` — source provenance, toolchain and Intel image format.
- `docs/03-debian-phosh-and-new-kernel.md` — staged port strategy and constraints.
- `docs/04-recovery-and-restore.md` — artifact validation and guarded recovery notes.
- `docs/05-live-bootloader-tests.md` — timestamped stock-firmware and live write-test log.
- `docs/06-kexec-ram-boot.md` — kexec tooling, exact factory control, watchdog analysis and live results.
- `docs/07-ipanic-and-next-kexec-debug.md` — persistent panic capture and next RAM-only debug test.
- `docs/08-fresh-stock-kexec-control.md` — isolated exact-stock handoff result and narrowed failure window.
- `docs/09-crash-kexec-kernel-and-signed-cmdline.md` — reproducible crash-kernel build, case-safe source repairs, boot candidate verification and guarded live-test procedure.
- `docs/10-fbrl-root-and-kexec-transition.md` — original FBRL provenance, RAM-only root mechanics, x86_64 compat-loader proof, watchdog analysis and CPU0-only control.
- `docs/11-crashkernel-and-preserve-context-results.md` — guarded crash-memory candidate result, native x86_64 Android kexec-tools build and preserve-context rejection.
- `docs/12-native-debian-and-phosh-under-stock-kernel.md` — installed native Debian chroot, interactive loopback shell, OpenSSH/seccomp result and Phosh graphics probes.
- `docs/13-power-on-debian-initramfs.md` — Android-free PID 1, P90 device setup, reproducible initramfs and tested factory-kernel candidate.
- `docs/14-persistent-debian-phosh-autoboot.md` — verified automatic screen-working Debian boot through the signed stock-kernel/init shim and reversible controls.
- `docs/23-native-camera-console-and-ril-v34.md` — verified factory-HAL JPEG capture, restored native KGX launch, and persistent XMM7260 oFono bridge.
- `docs/24-portrait-camera-app-v35.md` — installed portrait camera/gallery UI, non-blocking shutter repair, and real UI-to-JPEG verification.
- `scripts/build-vendor-kernel.sh` — deterministic vendor-kernel build entry point.
- `scripts/build-mainline-moorefield-kernel.sh` — separate Linux 6.x MID test build.
- `scripts/build-vendor-relocatable-crash-kernel.sh` — vendor crash-kernel configuration and build.
- `scripts/patch-signed-osip-cmdline.py` — changes only the unsigned Intel command-line field in a preserved signed OSIP image.
- `scripts/backup-raw-partitions.sh` — root-only, read-only partition preservation.
- `scripts/stitch_intel_image.py` — documented reimplementation of Lenovo's image packer.
- `scripts/make-vendor-rescue-image.sh` — packages the rebuilt kernel with Lenovo's root-ADB ramdisk.
- `scripts/build-debian-bookworm-rootfs.sh` — creates an amd64 Debian + Phosh rootfs archive.
- `scripts/build-p90-debian-initramfs.sh` — builds the reproducible P90-native early userspace from retained inputs.
- `scripts/p90-debian-autoboot-control.sh` — checks, enables or disables persistent Debian startup without requiring root.
- `scripts/make_intel_osip.py` — creates the unsigned Intel OSIP outer container.
- `scripts/wrap-intel-osip-image.sh` — reproducible OSIP packaging entry point.
- `manifests/SOURCES.md` — upstream locations, revisions and checksums.

The large source tree, compiler, build products and private phone backup live
outside this documentation directory under `work/p90-native-port/` and
`outputs/p90-backup/` respectively. The backup should not be published.
