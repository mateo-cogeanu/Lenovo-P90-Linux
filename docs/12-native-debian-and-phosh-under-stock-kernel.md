# Native Debian and Phosh under the signed stock kernel (2026-08-10)

## Result

A complete Debian 12 amd64 root filesystem is installed at
`/data/local/p90-debian`. It runs through a real kernel `chroot`, not PRoot or
CPU emulation. A root identity probe returned:

```text
debian-version=12.15
architecture=amd64
identity=uid=0(root) gid=0(root) groups=0(root)
kernel=Linux 3.10.20-x86_64_moor x86_64 GNU/Linux
complete=1 probe=0
```

An ADB-forwarded, loopback-only shell then ran the same commands interactively
inside Debian and confirmed that Phoc is installed. Phosh is packaged at
`/usr/libexec/phosh`; `phosh-session` is at `/usr/bin/phosh-session`.

This is native Debian userspace on the phone, but it still uses Lenovo's signed
3.10.20 kernel and Android init as the temporary root launcher. It is not yet a
custom-kernel boot or an Android replacement at power-on.

## Installation transport

The preserved xz archive was losslessly repacked as gzip because Lenovo's
BusyBox 1.16 tar supports gzip but not xz:

```text
work/p90-native-port/artifacts/debian-bookworm-phosh-amd64.tar.gz
SHA-256 196c12dc40fb7505f60b5b67c9fe724f6ecb4f6a569fdba48c0bfe1a96713b89
compressed size 285,168,708 bytes
expanded size 747,509,760 bytes
```

ADB could not reliably finish the single 285 MB transfer, so the archive was
split into four chunks below 95 MB, transferred, concatenated on `/data`, and
verified on the phone against the host SHA-256. No Android boot or system
partition was used for the rootfs.

After successful extraction and evidence capture, the four transfer chunks and
the redundant gzip copy were removed. One verified recovery archive remains at
`/data/local/tmp/p90-debian-rootfs-complete.tar.gz`; `/data` reports about
51.0 GiB free.

The FBRL/Droidboot attempt could not safely access the persistent `/data` tree
and repeatedly left `startftm` waiting. Fresh Droidboot power cycles recovered
the phone without data loss. This route must not be used again for Debian
filesystem work.

Android's already-audited temporary-root route was then used. A 6,384-byte
installer temporarily replaced the first pages of the factory `dumpstate`
executable, ran as UID 0, created `/data/local/p90-debian`, and extracted the
archive. BusyBox warned that it skipped POSIX pax `x` metadata headers, but
returned exit zero and extracted the corresponding file entries; the native
Debian probe confirms the resulting runtime tree is usable.

After every one-shot root operation, the complete 61,720-byte factory
`dumpstate` was restored. Its post-operation SHA-256 was always:

```text
1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c
```

The root service is stopped at handoff, and the SCU watchdog sysfs state is `0`
(enabled).

## Interactive shell transport

Debian OpenSSH 9.2 starts and listens through an ADB forward, but its pre-auth
sandbox calls `prctl(PR_SET_SECCOMP)`. Lenovo's production configuration has
`CONFIG_SECCOMP` disabled, so the call returns `EINVAL` and OpenSSH closes the
connection during key exchange. The complete debug log is retained.

A small alternative binds only Android loopback `127.0.0.1:22333`, accepts one
ADB-forwarded connection, chroots and execs Debian `/bin/sh`. It has no Wi-Fi or
cellular listener. An interactive PTY session returned:

```text
loopback-native-shell-ok
12.15
amd64
Linux 3.10.20-x86_64_moor x86_64 GNU/Linux
uid=0(root) gid=0(root) groups=0(root)
phoc=installed
```

The ADB forwards were removed after the test. This transport is a development
tool, not a production login service.

## Phoc and Phosh probes

The stock kernel exposes `/dev/dri/card0` and a connected 1080x1920 DSI panel
through Intel's proprietary `pvrsrvkm`/`psb` DRM stack, but no DRM render node.
It also exposes `psbfb` as Android's conventional `/dev/graphics/fb0` node
(major 29, minor 0). A static x86_64 Linux probe opened that node read-only,
queried both fbdev ioctls, and mapped one page without changing the display:

```text
id=psbfb
memory_bytes=8355840 line_length=4352 visual=2 type=0
visible=1080x1920 virtual=1080x1920 offset=0,0 bpp=32 rotate=0
red=16/8 green=8/8 blue=0/8 alpha=0/0
readonly_sample_bytes=4096 fnv1a32=76efddc5
```

This proves that native Debian can reach a standard XRGB8888 framebuffer. It
does not yet prove safe display ownership while SurfaceFlinger is stopped or
that Phoc can use fbdev directly.

The Synaptics touchscreen is independently usable through Linux evdev at
`/dev/input/event2`. A nonblocking ioctl-only probe reported ten multitouch
slots and axes that exactly match the panel:

