# Full Phosh and hardware integration

## 2026-08-15 package expansion

The original rootfs contained Phosh, Phoc, Squeekboard, Calculator, text
editors, and file managers, but not the complete mobile environment.  A
Bookworm offline transaction was resolved against the phone's real dpkg status
in the x86_64 `p90-build` VM.  It installed 702 new packages and upgraded two,
with no removals.  The downloaded set contains 704 debs (about 415 MB).

The requested profile is now represented by these installed packages:

```text
phosh-full phosh-phone phosh-core
gnome-calls chatty gnome-contacts
gnome-calendar gnome-maps gnome-weather gnome-software
gnome-clocks gnome-console gnome-control-center
gnome-backgrounds gnome-icon-theme librsvg2-common
feedbackd network-manager network-manager-gnome modemmanager
pipewire pipewire-pulse wireplumber alsa-utils brightnessctl
mobile-broadband-provider-info
```

`dpkg --configure -a`, the Adwaita icon cache, the desktop database, and GLib
schemas all completed successfully.  The application directory contains 89
desktop entries after installation.  Package-service starts were suppressed
during the offline transaction with `policy-rc.d`; the persistent launcher
starts only the services appropriate to this unusual non-systemd environment.

The reproducible package commands are retained under
`work/p90-native-port/package-commands/`.  The offline package directory is
`work/p90-native-port/offline-packages/phosh-full-bookworm-v1/`.

## v21 full-session boot

Persistent launcher v21 added:

- a fresh system D-Bus socket on every boot;
- NetworkManager and a controlled udev record for the Broadcom WLAN device;
- Debian `wpa_supplicant`;
- UPower and ModemManager;
- PipeWire, PipeWire-Pulse, and WirePlumber;
- feedbackd;
- GNOME power, RF-kill, media-key, sound, and WWAN settings components; and
- the full Phosh application/icon/background databases.

The first v20 test exposed a stale system-bus socket because the rootfs `/run`
directory is stored under `/data`.  v21 explicitly unlinks the old socket and
PID before creating the bus.  The next automatic boot kept Phosh, Squeekboard,
PipeWire, NetworkManager, UPower, and ModemManager alive while Android zygote,
system_server, and mediaserver remained stopped.

Verified hardware state in v21:

```text
UPower battery: bq27441_battery
UPower charger: bq24261_charger
backlight: psb-bl, 0..255, current test value 223
ALSA card 0: IntelHDMI
ALSA card 1: wm8958-audio
WLAN: Broadcom wl/bcmsdh_sdmmc, real MMC sysfs path, managed by NetworkManager
modem devices: gsmtty1..gsmtty63, ttyACM0/1, mdm_ctrl0
```

The battery and brightness backends work.  NetworkManager now owns the real
WLAN device rather than a placeholder, but the device is still `unavailable`:
the upstream Bookworm supplicant cannot yet complete this vendor driver's link
bring-up under the Android SELinux domain.  No Wi-Fi credentials were stored
on the phone, so association has not been tested.

ModemManager runs but has not discovered a modem.  Lenovo's Android `rild` and
`mmgr` own a proprietary Intel XMM interface; the generic ModemManager probe
does not yet turn those ports into a modem.  Calls and Chatty are installed,
but SIM calling/data are not yet functional.

ALSA enumerates all wm8958 playback devices.  PipeWire and PipeWire-Pulse run,
but the first WirePlumber configuration exited because Debian enables its
Bluetooth/logind arbitration plugin and this session has no logind.  Without a
session manager, PipeWire exposes only a dummy sink.

## Prepared v22 keyboard/audio correction

Squeekboard's log showed it selected the compiled `us_wide` layout because no
GNOME input source existed.  v22 sets
`org.gnome.desktop.input-sources sources` to `('xkb', 'us')` before starting
Squeekboard, which selects the normal portrait phone layout.

WirePlumber debugging proved that its logind plugin was the fatal activation,
not an ALSA or PipeWire version mismatch.  v22 writes this local override:

```lua
bluez_monitor.properties["with-logind"] = false
```

It also starts PipeWire first and delays WirePlumber by two seconds.  Bluetooth
audio monitoring stays enabled; only multi-login arbitration is disabled.
The v22 binaries were built successfully but were not installed at the time of
this entry because the phone's USB transport disconnected.

Artifacts:

```text
work/p90-native-port/build-tools/p90-wayland/p90-debian-autoboot-v22
  11f0a41d1e096c48126ee78b24c5f57644fe14590cd74c7227f7897b939adffb
work/p90-native-port/build-tools/p90-wayland/p90-debian-autoboot-installer-v22
  f895301f1e487d3c9e64ec9fb771d6462213425ef5b9453abe3d85111653512e
```

v21 remains installed and bootable.  v18, v19, and v20 are preserved as
fallback binaries.  Creating `/data/local/tmp/p90-debian-autoboot-disabled`
and rebooting continues to bypass Linux autoboot entirely.
