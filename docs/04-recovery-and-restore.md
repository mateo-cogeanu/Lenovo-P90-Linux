# Recovery and restore notes

## Validate before use

Run SHA-256 from the raw backup directory:

```sh
cd outputs/p90-backup/raw-partitions/p90-root-backup
shasum -a 256 -c ../../../p90-native-linux-port/manifests/BACKUP-SHA256SUMS.txt
```

Validate userdata without modifying the retained archive:

```sh
gzip -t mmcblk0p9-online.ext4.simg.gz
gzip -dc mmcblk0p9-online.ext4.simg.gz > /safe/temporary/p9.simg
simg2img /safe/temporary/p9.simg /safe/temporary/p9.raw.img
e2fsck -fn /safe/temporary/p9.raw.img
```

Journal/orphan and small bitmap warnings are expected because p9 was captured
online. Never run a repairing fsck against the only retained copy.

## Partition restoration guardrails

The backup maps p1 through p9 exactly by GPT name. Restoration with `dd` or
fastboot is intentionally not scripted: a one-character target mistake can
destroy the bootloader, radio calibration or userdata. Before any restore:

1. Boot a known root rescue system without writing eMMC.
2. Re-read GPT and compare disk GUID, starts, ends and names with the preserved map.
3. Re-hash the source image.
4. Restore only one explicitly identified partition at a time.
5. Read it back and compare the complete hash before rebooting.

Do not write `mmcblk0boot0`, `mmcblk0boot1`, p1, `factory`, `config` or `misc`
as an experiment. Keep the original p1 image permanently: it contains the only
known-installed Lenovo bootstub/kernel combination.

## Proven S149 logical-image recovery

The exact signed S149 factory package provides a safer logical restore path than
raw p1 writes. On this handset, Droidboot is reached from full power-off with
**Power + Volume Down**. Use the legacy client and Intel vendor ID:

```sh
fastboot -i 0x8087 devices
fastboot -i 0x8087 flash recovery recovery.img
fastboot -i 0x8087 flash boot boot.img
```

Only use files whose hashes match the artifact manifest. The live test proved
the matching signed recovery and boot writes. After an unsigned recovery test,
the phone repeatedly entered Lenovo Recovery in `fota` mode even though no
`/cache/recovery/command` was queued. Rewriting only the exact signed S149
`boot.img`, then choosing plain **Reboot** in Droidboot, restored Android boot.
Do not select either `fuse wipe data` or `fuse format data` in Lenovo Recovery.

On this unit, host-issued `fastboot reboot`, `adb reboot` from recovery and
`adb reboot bootloader` often behaved like power-off. A 10–20 second Power hold
started the phone again. Use Power + Volume Down when Droidboot is required.

## Current installation blocker

Intel Droidboot does not implement the standard unlock workflow and did not
execute temporary boot transfers. Raw preservation makes future work safer, but
does not make blind flashing safe. Debian, the vendor rescue image and the 6.18
test kernel remain host-side artifacts until a reversible boot method is found.
