# Debian, Phosh and a newer kernel

## Why this needs stages

A Debian root filesystem is CPU-portable; a phone kernel is hardware-specific.
The P90's 64-bit x86 CPU can run Debian amd64 binaries, but a generic amd64 kernel
does not know Lenovo's Moorefield board wiring or necessarily include working
display, touch, modem, charging, camera, sensor and suspend drivers.

The practical first native system is therefore Debian userspace on Lenovo's
3.10.20 kernel. This is a bring-up environment, not the desired long-term secure
kernel.

The rootfs build script deliberately targets Debian 12 (Bookworm), not the
newest Debian release. Its older userspace gives the 3.10 vendor kernel a better
chance of supporting early bring-up. It installs amd64 Phosh/Phoc packages but
locks both accounts; credentials or an SSH key must be added locally during
installation. The archive can first be extracted below Android's ext4 userdata
and entered with `chroot`, avoiding repartitioning. A later rescue initramfs can
mount that filesystem and switch root into the same tree.

## Completed Debian artifact

`work/p90-native-port/artifacts/debian-bookworm-phosh-amd64.tar.xz` is a verified
amd64 Bookworm rootfs containing Phosh 0.24, Phoc 0.24, Mesa 22.3, NetworkManager,
OpenSSH, BusyBox, libinput/evtest, PCI/USB tools and basic administration tools.

- Size: about 166 MiB compressed
- SHA-256: `98812c5f1fc6b2ea628fb4f2fbc9fe5aaabcba24f845900f7bfab5032318e4c7`
- Hostname: `lenovo-p90`
- Accounts: `root` and `p90` are locked; no default password is embedded

The archive passes `xz -t` and contains numeric ownership, xattrs and ACLs. It is
not installed on the phone because the bootloader route remains unproven.

## Proposed milestones

1. **Rescue initramfs:** root ADB, storage tools, full device backup.
2. **Minimal Debian:** read-only or external/test root, console/SSH, no Android
   replacement yet.
3. **Hardware inventory:** preserve dmesg, ACPI tables, PCI/USB/I2C/SPI devices,
   firmware files and loaded-module parameters.
4. **Input and display:** prove framebuffer or DRM output and touch input.
5. **Networking/power:** Wi-Fi firmware, charging, battery reporting and suspend.
6. **Graphical shell:** test Mesa software rendering and a Wayland compositor;
   attempt Phosh only when its compositor can start reliably.
7. **Install layout:** use a reversible dual-layout or separate test partition
   before considering replacement of Android userdata/system.

## Phosh reality check

Phosh is a shell on top of Wayland and typically uses Phoc/wlroots. A vendor
Android framebuffer and proprietary PowerVR-era graphics modules are not the
same as a modern DRM/KMS + Mesa stack. Software rendering may permit a slow
prototype, but it is not guaranteed on this old kernel. A lightweight fbdev or
X11 interface may be needed during bring-up.

## Newer kernel track

Linux 6.18.41 was selected as the current long-term test baseline whose digest
was present in kernel.org's signed checksum manifest. Its tree still explicitly
supports `CONFIG_X86_INTEL_MID`, Z35xx Moorefield pinctrl, SCU, MID serial,
SDHCI PCI and DWC3 PCI. Source and archive are retained below
`work/p90-native-port/mainline/`; the build is driven by
`scripts/build-mainline-moorefield-kernel.sh`.

The diagnostic build completed successfully. Its files are retained under
`work/p90-native-port/artifacts/mainline-6.18.41/`:

- `bzImage` — SHA-256
  `3634455c543980ee2ae12bfd15f3e2dd958f7eb2512a58fa9f3850263da1c27e`
- `kernel.config` — SHA-256
  `e3f600420287629f77558658e98ce8c9e3404174a4b87e0bd2eaf3ea59888b4b`

The verified final configuration enables `X86_INTEL_MID`,
`PINCTRL_MOOREFIELD`, `GPIOLIB`, `GPIO_MERRIFIELD`, `SERIAL_8250_MID`,
`INTEL_SCU_PCI`, `INTEL_MID_WATCHDOG`, `MMC_SDHCI_PCI` and `USB_DWC3_PCI`.
Desktop `DRM_I915` is deliberately disabled: Moorefield phone graphics use a
PowerVR/board-specific path, while DRM core remains available for future display
work. The image is an ordinary x86 bzImage, not a Lenovo-stitched or signed p1
image, and has not been booted or flashed.

This does not imply Lenovo P90 hardware support is complete. Mainline MID support
does not contain Lenovo's display panel description, touchscreen integration,
PowerVR Android graphics stack, modem configuration or all board-specific power
sequences. The resulting bzImage is a diagnostic kernel, not an install image.

Keep this separate from the Debian-rootfs work:

1. Boot a current upstream x86_64 kernel with maximal Intel MID/Atom support and
   a rescue initramfs; do not install it.
2. Capture its earliest serial/ramconsole output and compare enumeration with the
   Lenovo kernel.
3. Forward-port platform data, ACPI quirks and only the essential drivers in
   small, reviewable patches.
4. Prioritize eMMC, USB, display and input before radio/modem/camera support.
5. Keep Lenovo's 3.10 source, build config, compiler and rescue image indefinitely
   as the recovery baseline.

A recent kernel version alone is not success. The acceptance criterion is a
repeatably booting kernel that preserves charging and storage safety and exposes
enough standard interfaces for Debian and a compositor.
