# Live bootloader test log

## 2026-08-03: matching stock package

Downloaded `Lenovo_P90_S149_160504.zip`, matching the installed
`P90_S149_160504_ROW` build. The archive SHA-256 is
`a976c36c73613c1d148cce70bf10912497697aa5d31eb8958b4091d0daa0a83e`;
`unzip -t` passed for every member.

The factory images are Intel OSIP containers, not ordinary Android boot images.
Their first sector contains `$OS$`, an OSII descriptor, load address
`0x01100000`, entry point `0x01101000`, a block count and `0x55aa`. Signed images
carry a `$MOSS` verification record before the stitched command-line, bootstub,
kernel and initrd payload. Lenovo's included `upgrade.bat` uses:

```text
fastboot flash boot boot.img
fastboot flash fastboot droidboot.img
fastboot flash recovery recovery.img
```

This explains why raw p1 is 256 MiB and contains multiple installed images:
Droidboot parses the supplied OSIP file and updates the selected logical slot.
A raw stitched image must never be written over p1.

## Unsigned rescue OSIP

Added `scripts/make_intel_osip.py` and `scripts/wrap-intel-osip-image.sh`. They
wrap the existing vendor rescue payload in Lenovo-compatible unsigned OSIP
metadata (`image_attributes=1`). Output:

- `work/p90-native-port/artifacts/vendor-stock/p90-vendor-root-adb-osip.img`
- SHA-256: `81ac6971457baabd1eb3a334b3a9e9936476deaec802e3a5d5030de0461d9a5c`

The vendor `gen_os` source confirms that signed production images require the
private Intel/Lenovo signing utility and certificate; these are not in the GPL
release.

The legacy client's `fastboot boot FILE` command treated `FILE` as a kernel and
wrapped it in a standard Android boot image. Droidboot accepted the transfer but
remained in its menu, so this did not test raw OSIP execution.

## Signed recovery write test

Battery was 92%. With the phone in Droidboot, ran:

```text
fastboot -i 0x8087 flash recovery recovery.img
```

using the matching signed S149 recovery, SHA-256
`6d3688fd6e87e3d937adc9f7f1c95d5fa4ab898270724c0219421d70145eeb1d`.
Fastboot reported successful transfer and write. No other logical image or GPT
partition was modified.

`fastboot reboot` then completed from the host's perspective and powered the
phone down. After the user held Power, Android booted normally. This established
that the matching signed recovery can be written and that Android remained
bootable.

## Unsigned recovery-slot execution test

After Android health and a 93% battery level were verified, the phone was
returned to Droidboot. The only custom-image write performed was:

```text
fastboot -i 0x8087 flash recovery p90-vendor-root-adb-osip.img
```

Droidboot reported successful transfer and write of the unsigned OSIP-wrapped
vendor-kernel/root-ADB rescue. After reboot and a physical power-on, the phone
returned to Droidboot instead of starting the rescue, and no ADB device
appeared. The most likely explanation is that Droidboot accepted the container
for storage but the verified-boot path rejected it at execution because it
lacks Lenovo/Intel's `$MOSS` signature. This is an inference from the observed
behavior, not a lock-state value reported by Droidboot.

The exact signed S149 recovery was immediately flashed back to `recovery`.
Droidboot again reported successful transfer and write. A subsequent read-only
`getvar secure` returned an empty value, so this old implementation does not
provide a useful secure/unlocked status through that variable. `fastboot
continue` was then accepted, but entered the restored Lenovo recovery rather
than Android.

Recovery ADB enumerated as `MedfieldE0F225F9 recovery`. Its log identified the
exact S149 build and reported `ro.boot.mode=fota` / `ro.bootmode=fota`, explaining
why the continue path selected recovery. `/cache/recovery/command` was absent,
and the recovery log contained no queued update, wipe or factory-reset action.
A plain `adb reboot` was issued from recovery; like `fastboot reboot` on this
device, it disconnected and appeared to power the phone down rather than
immediately restarting it, requiring a physical Power-button start.

Repeated plain reboots from both Lenovo Recovery and Droidboot continued to
enter recovery. Power + Volume Down was confirmed as the physical Droidboot
combination on this handset. The exact signed S149 stock boot image was then
rehash-verified and written:

```text
fastboot -i 0x8087 flash boot boot.img
```

Source SHA-256:
`0bc13d50f88c1f32297e932c3aff63c013c52dcc214255a5c5192b0d25c6646b`.
Droidboot reported successful transfer and write. Choosing its plain **Reboot**
entry then restored Android. Post-boot verification reported:

- serial `MedfieldE0F225F9`, ADB state `device`
- build `P90_S149_160504_ROW`
- boot mode `main`, `sys.boot_completed=1`
- battery 100%, AC powered
- `/data` 657.7 MiB used and `/cache` 2.3 MiB used
- `run-as` SHA-256 `9e003e80d58c8143105f84ebb2e797bea715fce07cb5b116780a4d7f94f9f67a`
- `dumpstate` SHA-256 `1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c`

The two executable hashes exactly match their original preserved values. No
custom `boot`, `fastboot`, `system` or `userdata` image was written; the only
boot-slot write was the exact signed stock restore. The test demonstrates that
successful `flash` output is not proof that an unsigned image is executable on
this device. Do not risk the Android boot slot with an unsigned image unless a
genuine unlock, signature bypass or recoverable manufacturing boot path is
found first.
