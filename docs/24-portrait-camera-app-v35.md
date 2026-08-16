# Portrait camera application: v35

Date: 2026-08-16

## Result

The v34 proof-of-hardware button was replaced by a portrait GTK3 camera and
in-app gallery.  The installed app now provides:

- a large phone-style shutter control;
- capture progress and elapsed-time feedback;
- immediate errors when the root camera worker is unavailable;
- a bounded 35-second hardware timeout instead of a permanently blocked FIFO;
- automatic display of the newly saved image;
- previous/next controls for photos in `Pictures/P90-Camera`;
- photo count, filename, and saved-state feedback;
- collision-resistant filenames independent of the P90's incorrect RTC date.

This remains a still-camera application.  Its central image area displays the
latest capture and acts as an in-app gallery; live HAL preview is not yet routed
into the Wayland client.

## Root cause of the stuck shutter

The original Python worker opened `/run/p90-camera-request` in normal blocking
mode.  A FIFO can continue to exist after its reader exits, so the app could
remain at `Capturing...` forever while opening the request.  v35 opens it with
`O_NONBLOCK`, reports a missing reader immediately, and retains a separate
bounded result timeout.

## End-to-end verification

The installed application was run in the real Phosh Wayland session with its
test mode invoking the same GTK shutter callback as a tap.  It exited normally
and reported:

```text
camera-autotest-photo=/root/Pictures/P90-Camera/P90-1389741959457744475-3160.jpg
```

The retained result is a valid EXIF baseline JPEG, 640x480 and 112,994 bytes:

```text
work/p90-native-port/live-test/2026-08-16-camera-app-v35/ui-shutter-capture.jpg
SHA-256: 44553092e453972c89e221005a300edd01a674e3c4bfbb352c04b6f632e24e78
```

Installed application/source SHA-256:

```text
a8f8764adfeb9dacef529cef0167376b584fbba9c1ecaadff2f38b89eea59199  p90-camera-app
```

Source: `outputs/p90-native-linux-port/assets/p90-camera-app`
