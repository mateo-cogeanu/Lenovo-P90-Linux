# Persistent Debian/Phosh autoboot through the signed stock kernel

## Achieved result

On 2026-08-15 the P90 completed an automatic startup into the native Debian
graphics stack. The software-visible session is:

```text
Lenovo signed 3.10.20 kernel
  -> Android init as a trusted hardware/boot shim
  -> root flash_recovery one-shot service
  -> /system/bin/p90-debian-autoboot
  -> Debian Xorg on psbfb
  -> Phoc X11 backend with pixman
  -> Phosh
```

This is native x86_64 Debian userspace and native framebuffer/touchscreen I/O,
not emulation, PRoot or remote display. It is also not yet the Android-free PID
1 payload from chapter 13: Lenovo's signed kernel and Android init remain the
trusted first-stage shim because the production boot chain rejects every
unsigned boot image tested.

The verified v5 persistent boot reported:

```text
ro.bootmode=main
ro.boot.bootreason=watchdog
sys.boot_completed=1
init.svc.flash_recovery=running
init.svc.surfaceflinger=stopped
x11-ready=1
android-boot-complete-after=17
Phosh ready after 1.26s
surfaceflinger-stop-result=0
linux-screen-active=1
kexec_loaded=0
kexec_crash_loaded=0
```

The persistent launcher was PID 237, a direct child of init. Xorg, Phoc and
Phosh were its descendants. This proves the startup hook ran automatically
after the reboot rather than being held open by the host-side test launcher.

## Trusted startup hook

The stock ramdisk declares:

```text
service flash_recovery /system/etc/install-recovery.sh
    class main
    oneshot
```

A temporary bind-mount probe proved this service executes as `uid=0`,
`gid=0`, SELinux `u:r:init:s0`. The bind was lazily detached and the original
script remained byte-exact. The original script SHA-256 is:

`803b1a22af243feb956b5faa0cecf36304421fde13b4cb33b26c7c3f222d1047`

For persistent startup it is preserved on the phone at
`/system/etc/install-recovery.sh.p90-stock`. The active 134-byte hook only
checks the disable marker and then executes
`/system/bin/p90-debian-autoboot-v5`.
Both files retain `u:object_r:system_file:s0`; `/system` was verified read-only
after installation.

The launcher source is
`outputs/p90-native-linux-port/tools/android-root-debian-phosh-autoboot.c`.
It waits for userdata, Xorg, Phoc, `psbfb`, and Android's late
`sys.boot_completed=1` transition; binds `/dev`, `/proc` and `/sys` into
Debian; provides POSIX shared memory and the proven touchscreen udev record;
stops SurfaceFlinger; starts Xorg and Phosh; verifies that both remain alive;
and activates fbdev scanout. Waiting for boot completion is essential: v3
stopped SurfaceFlinger too early, after which Android restarted it and reset
the backlight to zero. A later sustained observation showed Android can try to
restart SurfaceFlinger again after `sys.boot_completed`. V5 therefore
supervises the display for the lifetime of Phosh: it stops any reappearing
SurfaceFlinger, reapplies fbdev scanout, and restores the backlight if Android
sets it to zero. If the compositor exits normally, v5 terminates Xorg and
restores Android's display service.

Current v5 launcher SHA-256:

`c84576639c5b694c670ec42aa0aa86cf80dc41c376bcabf53300ac9086f829d4`

Active hook SHA-256:

`a2bd0b5159f3f7df757c10eaceeb44fb6b48dd23c619e6cea6256de00313e828`

Factory `dumpstate` was restored after every privileged installation step and
verified at SHA-256
`1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c`.

## Reversible control

The escape hatch is deliberately writable from ordinary ADB, without another
root operation:

```sh
./outputs/p90-native-linux-port/scripts/p90-debian-autoboot-control.sh status
./outputs/p90-native-linux-port/scripts/p90-debian-autoboot-control.sh disable
./outputs/p90-native-linux-port/scripts/p90-debian-autoboot-control.sh disable-reboot
./outputs/p90-native-linux-port/scripts/p90-debian-autoboot-control.sh enable
```

The marker is `/data/local/tmp/p90-debian-autoboot-disabled`. When present, the
hook exits immediately and Android starts normally, including SurfaceFlinger.
The `disable` and `enable` commands do not disturb the running session; variants
ending in `-reboot` explicitly request a reboot.

