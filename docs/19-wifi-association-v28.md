# Wi-Fi association and internet access: persistent v28

Date: 2026-08-16

## Result

Wi-Fi association, DHCP, routing, DNS and internet traffic now work and survive
reboot.  Persistent v28 automatically connected to `XGS`, obtained
`192.168.111.64/24`, installed gateway and DNS `192.168.111.1`, started Phosh,
and reached `1.1.1.1` with zero packet loss.

Chapter 18 proved firmware loading and scanning.  It did not prove association;
the first user connection attempt exposed two additional compatibility issues
documented and fixed here.

## Failure 1: WPA3 selected on unsupported firmware

`XGS` advertises WPA2/WPA3 transition mode.  GNOME/NetworkManager initially
saved the connection as:

```text
802-11-wireless-security.key-mgmt: sae
```

The modern Debian wpa_supplicant understands SAE, but the P90's December 2014
Broadcom firmware/driver cannot ready this interface for WPA3 SAE.  Activation
failed before authentication with:

```text
The device could not be readied for configuration
```

The existing profile was changed to `wpa-psk`, retaining the password already
entered by the user.  This selects WPA2 on the transition-mode access point;
no password was read into the host-side logs or documentation.

## Failure 2: first NetworkManager instance wedges preparation

Even with WPA2 selected, the NetworkManager instance started alongside the
legacy Broadcom supplicant remained unable to prepare `wlan0`.  Its state
eventually said only that the supplicant had become available, but it did not
retry the saved profile successfully.

A controlled debug run established the reliable sequence:

1. allow the first daemon to initialize the old `wl`/nl80211/supplicant path;
2. stop that NetworkManager instance;
3. start a clean normal NetworkManager instance;
4. after it sees the supplicant, explicitly activate the saved WPA2 profile.

The second instance associated immediately.  DHCP received an offer and ACK
from `192.168.111.1`, installed address `192.168.111.64`, and selected `XGS` as
the default IPv4 route and DNS connection.  NetworkManager's connectivity test
then reported `full`.

## Persistent v28 implementation

`system_services` in
`outputs/p90-native-linux-port/tools/android-root-p90-wayland-phosh-probe.c`
now performs the verified one-time initialization sequence.  It starts the
first daemon after the firmware delay, restarts NetworkManager once, and then
raises the existing `XGS` profile.  The activation is bounded and runs in the
background so a missing access point cannot block Phosh startup.

The WPA2 selection is stored in NetworkManager's persistent keyfile for the
saved `XGS` connection.  Other WPA2-only networks can still be added normally.
WPA2/WPA3 transition networks may need their new profile forced to `wpa-psk`
on this legacy firmware as well.

Installed artifacts:

```text
/system/bin/p90-debian-autoboot-v28
  04329752c3dcf8578529ba5675d4a2f394679643d2258a6189f937942ea0d3f6

/system/etc/install-recovery.sh
  7088c3eef433a437b1debca72e070d8aafb3e6e219c1a14bdc98307d9ce44efc

/system/bin/dumpstate (factory restored)
  1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c
```

The installer preserved the stock hook backup and reported successful launcher
and hook installation.  `/system` was verified read-only after restoration.

## Reboot proof

The post-reboot record contains all of the following simultaneously:

```text
sys.boot_completed = 1
/system/bin/p90-debian-autoboot-v28
wpa_supplicant
NetworkManager
/usr/libexec/phosh
inet 192.168.111.64/24 on wlan0
default via 192.168.111.1 dev wlan0
2/2 replies from 1.1.1.1
```

Evidence is retained in:

```text
work/p90-native-port/live-test/2026-08-16-wifi-association-v28/
```

`p90-v28-post-reboot-wifi.txt` is the concise final proof.  The earlier files
retain the failed SAE attempt, the preparation failure, successful debug
association, normal-daemon validation, installer outcome, and installed
artifacts.