```text
device=/dev/input/event2 name=synaptics_dsx
ABS_MT_POSITION_X value=0 min=0 max=1080 fuzz=0 flat=0 resolution=0
ABS_MT_POSITION_Y value=0 min=0 max=1920 fuzz=0 flat=0 resolution=0
ABS_MT_SLOT value=0 min=0 max=9 fuzz=0 flat=0 resolution=0
```

The corrected headless probe started Phoc, created Wayland display `wayland-0`,
added `HEADLESS-1`, and started `/usr/libexec/phosh`. Modern wlroots could not
find a DRM render node, then failed headless swapchain allocation and eventually
segfaulted. `WLR_RENDERER=pixman` and `WLR_RENDERER_ALLOW_SOFTWARE=1` did not
avoid the allocator requirement in this packaged build.

A real DRM probe, without stopping Android's display service, failed earlier in
backend creation:

```text
[backend/drm/drm.c:52] PRIME import not supported
[backend/backend.c:228] Failed to create DRM backend
Could not successfully create backend on any GPU
```

Therefore the next display task is not generic Phosh configuration. The
read-only framebuffer result makes a framebuffer bridge the nearest controlled
milestone, but a genuine Phosh display still requires one of:

1. a patched/older wlroots path that supports the legacy `psb` KMS driver and
   dumb software buffers;
2. a headless compositor with an allocator that does not require a render node,
   plus a framebuffer/VNC bridge; or
3. kernel DRM work adding the capabilities modern wlroots expects.

Do not stop SurfaceFlinger or claim a visible Phosh boot until one of these is
validated without taking over the physical display.

## Retained evidence

Live logs are under
`work/p90-native-port/live-test/2026-08-10-native-debian/`:

- `install.txt`
- `native-probe.txt`
- `loopback-shell.txt`
- `sshd-debug.txt`
- `phosh-headless.txt`
- `fbdev-readonly-probe.txt`
- `evdev-readonly-probe.txt`

Auditable launchers are under `outputs/p90-native-linux-port/tools/` with names
beginning `android-root-debian-`. The dedicated development SSH public key is
retained under `work/p90-native-port/keys/`; its private key is local-only and
must not be published.

The two read-only device probes are retained as
`tools/p90-fbdev-readonly-probe.c` and `tools/p90-evdev-readonly-probe.c`.

## Xorg fbdev bridge preparation

The archived Bookworm `libwlroots.so.10` contains its X11 backend, including
`wlr_x11_backend_create`. This gives Phoc a route that avoids the unsupported
DRM PRIME operation: an Xorg server owns `psbfb`, and Phoc runs as a fullscreen
nested Wayland compositor on that X11 display.

A reproducible downloader used the exact installed rootfs dpkg status database
to resolve Bookworm packages in an isolated x86_64 Debian VM. It produced a
25-package, 18.9 MB download set containing Xorg core, the fbdev driver, Xinit,
Xauth, utilities and only the dependencies absent from the phone rootfs. Every
`.deb` was checked against the generated SHA-256 list before installation.

Installation into `/data/local/p90-debian` completed with both `dpkg -i` and
`dpkg --configure -a` returning zero. `dpkg --audit` produced no output. The
phone now reports:

```text
xserver-xorg-core            2:21.1.7-3+deb12u12
xserver-xorg-video-fbdev     1:0.5.0-2
xinit                        1.4.0-1
xauth                        1:1.1.2-1
X.Org X Server               1.21.1.7
```

`/usr/lib/xorg/modules/drivers/fbdev_drv.so` is installed and has no unresolved
shared-library dependencies. Xorg was queried only for its version; it was not
started against the framebuffer, SurfaceFlinger remained running, and no pixel
write occurred. The next controlled display test is therefore Xorg on
`/dev/graphics/fb0`, followed by Phoc with `WLR_BACKENDS=x11` if Xorg succeeds.

The bundle recipe is
`scripts/build-xorg-fbdev-package-bundle.sh`; the retained bundle and audit log
are respectively under `work/p90-native-port/package-bundles/` and the live-test
directory listed above.

## Native Xorg, touchscreen and Phosh success

Xorg's stock `fbdevhw` fallback rejects framebuffer sysfs paths containing
`devices/pci`, intended to prevent accidental PCI framebuffer selection. The
P90 GPU advertises display class `0x0380` rather than VGA class `0x0300`, so it
does not enter Xorg's PCI fbdev probe and then hits that fallback rejection.

Patch `0200-xorg-fbdevhw-allow-explicit-pci-framebuffer.patch` narrows the
rejection to auto-detection. An administrator's explicit
`Option "fbdev" "/dev/graphics/fb0"` is accepted. The exact Debian
`xorg-server 21.1.7-3+deb12u12` source and rebuilt `libfbdevhw.so` are retained.
The module initialized `psbfb`, selected depth 24 in a 32-bit framebuffer,
created a 1080x1920 shadow framebuffer and brought X display `:1` online.