The exact factory hook backup remains on both the phone and host. Restoring it
fully requires a root remount of `/system`, copying
`install-recovery.sh.p90-stock` over `install-recovery.sh`, verifying the
factory hash above, and remounting read-only. The marker is the safer immediate
recovery mechanism and should be used first.

## Retained evidence

The complete logs are under:

`work/p90-native-port/live-test/2026-08-15-persistent-debian-autoboot/`

- `p90-install-recovery-hook-probe.txt` proves init UID and SELinux context;
- `p90-install-recovery-bind-probe.txt` proves bind, service start and detach;
- `p90-debian-autoboot-install.txt` records the successful explicit ext4
  remount and byte installation before the watchdog reboot;
- `p90-debian-autoboot.txt` contains complete Xorg, Phoc and Phosh output from
  the automatic post-reboot session.

The installer log ends after both files were fsynced because the phone rebooted
with boot reason `watchdog` during the final synchronization/remount sequence.
On the next boot `/system` mounted read-only, both installed files matched their
host hashes, the stock hook backup matched its factory hash, and the automatic
Debian/Phosh launch succeeded. This distinction is retained rather than
claiming the installer itself requested or cleanly completed that reboot.

The host-held 60-second recovery guard from the reversible test later restarted
SurfaceFlinger even though its host ADB process had been interrupted. Debian,
Xorg and Phosh remained healthy. A minimal init-domain helper stopped
SurfaceFlinger again, factory `dumpstate` was restored and rehashed, and the
service stayed stopped. That guard is not installed and will not run on future
boots.

## Scanout correction

The first persistent run proved that Phosh rendered and that Xorg owned
`psbfb`, but the user correctly reported that the physical panel still showed
Android. Reading all 8,355,840 framebuffer bytes and converting the confirmed
4352-byte-stride XRGB8888 image showed the complete Phosh lock screen. Therefore
rendering was correct while Intel's display engine continued scanning out
SurfaceFlinger's last DRM buffer.

The v5 launcher performs the hardware handoff after stopping
SurfaceFlinger:

1. `FBIOBLANK(FB_BLANK_POWERDOWN)`;
2. `FBIOPUT_VSCREENINFO` with `FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE`;
3. `FBIOPAN_DISPLAY`; and
4. `FBIOBLANK(FB_BLANK_UNBLANK)`.

The standalone test and the subsequent automatic v5 boot returned zero for all
four operations. The v5 boot log records:

```text
android-boot-complete-after=17
Phosh ready after 1.26s
surfaceflinger-stop-result=0
scanout-get-var=0
scanout-blank=0
scanout-put-var=0
scanout-pan=0
scanout-unblank=0
fbdev-scanout-result=0
linux-screen-active=1
supervisor-surfaceflinger-stop=0
supervisor-scanout-result=0
```

The v5 evidence directory is
`work/p90-native-port/live-test/2026-08-15-persistent-debian-autoboot-v5/`.
Its framebuffer PNGs are fresh captures of the live Phosh session;
`p90-debian-autoboot-v5.log` is the complete automatic transcript. A deliberate
SurfaceFlinger restart was reclaimed successfully, followed by a sustained
three-and-a-half-minute observation with brightness fixed at 255. The captures
prove the pixels are in `psbfb`; final physical-panel confirmation remains a
separate observation and must not be inferred from the captures alone.

## Remaining boundary

The automatic Linux graphics stack is complete at the framebuffer boundary.
Physical-panel confirmation of v5 is still required. Remaining work is
hardware enablement and reducing the Android shim: system D-Bus and
NetworkManager, Wi-Fi, audio, charging policy, suspend, rotation, modem and
eventually a trusted route for the Android-free initramfs. A newer kernel is a
separate driver-porting project and should not replace this working baseline.

## 2026-08-15 usability update: v17

Launcher v17 supersedes the earlier framebuffer-boundary prototype. Physical
panel operation has now been confirmed by the user and repeatedly reproduced.
The active stack remains genuine Debian amd64 userspace with Phosh and Phoc,
but it still uses Phoc's wlroots X11 backend over Xorg `fbdev` and Lenovo's
signed `3.10.20-x86_64_moor` kernel. It is therefore not yet the final
Android-free power-on replacement.

