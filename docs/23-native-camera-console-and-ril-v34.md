# Native camera, Console, and RIL integration: v34

Date: 2026-08-16

## Verified results

The factory Intel camera pipeline now produces a real JPEG without an Android
camera application, Zygote, `system_server`, or `mediaserver`.  A source-built
32-bit Bionic client opens Lenovo's camera1 HAL, supplies the preview window it
requires through a hidden SurfaceFlinger surface, and saves the compressed
callback.  The guarded rear-camera test returned:

```text
camera-count=2
camera-open result=0
preview-window result=0
preview-start result=0 enabled=1
take-picture result=0
jpeg index=0 bytes=109816 result=0
complete jpeg-frames=1
```

The retained output is a valid baseline JPEG with EXIF metadata and dimensions
640x480:

```text
work/p90-native-port/live-test/2026-08-16-camera-hal-surface/rear-camera.jpg
SHA-256: a65adc60ad580ba568a6e60659285c5d0f7d24fad28a378b50b3267bc18be1a2
```

The test ended with Zygote and media stopped, the temporary standalone
`sensorservice` gone, `/system` read-only, and the exact factory `dumpstate`
restored.

GNOME Console 43.0 was also retested after correcting the inherited preload
environment.  `kgx` remained alive and responsive for the complete guarded
14-second Wayland run at about 33 MiB RSS.  v34 therefore restores the Console
icon to direct KGX startup; Foot remains installed as a fallback.

Persistent v34 was installed and reboot-verified on the phone.  The booted
launcher created its root-owned camera FIFO worker, stopped Android Zygote and
media, and kept Phosh, Phoc, oFono, and SurfaceFlinger running.  A request sent
through the installed FIFO returned status 0 and produced this second valid
640x480 JPEG:

```text
work/p90-native-port/live-test/2026-08-16-v34-persistent/camera-worker.jpg
SHA-256: cb7bcb0657dc087d416f6d0665878db81149687410f3071f0f095e300f3fe57a
```

The installed portrait GTK camera UI then survived a guarded 10-second native
Wayland launch at about 34 MiB RSS.  The rebooted-session KGX retest also
survived its complete 14-second guard.  After all tests, the temporary camera
sensor service was absent, `/system` was read-only, and stock `dumpstate` was
restored byte-for-byte.

Debian oFono 1.31 connects to Lenovo's existing XMM7260 Android RIL socket and
publishes `/ril_0`, including `SimManager`, `CallVolume`, and
`VoiceCallManager`.  The modem reported its real baseband version.  It also
reported `card_state=0` and zero SIM applications, so calls, SMS, data, and
call audio are deliberately not claimed as verified.

## Camera architecture

The first CameraService client reached `media.camera` but blocked at the
standard Android camera-permission check because the Linux UI intentionally
keeps `system_server` absent.  Restarting the complete Android framework would
violate the Linux-only UI goal, so that route was discarded.

The working path is:

```text
Phosh Camera app
  -> /run/p90-camera-request (validated FIFO request)
  -> root-owned v34 camera worker
  -> standalone factory sensorservice
  -> factory camera.mofd_v1 HAL
  -> hidden SurfaceFlinger preview surface
  -> JPEG callback in /root/Pictures/P90-Camera
```

The worker accepts only simple `.jpg` names below the fixed P90 Camera photo
directory.  It starts `sensorservice` only for the bounded capture, stops it
afterward, records a result file, and never starts an Android application or
the media service.  The GTK3 app provides a portrait capture button and shows
the newest photo.

## Source and reproduction

- HAL capture client:
  `outputs/p90-native-linux-port/tools/bionic-camera-hal-surface-jpeg-capture.cpp`
- Build script:
  `outputs/p90-native-linux-port/scripts/build-bionic-camera-hal-surface-client.sh`
- GTK camera app:
  `outputs/p90-native-linux-port/assets/p90-camera-app`
- Persistent worker and desktop wrappers:
  `outputs/p90-native-linux-port/tools/android-root-p90-wayland-phosh-probe.c`
- Guarded hardware test:
  `work/p90-native-port/diagnostics/test-camera-hal-surface-jpeg-host.sh`
- v34 installer:
  `outputs/p90-native-linux-port/scripts/install-autoboot-v34-safely.sh`

The camera client uses official AOSP `frameworks/av` tag
`android-4.4.4_r2.0.1`, commit
`66809dc49bec6cb6345c74d48d319c2a63c17ac6`, the already-retained official
KitKat `frameworks/native` source, and exact P90 system libraries as link-only
inputs.

Artifact hashes:

```text
87331d8c88968734f8c61ce970fd030af57b62d4e599b49495931d391181d2ac  p90-bionic-camera-hal-surface-jpeg-capture
1a0deaca6b56e9ec90c8ca8fc429d9d6d3cb0e8aa6e4283ad15b6d3bcdab45c8  p90-camera-app
8c4d41e0dade2d05b8049ce8a01a7eb09b5f8ad45df1420fd61b076e246a0c53  p90-debian-autoboot-v34
fa96b9122f24586b3295f7ad72cf17c53a7dad801bbc1c4d06276549c7148b6b  p90-debian-autoboot-installer-v34
```

## Scope still not verified

Only rear-camera still capture is proven.  Front-camera capture, live preview
inside the GTK window, video recording, flash timing, autofocus behavior, and
full-resolution capture remain separate tests.  They are not labelled broken
or fixed.  SIM-dependent telephony remains unverified until a SIM is detected.
