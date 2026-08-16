# Mobile hardware integration: persistent v30

Date: 2026-08-16

## Outcome

Persistent v30 adds a working PipeWire microphone, installs the Megapixels
camera application and V4L2 utilities, and forces a tall portrait keyboard
layout.  Camera sensor enumeration succeeds but frame capture does not; SIM
service remains unavailable to native ModemManager.  The camera and SIM limits
are recorded as tested blockers rather than claimed as working features.

## Working microphone

The kernel exposes several WM8958/Saltbay capture PCM devices.  PCM device 0
opened but initially returned no samples because the Intel SST graph was routed
from the analog codec input.  Lenovo's parameter-framework XML identifies the
built-in microphones as the digital `codec_in0` path and the regular record
path as `pcm_record/mic_1_2`.

The proven route is:

```text
PM4 DMIC1L ENA = On
PM4 DMIC1R ENA = On
AIF1ADC1L Mixer ADC/DMIC Switch = On
AIF1ADC1R Mixer ADC/DMIC Switch = On
codec_in0 gain 0 mute = Off
pcm0_out mix 0 codec_in0 = On
pcm1_out mix 0 codec_in0 = On
pcm1_out gain 0 mute = Off
```

A raw three-second ALSA control recorded 288,000 samples at 48 kHz stereo.  Its
mean level was -24.9 dB and peak was -9.6 dB.

v30 applies that route before starting PipeWire and adds an
`api.alsa.pcm.source` adapter named:

```text
alsa_input.p90_microphone
Lenovo P90 Microphone
```

The post-reboot application-level test used the Pulse compatibility endpoint,
not direct ALSA.  `pactl` listed the source, and `parec` recorded 131,072
samples with a -21.8 dB mean and -9.2 dB peak.  This proves applications can
open the microphone through the normal desktop audio service.

## Portrait keyboard

Squeekboard 1.21 selected its compiled `us_wide` resource.  That resource uses
42-pixel-high keys and looks like a short landscape keyboard even though the
display is 540x960 portrait.  The normal `us` resource uses 52-pixel-high keys.

The source-controlled portrait layout is:

```text
outputs/p90-native-linux-port/assets/us_wide-portrait.yaml
```

It is installed at:

```text
/usr/local/share/p90/us_wide-portrait.yaml
/root/.local/share/squeekboard/keyboards/us_wide.yaml
```

v30 copies the canonical file into the user override before starting the OSK.
After reboot, both files had SHA-256
`c4ecd20058177d8068f7cdc54b4b64ee88e95ebade853e4013746596df23736d`,
and Squeekboard logged:

```text
Loaded layout Path: "/root/.local/share/squeekboard/keyboards/us_wide.yaml"
```

Squeekboard remains the small Wayland text-input service because it is what
Phosh automatically opens and closes for focused fields.  Two actual engine
replacements were evaluated:

- Maliit Keyboard requires 38 additional Qt packages (62,770,332 compressed
  bytes) on this already resource-constrained phone.
- `wvkbd` is only 42,772 bytes and supports separate portrait/landscape
  heights, but its own documentation says responding to Wayland text fields is
  not implemented.  It must be shown and hidden by Unix signals.

The installed override therefore changes the visible keyboard into the proper
portrait layout without losing automatic text-field integration or adding the
large Qt runtime.

## Camera application and AtomISP blocker

The exact Debian Bookworm packages installed were:

```text
megapixels 1.6.0-1+b1
v4l-utils 1.22.1-5+b2
libraw-bin 0.20.2-2.1+deb12u1
libraw20 0.20.2-2.1+deb12u1
libv4l2rds0 1.22.1-5+b2
```

Megapixels now has an app-grid desktop entry.  The packages and dependency
manifest are retained under
`work/p90-native-port/offline-packages/mobile-hardware-v30/`.

The media controller identifies:

```text
driver: atomisp-css2401
model: Intel Atom ISP
front sensor: ov5693 4-0036
rear sensor: imx214 4-001a
flash: lm3646 4-0067
```

Nine V4L2 nodes enumerate YUV420, NV12, YUYV, UYVY, RGB and Bayer formats.
Both sensors are selectable on the AtomISP capture/preview/video nodes.

Frame capture is not working.  The media graph starts with the ISP pipeline
links disabled; a normal `VIDIOC_S_FMT` returns `EINVAL`, followed by
`VIDIOC_REQBUFS` failure.  Attempting to enable the OV5693 -> CSI2 -> AtomISP
preview links caused the legacy driver to reset USB before returning any frame.
The factory camera stack uses Lenovo's proprietary CSS2400 Android HAL and
camera-profile programming around these same nodes.  No unstable media graph
is applied at boot, and no unverified Megapixels configuration is installed.

Thus the camera app is installed, but opening it cannot yet produce a picture.
A functional camera needs either a userspace bridge to Lenovo's camera HAL or a
reimplementation of its AtomISP/CSS2400 pipeline sequence.

## SIM and modem result

The phone exposes:

```text
/dev/ttyACM0
/dev/ttyACM1
/dev/gsmtty1 ... /dev/gsmtty63
/dev/mdm_ctrl0
USB vendor/product 1519:0452
```

Lenovo `mmgr` and `rild` normally own the proprietary Intel XMM stack.  A
guarded test stopped both services, started ModemManager 1.20.4, and restored
Lenovo's services after 30 seconds through an independent host watchdog.
ModemManager loaded its XMM helper and inspected the ACM and CDC-NCM functions,
but classified the ports as non-candidates and created no modem object.

The final state has `mmgr` and `ril-daemon` running again.  Native Phosh calls,
SMS and mobile data are not functional.  Stopping Lenovo's services
permanently would only remove cellular standby without making ModemManager work,
so v30 deliberately does not change modem ownership.

## Persistent artifacts

```text
/system/bin/p90-debian-autoboot-v30
  aa8d6f949475dd3986075350ea90d90e10df5eb5bfea922443eb605dd92915fc

p90-debian-autoboot-installer-v30
  12096c7a35857139cc06f25256d97758cf71e3d29f22a3d071307e7abfc863c2
```

The launcher source remains
`tools/android-root-p90-wayland-phosh-probe.c`.  After installation and reboot,
`/system` was read-only and Lenovo's factory `dumpstate` SHA-256 remained:

```text
1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c
```

No boot image, recovery image, kernel, partition table or Android application
was modified in this work.

## Upstream package references

- [Debian Megapixels 1.6.0](https://packages.debian.org/bookworm/megapixels)
- [Debian v4l-utils](https://packages.debian.org/bookworm/v4l-utils)
- [Debian Squeekboard 1.21 source layouts](https://sources.debian.org/src/squeekboard/1.21.0-1/data/keyboards/)
- [Debian Maliit Keyboard](https://packages.debian.org/bookworm/maliit-keyboard)
- [Debian wvkbd](https://packages.debian.org/bookworm/wvkbd)