The physical panel had remained on SurfaceFlinger's final buffer even while
`psbfb` contained correct Phosh pixels. The launcher now maps the Moorefield
display MMIO page through `/dev/mem`, validates the complete primary-plane
layout, and changes only `DSPASURF` from the stale Android GTT offset to zero,
the kernel fbdev surface. Both documented 32-bit formats are accepted:
`0x98000000` (XRGB) and `0x9c000000` (ARGB). The v17 boot recorded a nonzero
surface followed by `mmio-dspasurf-after=0` and
`mmio-fbdev-plane-result=0`.

The major remaining easy performance costs were also removed:

- Xorg `ShadowFB` is disabled and its verbosity is zero;
- Phosh animations are disabled;
- the CPU governor is `performance` while Linux owns the screen;
- `G_MESSAGES_DEBUG=all` was removed, eliminating synchronous per-frame and
  per-input storage writes;
- the cursor is hidden on touch; and
- GTK uses the Cairo software renderer explicitly.

An idle five-second observation showed zero-byte launcher-log growth. This is
still software composition and a full-frame X11/fbdev copy at 1080x1920, so it
cannot match Android's proprietary PowerVR acceleration without a future GPU
driver/kernel port.

### Applications, keyboard, and background

The original blue screens were Phosh launch placeholders for crashed apps,
not blue rendered application windows. GNOME Text Editor failed while building
`EditorOpenPopover`, and Nautilus depended on unavailable system D-Bus,
UDisks, GVFS and Tracker services. Their desktop entries are hidden for the
root session. The offline v2 bundle installs lighter Wayland-capable
alternatives:

- Mousepad for text editing;
- Thunar for local file management;
- Foot for a terminal;
- GNOME Calculator, retained because it already worked; and
- Squeekboard for touch input.

Squeekboard exposed a kernel-age incompatibility: its imported `getrandom`
function returned `ENOSYS` on Linux 3.10 and the process aborted. The narrowly
scoped `p90-getrandom-compat.so` preload implements that one function using
`/dev/urandom`. On v17 Squeekboard remains alive and reports that the bundled
`us_wide` layout loaded. The launcher also supplies a small baseline PPM
gradient and writes both GNOME light and dark background keys from inside the
session D-Bus before Phosh starts; the previous missing Adwaita background
warning is absent.

The v2 offline package directory contains 21 `.deb` files and its package hash
manifest is `work/p90-native-port/offline-packages-v2/SHA256SUMS.packages`.
Resolution is reproducible with
`outputs/p90-native-linux-port/tools/resolve-debian-packages.py`.

### Installed state and recovery

The active startup hook executes `/system/bin/p90-debian-autoboot-v17`.
After installation, `/system` was verified mounted read-only and the factory
`/system/bin/dumpstate` SHA-256 was again exactly:

`1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c`

The ordinary-ADB escape marker is unchanged:
`/data/local/tmp/p90-debian-autoboot-disabled`. Creating it and rebooting gives
maintenance Android; removing it and rebooting starts Linux. No boot, recovery,
GPT, userdata, or raw image was flashed for this update.

Current reproducible sources are:

- `outputs/p90-native-linux-port/tools/android-root-debian-phosh-autoboot.c`;
- `outputs/p90-native-linux-port/tools/android-root-debian-autoboot-installer.c`;
- `outputs/p90-native-linux-port/tools/p90-debian-autoboot-service.sh`;
- `outputs/p90-native-linux-port/tools/p90-getrandom-compat.c`; and
- `outputs/p90-native-linux-port/tools/android-root-p90-mmio-snapshot.c`.

## 2026-08-15 Wayland/PowerVR persistent update: v18

Launcher v18 replaces the Xorg/fbdev v17 display path with the verified
Wayland/PowerVR bridge.  The persistent boot sequence is now:

```text
Lenovo signed 3.10.20 kernel
  -> Android init and SurfaceFlinger hardware shim
  -> flash_recovery root service
  -> /system/bin/p90-debian-autoboot-v18
  -> 32-bit libhybris Wayland server on a SurfaceFlinger PowerVR layer
  -> Debian amd64 Phoc with Wayland output + direct libinput touchscreen
  -> Phosh + Squeekboard
```

The bounded validation first proved that the Synaptics driver delivered the
full multitouch event stream.  wlroots' nested Wayland touch path did not act
on those events, so v18 gives Phoc direct libinput access while the outer
server handles only display presentation and the keyboard-capable parent seat.
The user physically confirmed working touch and animations in this exact
configuration before it was made persistent.

