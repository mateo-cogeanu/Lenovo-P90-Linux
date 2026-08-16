# Wi-Fi and speaker audio bring-up: persistent v26

Date: 2026-08-16

## Result

Persistent launcher v26 makes the P90's Broadcom Wi-Fi scan normally and
exposes the Wolfson WM8958 internal speaker as the default PipeWire/Phosh audio
sink.  Both fixes survived a full automatic boot.  The final verification saw
eleven visible access-point entries, including `XGS`, `O2-Internet-923`,
`Vodafone-9168`, `GSN5`, `Mudit1`, and `GSN2`.  PipeWire reported:

```text
Default Sink: alsa_output.p90_speaker
Lenovo P90 Speaker
s16le 2ch 48000Hz
```

A default-target `pw-play` completed with status zero after that boot.

This chapter proves Wi-Fi discovery and internal-speaker playback.  It does
not yet claim working microphone capture, wired-headset switching, telephony
call routing, Bluetooth audio, or modem voice audio.

## Wi-Fi root cause and repair

The kernel interface existed as `wlan0`, used `bcmsdh_sdmmc`, and was not
rfkill-blocked.  NetworkManager nevertheless reported it unavailable.  Kernel
messages revealed repeated failures to open:

```text
/system/etc/firmware/fw_bcmdhd.bin_4339_a0
```

That file exists in Android's `/system`.  The Broadcom kernel driver opens this
absolute path in the filesystem root of the userspace process asking it to
load firmware.  NetworkManager and wpa_supplicant run inside the Debian
chroot, where `/system` had not been mounted, so the same absolute pathname was
missing from their view.

v26 creates `/data/local/p90-debian/system` and bind-mounts Android `/system`
there before starting Debian services.  Android `/system` remains read-only.
The firmware load then succeeds and NetworkManager scans normally.  No
firmware was replaced or modified.

## Audio diagnosis

ALSA exposes two relevant cards:

```text
card 0: IntelHDMI
card 1: wm8958audio
```

The normal media PCM is `hw:1,0`.  Its hardware constraints are fixed at
48 kHz, signed 16-bit little-endian, stereo, with period size 20..12000 and
buffer size 40..48000 frames.  PipeWire originally showed only Dummy Output,
and Lenovo's 857 mixer controls showed the media front ends, codec output,
DAC1 and speaker path muted or disconnected.

Lenovo's original parameter-framework files were retained and used to map the
factory `Normal`/`IHF` route.  The working speaker path is:

```text
Intel SST PCM media input
  -> codec_out0
  -> WM8958 AIF1.1
  -> DAC1
  -> left speaker mixer and boost
  -> internal speaker
```

The persistent launcher applies only the speaker controls needed for this
route.  Codec gain remains at unity, Lenovo's normal mixer/boost values are
used (`3 3` and `7 2`), and the initial codec speaker level is deliberately
limited to `45 45`.  Phosh/PipeWire provides normal software volume control
above that safe hardware baseline.

Direct ALSA validation opened `hw:1,0` successfully with 48 kHz S16_LE stereo,
49-frame periods and a 196-frame buffer.  A static PipeWire adapter is then
created with:

```text
node.name = alsa_output.p90_speaker
api.alsa.path = hw:1,0
audio.format = S16LE
audio.rate = 48000
audio.channels = 2
period-size = 1024
period-num = 4
```

Both WirePlumber roles run inside Phoc's `dbus-run-session`.  The main role
publishes the adapter as the default sink; the policy role links application
streams to it.  On the verified v26 boot, Phosh, GNOME media keys, CallAudio,
both WirePlumber roles, PipeWire and PipeWire Pulse were connected to the same
graph.

## Persistent implementation

The implementation is embedded in:

```text
outputs/p90-native-linux-port/tools/android-root-p90-wayland-phosh-probe.c
```

It performs three new boot-time actions:

1. bind Android `/system` into the Debian chroot for the Broadcom firmware and
   read-only vendor tools;
2. write `/etc/pipewire/pipewire.conf.d/90-p90-speaker.conf` inside Debian;
3. apply the recovered speaker-only mixer route before PipeWire starts.

The reproducible standalone PipeWire configuration and route experiments are
retained under:

```text
work/p90-native-port/package-commands/90-p90-speaker.conf
work/p90-native-port/package-commands/test-v25-speaker-route-host.sh
```

The Android launcher and installer were rebuilt in the existing x86_64 Lima
VM with archived NDK r16b, x86 ABI, Android API 16 and GCC 4.9.  `-std=gnu99`
was supplied because older unrelated probe sources in the same Android.mk use
C99 loop declarations.

Installed artifacts:

```text
/system/bin/p90-debian-autoboot-v26
  a44a792d12e25c908186f7e4c7ef39efa39fa353dc7281e3d414d2bc97fb9bec

/system/etc/install-recovery.sh
  a86c05c2a0dc6e14b8a0c125831cf80c4adb4131fd05c2baca5e0b62508dd38e

/system/bin/dumpstate (factory restored)
  1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c
```

The installer reported successful launcher and hook copies, preserved the
existing stock hook backup, and remounted `/system` read-only.  The temporary
`dumpstate` execution target was restored and pulled after the final test; its
SHA-256 exactly matches the factory binary above.

## Reboot verification and evidence

After the installation-triggered reboot, the following were simultaneously
present:

```text
/system/bin/p90-debian-autoboot-v26
wpa_supplicant
NetworkManager
Phoc and Phosh
PipeWire and PipeWire Pulse
two WirePlumber roles
```

The Debian view showed the Android system partition at `/system`, read-only.
NetworkManager reported `wlan0` disconnected rather than unavailable and
returned real scan results.  PipeWire selected `alsa_output.p90_speaker` as
the default sink and a default-target WAV playback completed without errors.

Raw evidence and installed copies are retained in:

```text
work/p90-native-port/live-test/2026-08-16-wifi-audio-v26/
```

The most useful record is `p90-v26-wifi-audio-verify.log`; it contains the
post-reboot mount, Wi-Fi scan, PipeWire graph, default sink and playback exit
status in one root-verified transcript.

## Rollback and remaining boundary

The existing escape hatch remains unchanged: create
`/data/local/tmp/p90-debian-autoboot-disabled` from ordinary ADB and reboot to
skip the Debian launcher.  The exact stock recovery hook remains backed up as
`/system/etc/install-recovery.sh.p90-stock`.

This is native Debian userland and Phosh using the phone's real kernel Wi-Fi
and ALSA devices, but it is still the compatibility architecture documented in
earlier chapters: Lenovo's signed 3.10 kernel and Android init boot first, and
SurfaceFlinger plus the proprietary PowerVR stack provide scanout.  It is not
yet an Android-free power-on Debian kernel boot.
