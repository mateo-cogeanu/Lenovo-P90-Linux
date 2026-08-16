# Phosh application stability and persistent v25

Date: 2026-08-16

## Symptoms and diagnosis

GNOME Settings exited when opened, Epiphany crashed, and Console or Maps could
make the UI appear frozen. Idle memory was not the cause: the P90 had about
2.5 GiB free. Four separate problems were found:

1. Phoc's pixman renderer produced a full 1080x1920, 8.3 MiB software frame and
   the outer compositor copied it to PowerVR repeatedly. Complex GTK/WebKit
   views saturated CPU and memory bandwidth.
2. Linux 3.10 has no `memfd_create(2)`. The compatibility shim used POSIX
   shared memory but did not emulate `F_ADD_SEALS`/`F_GET_SEALS`, causing
   WebKit's `Failed to write memfd` fatal error.
3. Bubblewrap could not create the namespaces WebKit requested on this old
   kernel (`Creating new namespace failed: Invalid argument`).
4. WirePlumber's combined configuration loaded its Bluetooth/logind module.
   There is no logind session in this Android-init compatibility stage, so the
   session manager exited.

## v25 changes

- The SurfaceFlinger/PowerVR surface remains 1080x1920, but the nested Wayland
  output is 540x960. PowerVR scales the final texture to the panel. This cuts
  pixman rendering and upload traffic to one quarter of the previous size.
- Touch coordinates exposed by the outer server use the same 540x960 output
  coordinate space.
- The 64-bit compatibility shim now remembers emulated memfd seals for the
  POSIX shared-memory descriptors it creates.
- The Phosh session exports `WEBKIT_DISABLE_COMPOSITING_MODE=1` and
  `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1`.
- WirePlumber starts as two supported instances, `-c main.conf` and
  `-c policy.conf`, which retain ALSA discovery and routing without loading the
  Bluetooth/logind configuration.
- The standard `('xkb', 'us')` input source remains configured for Squeekboard.

Disabling the WebKit sandbox is a security compromise. It is necessary here
because the stock 3.10 kernel and `/data`-hosted rootfs cannot provide the
namespace/setuid environment bubblewrap requires. This should be removed if a
future native kernel and real Debian PID 1 supply user namespaces.

## Tests

- GNOME Settings stayed alive for the entire guarded 12-second test. Its
  missing SettingsDaemon brightness property was non-fatal.
- Epiphany first reproduced the memfd fatal error, then reproduced the
  bubblewrap namespace failure, and finally stayed alive for the full
  10-second test after both compatibility fixes.
- Maps stayed alive for its full 10-second test.
- `main.conf` and `policy.conf` WirePlumber instances both stayed alive in the
  bounded test and both are present after a clean v25 reboot.
- The final boot reported `first shm frame presented: 540x960`, advancing frame
  hashes, `upload-gl=0x0`, `draw-gl=0x0`, and `linux-only-ui=1`.
- After settling, about 2.58 GiB of 3.99 GiB RAM was free.

Evidence is retained under:

```text
work/p90-native-port/live-test/2026-08-16-app-stability-v25/
  autoboot-v25.txt
  process-memory-v25.txt
  control-center-test.txt
  web-maps-test.txt
  wireplumber-main-policy-test.txt
```

## Installed artifacts

```text
/system/bin/p90-debian-autoboot-v25
  b5f14fd5478bd81bded7440ba28ce78a8e5e4fad9be0a6d42a074e27c1d8f3f6

/data/local/tmp/p90-hybris/bin/p90-wayland-sf-compositor
  9d190500c8dd25da1a4ce8d2ed28b41c247c38cb1b6eaa1488c704ffcd09fde4

/data/local/tmp/p90-getrandom-compat.so
  bdc360979eef911c254a5a9561f64e7dc9edc7fdcd54ff9649115d8d509e9a00
```

The factory `/system/bin/dumpstate` was restored and verified as
`1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c`.
`/system` was verified read-only after installation.

## Remaining limitations

This remains the stock Lenovo 3.10 kernel plus Android init, Binder,
SurfaceFlinger, and proprietary PowerVR userspace. It is not yet the eventual
Android-free Debian boot. ALSA devices are discovered, but the correct wm8958
phone routing profile is incomplete; `callaudiod` still reports no suitable
card. Wi-Fi, modem/SIM, suspend, camera, and complete brightness integration
remain separate hardware-porting work.

To disable the persistent Debian UI for recovery, create
`/data/local/tmp/p90-debian-autoboot-disabled` and reboot. The factory hook is
retained at `/system/etc/install-recovery.sh.p90-stock`.
