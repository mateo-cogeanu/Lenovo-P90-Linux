# Console and on-screen keyboard repair: persistent v29

Date: 2026-08-16

## Result

The Console icon now launches `foot` 1.13.1, a native Wayland terminal, instead
of GNOME Console (`kgx`) 43.0 / VTE 0.70.6.  The replacement was tested on the
phone, installed into the Debian rootfs, embedded in persistent launcher v29,
and verified after a full reboot.  Phosh, Phoc, Squeekboard and NetworkManager
all returned under v29, and Android Zygote/media were stopped for the Linux-only
UI handoff.

## Diagnosis

The reported failure had two visible parts: Console froze the phone, and the
keyboard did not appear.  The frozen `kgx` process consumed about 149 MiB RSS
and was killed without restarting the desktop.  The underlying terminal stack
was then checked:

- `/dev/pts` was mounted read/write and allocating a PTY succeeded.
- Phosh had the correct `WAYLAND_DISPLAY=wayland-1`, `XDG_SESSION_TYPE=wayland`
  and D-Bus session address.
- `screen-keyboard-enabled` was true.
- Squeekboard was alive.  Its log had previously loaded
  `terminal/us_wide`, proving that a working terminal focus request reaches the
  OSK.
- The stock Console desktop entry used `Exec=kgx` and
  `DBusActivatable=true`.

This ruled out PTY allocation and the physical touchscreen.  The freeze was in
the KGX/VTE application/rendering path on this old Atom/PowerVR userspace.  The
missing keyboard was a consequence: once KGX wedged before obtaining usable
text focus, Squeekboard had no pending text-input request.

## Guarded hardware test

The already-installed `foot` binary was launched in the live Phosh session with
a device-side 12-second timeout.  The timeout guaranteed that a bad test could
not trap the user in another full-screen application.  Foot reported:

```text
version: 1.13.1 +pgo +ime +graphemes -assertions
arch: Linux x86_64/64-bit
WL-1: 540x960
using 4 rendering threads
```

It remained responsive until the intentional timeout.  During that same test,
Squeekboard logged `Loaded layout Resource: terminal/us_wide`.  The warning
about sealing its shared-memory file is a limitation of the 3.10 kernel but did
not prevent display, input, or orderly shutdown.

## Persistent implementation

Launcher source:

```text
outputs/p90-native-linux-port/tools/android-root-p90-wayland-phosh-probe.c
```

At each boot, v29 writes:

```text
/usr/local/bin/p90-terminal
/usr/local/share/applications/org.gnome.Console.desktop
```

The wrapper executes:

```sh
exec /usr/bin/foot --font='Noto Sans Mono:size=11' "$@"
```

The override deliberately retains the familiar `Console` name and icon, but
uses `DBusActivatable=false` and starts the wrapper directly.  This prevents
the old desktop entry from activating KGX over D-Bus.  `/usr/local/share`
overrides the package-owned entry without deleting or modifying Debian's KGX
package, so rollback remains simple.

The Android launcher and installer were rebuilt with NDK r16b for Android 16,
x86.  Installed artifacts:

```text
/system/bin/p90-debian-autoboot-v29
/system/etc/install-recovery.sh
```

SHA-256:

```text
eab6787525550d246493ccf6d14beeb5be453f210af8bcf0bdd55ba6f7135214  p90-debian-autoboot-v29
bbd388e8d861b8104cccd26b8d131092fb2de6dc5dd3b5bb2e957ef26f6c7526  p90-debian-autoboot-installer-v29
6f1ce9d7248c8156bbafcb4b469dfb80b572604a1ba48c615c7ff87096191b79  install-recovery.sh
```

After installation, Lenovo's temporary service binary was restored to its
exact factory SHA-256:

```text
1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c  /system/bin/dumpstate
```

`/system` was verified mounted read-only.  After reboot, the v29 process,
PowerVR display bridge, Phoc, Phosh, Squeekboard and NetworkManager were all
present, and the Console override was recreated byte-for-byte.

## Reproduction and evidence

The safe root-command runner, including mandatory factory restoration and hash
verification, is:

```text
outputs/p90-native-linux-port/scripts/run-root-command-safely.sh
```

The retained test evidence is under:

```text
work/p90-native-port/live-test/2026-08-16-console-foot-v29/
```

`foot-guarded.log` records the Wayland terminal test;
`squeekboard.log` records the terminal keyboard layout request;
`p90-v29-autoboot.txt` records the post-reboot persistent startup; and the
installed wrapper and desktop override are retained separately.

## Rollback

Remove the local desktop override and wrapper from the Debian rootfs, or change
the desktop entry back to `Exec=kgx` and `DBusActivatable=true`.  No Android
application, userdata partition layout, boot image, recovery image, or kernel
was changed by this repair.
