# Port status

## Completed

- Identified Lenovo P90 / Moorefield hardware, Android build and legacy Intel
  Droidboot behavior.
- Preserved shared storage, `/system`, GPT edges, p1-p8, eMMC boot0/boot1 and a
  validated filesystem-aware p9 userdata snapshot.
- Verified raw transfers at both ends and generated SHA-256 manifests.
- Restored every temporarily modified Android executable byte-for-byte and
  removed all temporary payloads from the phone.
- Downloaded and retained Lenovo's GPL source; built a 3.10.20 vendor recovery
  bzImage and unflashed Intel-format root-ADB rescue image.
- Built and verified a Debian 12 amd64 rootfs with Phosh/Phoc.
- Retained verified Linux 6.18.41 long-term source and built an explicit
  Moorefield/MID diagnostic bzImage with its exact configuration preserved.
- Proved the stock kernel's `kexec_load` path with a successful RAM-only
  load/unload and performed real execution handoffs after exact Android restore.
- Extracted and validated the exact factory bzImage and ramdisk from signed S149
  OSIP for a controlled kexec comparison.
- Captured and archived Lenovo's persistent `emmc_ipanic` records. The existing
  record proves the panic path works but appears stale because it lacks the
  kexec helper markers.
- Cleared the archived ipanic record and ran one exact-factory S149 RAM-only
  kexec control with verbose logging. Android returned by watchdog, but no new
  ipanic header, console or thread record was created.
- Built a relocatable vendor 3.10.20 crash kernel with `CONFIG_KEXEC=y`,
  `CONFIG_CRASH_DUMP=y`, `CONFIG_RELOCATABLE=y`, `CONFIG_PROC_VMCORE=y`,
  `CONFIG_PHYSICAL_START=0x1000000` and `CONFIG_PHYSICAL_ALIGN=0x1000000`.
  The resulting 7,057,408-byte bzImage is retained with its exact config and
  complete zero-fatal-match build log.
- Repaired the case-sensitive netfilter source/header pairs that Lenovo's
  archive loses when extracted on a default case-insensitive macOS filesystem.
  The match and target implementations were compiled separately and then linked
  successfully into the complete kernel.
- Produced a candidate from the byte-exact signed S149 boot image by
  appending `crashkernel=128M@0x40000000` only in its 0x400-byte engineering
  command-line field. Exactly 29 bytes differ; bytes before `0x4d8` and from
  `0x8d8` onward are identical to stock.
- Validated original IntelAndroid-FBRL as a temporary RAM-only Droidboot root
  route. A patched copy of our own loader passed the correct x86_64 architecture
  flag from 32-bit Droidboot and completed full image load/unload successfully.
- Ran an exact-stock CPU0-only control on 2026-08-09. CPUs 1-3 were offlined
  before execution, but the phone still returned with boot reason `watchdog`.
  The exact factory `dumpstate` hash and unloaded kexec state were verified.
- Tested the command-line crash-memory candidate on 2026-08-10. It entered
  Lenovo Recovery with `kexec_crash_size=0`; exact signed S149 boot was restored
  and Android booted normally.
- Built and phone-tested static native x86_64 Android kexec-tools 2.0.32. Its
  RAM-only `bzImage64` preserve-context probe reached the syscall without a
  loader patch, then safely failed with `EINVAL` because the production kernel
  lacks `CONFIG_KEXEC_JUMP`. Both kexec slots remained unloaded.
- Installed Debian 12.15 amd64 at `/data/local/p90-debian` and proved it through
  a real root chroot and an interactive ADB-forwarded loopback shell. The native
  shell reported the Lenovo x86_64 kernel and confirmed Phoc is installed.
- Started Phoc and Phosh in a native headless probe. The result isolates the
  visible-display blockers to wlroots buffer allocation: no DRM render node for
  headless mode and no PRIME import support from Lenovo's `psb` KMS driver.
- Confirmed Debian OpenSSH cannot serve this kernel unchanged because its
  pre-auth sandbox requires `CONFIG_SECCOMP`; the loopback shell avoids network
  exposure and does not depend on seccomp.
- Proved native Debian can query and read-map the physical `psbfb` display at
  `/dev/graphics/fb0`: 1080x1920 XRGB8888, 4352-byte stride and 8,355,840 bytes
  of framebuffer memory. No pixels were written and SurfaceFlinger remained
  running.
- Proved native Debian can query the `synaptics_dsx` touchscreen through evdev.
  Its multitouch axes are 0..1080 by 0..1920 with ten slots.
- Installed and audited Bookworm Xorg 1.21.1.7 plus its fbdev driver inside the
  Debian tree. The exact wlroots library includes an X11 backend, making
  Xorg-on-`psbfb` plus nested Phoc the next visible-display route without PRIME.
- Patched Debian's Xorg `fbdevhw` source so an explicitly configured PCI-backed
  framebuffer is accepted on Moorefield's display-class (`0x0380`) GPU. The
  rebuilt module initialized `psbfb` at 1080x1920 and terminated cleanly.