Bookworm `xserver-xorg-input-libinput 1.2.1-1+b1` was then installed. Android
does not run udev, so the launcher writes the minimal initialized device record
for character device 13:66. Xorg consequently reports:

```text
event2 - synaptics_dsx: is tagged by udev as: Touchscreen
event2 - synaptics_dsx: device is a touch device
XINPUT: Adding extended input device "P90 touchscreen" (type: TOUCHSCREEN)
```

The first nested Phoc run exposed a second Android compatibility detail:
wlroots 0.15 uses POSIX `shm_open`, but Android's `/dev` has no `/dev/shm`.
Creating a mode-1777 directory for the guarded session fixed both Wayland keymap
and pixman swapchain allocation. The launcher removes it again at cleanup.

Phoc then created its X11 backend, pixman renderer, Wayland display and X11
keyboard, pointer and touch devices. Installing
`gnome-settings-daemon-common 43.0-4` supplied a schema that Phosh loads
directly. In the final guarded run Phosh mapped its lock screen, top panel, home
and background surfaces, reported `Phosh ready after 0.85s`, and survived the
full 20-second interval. Xorg terminated successfully, the temporary `/dev/fb0`
link and `/dev/shm` directory were removed, and SurfaceFlinger stayed running.

Retained evidence includes:

- `xorg-fbdev-patched-success.txt`
- `xorg-phoc-phosh-first-run.txt`
- `xorg-phoc-phosh-shm-touch-success.txt`
- `xorg-phoc-phosh-sustained-success.txt`
- `fbdevhw-module-install.txt`

This is genuine native Debian graphics userspace on the physical framebuffer,
not emulation. It is still a nested, reversible development session launched by
Android init on Lenovo's signed 3.10.20 kernel. Exclusive display ownership and
a power-on Debian boot remain separate milestones.

## Guarded exclusive-display results

The guarded test was executed with exact factory-hash and empty-kexec preflight,
plus an independently held ADB recovery command. Directly stopping
SurfaceFlinger caused the `dumpstate` wrapper to terminate before cleanup. The
recovery command restarted Android as designed. The factory executable was
verified and the two temporary display nodes were removed afterward.

A follow-up attempted to freeze rather than stop SurfaceFlinger. The launcher
restored its factory backing file before reaching the signal code. That is safe
for the loopback launcher only because it has already `exec`ed Debian `/bin/sh`;
the graphics wrapper still needs its own later code pages. It exited before
logging a SurfaceFlinger PID or signal result. Its already-executed Xorg, Phoc
and Phosh children remained alive and Phosh continued updating its clock for
several minutes, proving sustained native userspace but not exclusive display
ownership. Those exact named test processes were terminated and `/dev/fb0` plus
`/dev/shm` were removed. Final checks showed SurfaceFlinger running, root service
stopped, exact factory `dumpstate`, and both kexec slots empty.

The experimental script is retained for audit but now exits immediately. This
temporary executable-replacement route must not be used for further display
experiments. A persistent launcher must come from an ordinary trusted service,
ramdisk, or boot path whose executable is not being restored underneath it.

## Exclusive framebuffer ownership achieved

A follow-up used a file bind mount instead of changing the running launcher's
backing executable. The sequence was:

1. enter the already-audited loopback root shell and immediately restore and
   verify the exact factory `dumpstate`;
2. bind-mount the proven Xorg/Phosh launcher over the factory pathname;
3. start the init service and have the launcher call
   `umount2("/system/bin/dumpstate", MNT_DETACH)` before graphics setup;
4. pull and hash `/system/bin/dumpstate` again, proving that the untouched
   factory file was visible before display takeover;
5. start Xorg, Phoc and Phosh, stop SurfaceFlinger for eight seconds, then start
   Android's display service again; and
6. terminate the Debian graphics session and remove its temporary `/dev/fb0`
   link and `/dev/shm` directory.

The first ordinary unmount correctly failed with `EBUSY` and the launcher
refused to continue. The corrected lazy-detach run reported:

```text
launcher-self-unmount-result=0 errno=0 (Success)
Phosh ready after 0.91s
surfaceflinger-stop-result=0
surfaceflinger-start-result=0
phoc-phosh-survived-20s=1
Server terminated successfully (0)
temporary-fb0-link-remove=0
temporary-dev-shm-remove=0
probe-complete=1
```

Thus Debian Xorg/Phosh retained the open physical `psbfb` device while Android's
display compositor was stopped: exclusive framebuffer ownership is proven. The
independent recovery command also fired and performed a redundant Android
display restart. Final checks found no Xorg/Phoc/Phosh process, no bind mount,
no temporary display node, SurfaceFlinger running, both kexec slots empty, and
the exact factory hash.

The complete evidence is
`xorg-phoc-phosh-exclusive-framebuffer-success.txt`. This is still launched
after Android boot on Lenovo's signed kernel; converting the same ownership
sequence into a trusted ramdisk/init service is the next step toward power-on
Debian.
