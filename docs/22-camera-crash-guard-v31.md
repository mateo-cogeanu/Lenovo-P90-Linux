# Megapixels crash guard: persistent v31

## Failure reproduced

Launching Debian Bookworm's Megapixels 1.6.0 in the real Phosh session exited
with status 1 and printed:

```text
Could not find any config file
Could not read device name from device tree
```

This is expected on the Lenovo P90's x86/ACPI kernel.  Megapixels 1.6 selects
its hardware profile from `/proc/device-tree/compatible`, with
`/etc/megapixels.ini` as a fallback.  This phone has neither a device-tree
compatible string nor a verified AtomISP/CSS2400 profile.

## Why a guessed profile was not installed

The media controller exposes AtomISP, OV5693 and IMX214 entities, but every
capture node starts at a 0x0 empty format.  Earlier generic-link activation
reset USB without returning a frame.  A later read-only legacy-subdevice query
also wedged long enough to force USB re-enumeration.  Megapixels normally
changes media links and formats during startup, so an invented INI file could
turn a simple application exit into a phone-wide camera-driver hang.

The functional camera blocker remains Lenovo's proprietary Intel CSS2400
pipeline programming.  A real implementation still needs a Lenovo HAL bridge
or a verified reimplementation of that sequence.

## Safe app-grid behavior

Persistent v31 retains the packaged Megapixels binary for future development,
but overrides only its app-grid desktop entry.  `/usr/local/bin/p90-camera`
now sends a Phosh notification explaining that native capture is not ready. It
does not open a V4L2 or media-controller node.  This prevents the visible
"crash" and, more importantly, prevents unverified camera graph programming.

The wrapper was tested against the actual Phosh session bus. `gdbus` returned
notification ID 1 before installation and again after a complete v31 reboot:

```text
(uint32 1,)
```

## Persistent artifacts

```text
/system/bin/p90-debian-autoboot-v31
  5e4547dc3b4a0dad25b424bce256d6e056e591dd9525bf9f289b9014f2326a83

p90-debian-autoboot-installer-v31
  ec1713e014d39be97e39c1fe0a48cdffb31c055a757b775400dfd10fd053189b

/system/etc/install-recovery.sh
  2e4eb768026f308fe949af4e4f0552119746d8d42ff180d8a3fae21c37f74d01
```

The installer completed its launcher and hook copies before the legacy USB
path reset.  After re-enumeration, the phone booted through v31, `/system` was
read-only, Phoc and Phosh were alive, and Lenovo's factory `dumpstate` was
restored and verified at:

```text
1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c
```

No boot image, recovery image, kernel, partition table or camera media link was
written in this repair.

## Upstream reference

- [Megapixels 1.6.0 source tag](https://gitlab.com/postmarketOS/megapixels/-/tree/1.6.0)