- Installed Bookworm's libinput driver and supplied the missing udev device
  metadata. Xorg recognized `/dev/input/event2` as the physical Synaptics
  touchscreen and exposed it to the nested compositor.
- Ran Phoc's X11 backend with a pixman renderer on that native Xorg server.
  After providing POSIX `/dev/shm` and GNOME's touchscreen schema, Phosh mapped
  its lock screen, top panel, home and background surfaces, reported ready in
  0.85 seconds, and remained healthy for the complete 20-second guarded run.
- Proved exclusive framebuffer ownership with a reversible bind-launch route.
  The launcher lazily detached its own bind mount, exposing and re-verifying the
  exact factory executable before stopping SurfaceFlinger. Phosh remained alive
  for the complete eight-second exclusive interval, Android restarted, Xorg
  exited cleanly, and all temporary nodes were removed.
- Built a reproducible Android-free P90 initramfs. Static BusyBox is PID 1; it
  explicitly creates devices because the factory kernel lacks devtmpfs, loads
  the matching S149 display modules, mounts p9 and starts the proven native
  Debian Xorg/Phoc/Phosh stack. Two builds were byte-identical. The initramfs
  and exact-stock-kernel Intel image are retained. One authorized OSIP-wrapped
  boot-slot test entered Lenovo Recovery instead of Debian; exact stock was
  restored and Android verified healthy.
- Installed and reboot-verified a reversible automatic Debian/Phosh startup
  through the signed stock kernel and Android init shim. The root
  `flash_recovery` service now execs a dedicated launcher, Xorg owns `psbfb`,
  Phosh reached ready state in 1.20 seconds, SurfaceFlinger is stopped, and the
  physical touchscreen is initialized. Ordinary ADB can disable the next boot
  by creating `/data/local/tmp/p90-debian-autoboot-disabled`.
- Installed and reboot-verified persistent v26 Wi-Fi and speaker audio.  The
  Debian chroot now sees Lenovo's read-only Broadcom firmware path, NetworkManager
  returns real access-point scans, and the WM8958 internal speaker is the
  default 48 kHz S16_LE stereo PipeWire sink.  Default-target playback succeeds.
- Installed and reboot-verified persistent v28 Wi-Fi association.  The saved
  WPA2 profile now automatically associates after the required legacy-driver
  NetworkManager reinitialization, receives DHCP address `192.168.111.64`, and
  reaches the public internet with zero packet loss.
- Installed and reboot-verified persistent v29 Console repair.  The app-grid
  Console icon now launches native-Wayland `foot` instead of the freezing
  KGX/VTE path, and Squeekboard selects its terminal layout when the terminal
  receives text focus.  The package-owned KGX files remain intact for rollback.
- Installed and reboot-verified persistent v30 microphone and portrait-keyboard
  integration.  PipeWire exposes a real `Lenovo P90 Microphone`; an
  application-level recording contained 131,072 samples with a -21.8 dB mean.
  The OSK loads the taller 52-pixel portrait layout from the local override.
- Installed Bookworm Megapixels and v4l-utils.  AtomISP, OV5693, IMX214 and the
  flash enumerate, but frame capture remains blocked by the proprietary
  CSS2400 pipeline; enabling the generic media link reset USB without a frame.
- Installed and reboot-verified persistent v31 camera crash guard. Megapixels'
  missing x86/ACPI profile was reproduced exactly; the Camera icon now shows a
  Phosh hardware-status notification instead of exiting or programming guessed
  AtomISP links. This guard was the safe baseline later superseded by the
  verified factory-HAL capture route below.
- Proved the factory camera path without programming generic media links. A
  source-built Bionic client supplies the HAL's required preview buffers through
  a hidden SurfaceFlinger surface and captured a valid 640x480 rear-camera JPEG
  while Zygote, system_server, and mediaserver remained absent. Persistent v34
  is installed and reboot-verified: its bounded root worker returned status 0
  and saved another valid 640x480 JPEG, and its portrait GTK camera UI survived
  a guarded native-Wayland launch test. The v35 UI upgrade replaces the
  blocking proof button with a portrait shutter, progress/error feedback, and
  an in-app previous/next gallery. Its real GTK shutter callback produced and
  displayed a third verified 640x480 JPEG.
- Connected Debian oFono 1.31 to Lenovo's XMM7260 RIL socket. `/ril_0` exposes
  SIM, call-volume, and voice-call interfaces persistently; the modem currently
  reports no SIM card, so SIM-dependent behavior remains unverified.
- Re-tested GNOME Console after isolating the GTK preload environment. KGX
  stayed responsive for the complete guarded Wayland run at about 33 MiB RSS;
  v34 restores its direct app-grid launch while retaining Foot as a fallback.

## Not completed