The first automatic v18 boot exposed one reboot-only issue: the bind mount for
the Linux 3.10 `getrandom`/`memfd_create` compatibility library disappeared,
revealing its empty mount-point file.  The final launcher therefore recreates
that bind mount unconditionally on every boot.  The following automatic boot
then succeeded without `file too short`, `getrandom`, or `memfd_create` errors:

```text
persistent=1
boot-prerequisites-after=13
PowerVR ready: PowerVR Rogue Hood
Wayland ready: /data/local/p90-debian/run/user/0/wayland-0
outer-ready=1
first shm frame presented: 1080x1920 stride=4320
phosh-display-window=ready
```

After sustained observation the init child, outer compositor, Phoc, Phosh, and
Squeekboard all remained alive.  Frame counters reached at least 480 with
changing content hashes and no GL upload, draw, or swap errors. SurfaceFlinger
remains alive underneath because it is the stable owner of the proprietary
Moorefield HWC/WSBM stack.

Installed artifacts and hashes:

```text
/system/bin/p90-debian-autoboot-v18
  c206bd89dedf0235297eea13d978397284c60a4de64448bd541d0947c9930b33
/system/etc/install-recovery.sh
  a034ca7e7521985557a3a78f1f53a6b31450ce8769bd7c5bf812c02a5b71d805
/system/bin/dumpstate (factory, unchanged)
  1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c
```

`/system` was verified read-only after installation.  The v17 binary and the
factory `install-recovery.sh.p90-stock` backup remain installed.  The ordinary
ADB recovery marker is unchanged: creating
`/data/local/tmp/p90-debian-autoboot-disabled` and rebooting skips v18 and boots
Android.  The pre-v18 marker was moved, not deleted, and remains as
`p90-debian-autoboot-disabled.saved-before-v18` while Linux autoboot is enabled.

Complete installation and automatic-boot evidence is retained in
`work/p90-native-port/live-test/2026-08-15-persistent-wayland-v18/`.

This is permanent Debian/Phosh startup, but not yet Android-free startup:
Lenovo's signed stock kernel, Android init, Binder, SurfaceFlinger, and the
32-bit PowerVR blobs remain the hardware-enablement layer.  Replacing those
components requires the separate signed-boot and newer-kernel driver project.

## 2026-08-15 Linux-only UI update: v19

Launcher v19 removes the remaining Android application environment from the
running user interface.  It waits until the outer PowerVR Wayland compositor
and Phosh have started, then stops Android's `media` and `zygote` init services.
Stopping `zygote` also removes `system_server` and all Android applications, so
an Android camera or launcher can no longer appear behind Phosh.  The verified
steady state is:

```text
running: stock kernel, Android init, Binder, SurfaceFlinger
running: p90-debian-autoboot-v19, outer Wayland server, Phoc, Phosh, Squeekboard
stopped: zygote, system_server, mediaserver, Android applications
```

This was first tested with the bounded
`android-root-p90-android-ui-control.c` helper.  Its 90-second watchdog restored
both services successfully.  The same cutoff was then integrated into v19.
The persistent launcher restores `zygote` and `media` if Phoc or the outer
compositor exits normally, or if the launcher receives SIGTERM/SIGINT.
Rebooting also restores the services during the next Android init sequence
before v19 starts Linux again.

The first automatic v19 boot reported:

```text
persistent=1
PowerVR ready: PowerVR Rogue Hood
outer-ready=1
first shm frame presented: 1080x1920 stride=4320
phosh-display-window=ready
android-media-stop=0
android-zygote-stop=0
linux-only-ui=1
```

After the cutoff, Android properties reported `zygote=stopped` and
`media=stopped`; neither zygote, system_server, nor mediaserver existed in the
process list.  SurfaceFlinger, Phoc, Phosh, and Squeekboard remained alive.
The PowerVR bridge reached at least frame 480 with changing hashes and
`upload-gl=0x0 draw-gl=0x0`.

The Android-side binaries were reproduced in the existing x86_64 `p90-build`
Lima VM with NDK r16b.  GCC 4.9 was selected explicitly because that archived
NDK's Clang executable requires `libncurses.so.5`, which is absent from the
current VM:

```sh
ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./Android.mk \
  APP_ABI=x86 APP_PLATFORM=android-16 NDK_TOOLCHAIN_VERSION=4.9 \
  p90-wayland-phosh-probe p90-debian-autoboot-installer-v19
```

`p90-wayland-phosh-probe` was installed under the persistent argv0/name
`p90-debian-autoboot-v19`.  The installer copied it and the updated service
script from `/data/local/tmp`, preserved the stock recovery script backup, and
remounted `/system` read-only.  The temporary privileged executable target was
then restored byte-for-byte and verified against its factory hash before the
automatic-boot test.

Installed artifacts and hashes:

```text
/system/bin/p90-debian-autoboot-v19
  9abc132c999d51a63016a5ac732fe7f29d5244d41465275fc0034269463336b6
/system/etc/install-recovery.sh
  0120f6a5c3becc175ba3199d57d2cae937854f4db7af70c4753d8be95cf1a90b
/system/bin/dumpstate (factory, unchanged)
  1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c
```

`/system` was verified read-only after installation.  v18 remains at
`/system/bin/p90-debian-autoboot-v18` as a fallback.  To bypass Linux and keep
Android running, create `/data/local/tmp/p90-debian-autoboot-disabled` and
reboot.  Evidence is retained in
`work/p90-native-port/live-test/2026-08-15-persistent-wayland-v19/`.

This is a genuine Linux-only graphical/userland session, but it is still not a
fully Android-free boot.  The signed Lenovo 3.10 kernel, Android init/Binder,
SurfaceFlinger, and proprietary 32-bit PowerVR userspace remain the minimal
hardware bridge.  SurfaceFlinger cannot yet be stopped because the available
PowerVR EGL/HWC stack loses scanout when it is removed.  A completely native
replacement still requires an accepted kernel boot path plus a DRM/KMS-capable
PowerVR driver for that kernel.

## 2026-08-16 live Settings wallpaper update: v32

Phosh 0.24 keeps its wallpaper in a separate layer, but its shipped home,
overview, and app-grid CSS paints opaque backgrounds over that layer. The
initial P90 workaround therefore painted one fixed image directly on
`phosh-home`; that made the wallpaper visible but prevented GNOME Settings
from changing it.

v32 loads `p90-phosh-wallpaper-live.so` only into the Phosh process. The helper
watches `org.gnome.desktop.background` (`picture-uri` and `picture-uri-dark`)
plus `org.gnome.desktop.interface color-scheme`. It rebuilds the small
`phosh-home` CSS rule whenever one of those values changes and resets the GTK
widgets, so the normal Settings background chooser remains the source of truth
and does not require a Phosh restart. Only local `file:///` URIs without CSS
escape characters are accepted; the installed P90 portrait wallpaper is the
fallback.

Reproducible sources and artifacts:

```text
outputs/p90-native-linux-port/tools/p90-phosh-wallpaper-live.c
work/p90-native-port/build-tools/p90-wayland/p90-phosh-wallpaper-live.so
outputs/p90-native-linux-port/tools/android-root-p90-wayland-phosh-probe.c
outputs/p90-native-linux-port/scripts/install-autoboot-v32-safely.sh
```

The x86_64 helper is built against GTK 3 and GIO:

```sh
gcc -shared -fPIC -O2 -Wall -Wextra -Werror \
  -o p90-phosh-wallpaper-live.so p90-phosh-wallpaper-live.c \
  $(pkg-config --cflags --libs gtk+-3.0 gio-2.0)
```

The Android-side launcher and installer use the same NDK r16b/GCC 4.9 recipe
documented above, with module names `p90-wayland-phosh-probe` and
`p90-debian-autoboot-installer-v32`. The safe installer restores the factory
`dumpstate` bytes and leaves `/system` mounted read-only.

The wallpaper helper must not remain in the environment inherited by desktop
applications. It links GTK 3 because Phosh 0.24 uses GTK 3, while GNOME
Settings 43 uses GTK 4; loading both toolkits into Settings makes GTK abort.
After the dynamic loader maps the helper into Phosh, its constructor therefore
resets `LD_PRELOAD` to the getrandom compatibility shim alone. A bounded launch
test confirmed that `gnome-control-center background` remained alive for eight
seconds with an empty error log, instead of immediately trapping on the
GTK-2/3-plus-GTK-4 check.