- Bootloader unlock is not confirmed. Standard unlock commands are unsupported
  or hang.
- Droidboot accepts `fastboot boot` transfers but does not execute them.
- No rescue or newer kernel has reached observable USB userspace. Native Debian
  userspace is observable and interactive, but still runs on the signed stock
  kernel through Android init.
- The power-on Debian initramfs is built and internally verified but has not
  executed on hardware; no known trusted entry path currently accepts it.
- Both rebuilt-vendor and exact-factory kexec controls currently reset through
  the Intel SCU watchdog at the same interval.
- Offlining all secondary CPUs does not change the exact-factory kexec result.
- Wi-Fi scanning, association, DHCP/internet traffic, internal-speaker output,
  application-level microphone capture, and rear-camera still capture are now
  proven. Front-camera/video behavior, headset switching, telephony audio,
  modem data, charging integration and suspend remain unproven. Native
  framebuffer output, touch interaction, sustained Phosh and PowerVR-backed
  presentation are proven.
- The guarded exclusive-display test proved that stopping SurfaceFlinger ends
  the temporary `dumpstate` wrapper before cleanup. A freeze variant never reached its
  signal code because restoring the wrapper's backing executable early ended
  the wrapper; its native Xorg/Phoc/Phosh children nevertheless stayed healthy
  for several minutes. They and their temporary nodes were cleaned explicitly.
  The experimental script is disabled and this launcher route is closed.
- The later bind-mounted route became the basis for the persistent signed-stock
  shim. Automatic screen-working Debian boot is achieved, although Android init
  remains the first-stage PID 1 rather than the Android-free initramfs.
- No crash-memory reservation is available. The guarded command-line candidate
  failed to establish one and must not be retried unchanged.
- Preserve-context loading is unavailable because the production kernel lacks
  `CONFIG_KEXEC_JUMP`.
- The recovery slot was used for one controlled unsigned-image execution test;
  the image did not execute and the exact matching signed S149 recovery was
  immediately restored. The exact signed stock boot image was subsequently
  rewritten to recover Android from a persistent recovery boot loop. No custom
  boot, Droidboot or system image has been written. Userdata now contains only
  the dedicated Debian rootfs and its staging archive in documented paths.

## Live test update: 2026-08-03

- Acquired and validated the exact S149 stock firmware package.
- Confirmed Lenovo's factory recipe flashes OSIP images by logical names rather
  than writing the raw 256 MiB reserved partition.
- Reflashed only `recovery` with the exact signed S149 `recovery.img`; fastboot
  reported both transfer and write success.
- `fastboot reboot` powered the device down; after a physical power-on, Android
  booted normally and its health and 93% battery level were verified.
- Flashed the unsigned OSIP-wrapped vendor rescue to only the recovery slot.
  Droidboot reported a successful write, but after reboot/power-on the device
  returned to Droidboot and never exposed rescue ADB.
- This behavior strongly indicates execution-time signature rejection, although
  it is an inference: the legacy `getvar secure` response was empty.
- Immediately restored the exact signed S149 recovery; Droidboot reported the
  restore write as successful. `fastboot continue` entered the restored Lenovo
  recovery in `fota` boot mode. Recovery ADB worked, no recovery command or wipe
  was queued, and a plain `adb reboot` was issued to request normal Android boot.
- Recovery and Droidboot plain-reboot attempts continued returning to recovery.
  Rehashed and flashed the exact signed S149 stock `boot.img` to the logical
  `boot` target. Droidboot reported success, and its plain Reboot entry then
  started Android normally.
- Verified build `P90_S149_160504_ROW`, boot mode `main`, completed Android boot,
  100% battery, expected filesystem usage, and exact original hashes for
  `/system/bin/run-as` and `/system/bin/dumpstate`.
- No custom boot image, Droidboot image, system image or userdata was written.
- The RAM-only kexec loader returned `load exit=0`; execution disconnected ADB.
  Android returned with `ro.boot.bootreason=watchdog`, and the factory
  `dumpstate` hash remained exact after every attempt.
- The published source shows why a userspace watchdog stop is insufficient:
  the reboot notifier re-arms it inside `kernel_restart_prepare()`.
- A minimal in-kernel handoff shim was built but the production kernel rejected
  the unsigned module with `ENOKEY`, despite Lenovo's published defconfig saying
  module signatures are disabled.
- The fresh exact-stock control completed. Its absence of a new ipanic record
  narrows the failure to the pre-init kexec transition window; it is not a
  Debian, Phosh, rebuilt-kernel or mainline-kernel userspace failure.

## Go/no-go decision

Current status is **no-go for repeating normal, crash or preserve-context
kexec**. Their distinct failure modes are recorded in chapters 10 and 11. It is
also no-go for raw p1 writes, unsigned boot/recovery images, signature bypasses
or broad userdata replacement. The dedicated Debian tree is the only authorized
userdata installation. The exact signed stock boot image remains the rollback
artifact.
