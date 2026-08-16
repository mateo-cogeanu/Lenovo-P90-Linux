# Fresh-panic exact-stock kexec control

## Purpose

Earlier RAM-only tests returned through the watchdog, but Lenovo's persistent
panic record predated those tests. This control removed that ambiguity by
clearing the archived record before launching the exact S149 factory kernel and
factory ramdisk.

No boot, recovery, GPT, system, userdata or raw partition image was flashed.
The kernel and ramdisk were loaded only into RAM.

## Preservation gates

Before the test:

- Android reported `sys.boot_completed=1`, `ro.boot.mode=main` and battery 100%.
- The phone was connected to AC power.
- The complete previous ipanic record was already archived and was captured
  again under `work/p90-native-port/live-test/2026-08-03-fresh-ipanic-stock-control/before/`.
- Factory `dumpstate` matched SHA-256
  `1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c`.
- The exact S149 bzImage and ramdisk matched their retained hashes.

The root helper cleared the old `/proc/emmc_ipanic_header` record, waited for
the driver's erase work, and loaded the exact S149 pair with the running command
line plus:

```text
console=logk0 loglevel=8 no_console_suspend ignore_loglevel panic=10 p90_kexec_test=fresh_ipanic_stock_s149
```

It then replaced itself with the stock Android shell and waited. The complete
factory `dumpstate` was restored and pulled before the trigger. `cmp` succeeded
and the factory SHA-256 matched exactly.

## Execution result

The trigger was issued at `2026-08-03T16:46:03Z`. ADB disconnected immediately.
The signed Android system reappeared around 104 seconds later and completed boot
around 121 seconds after the trigger. It reported:

```text
ro.boot.bootreason=watchdog
ro.boot.mode=main
ro.build.display.id=P90_S149_160504_ROW
```

The helper status persisted across the reset and proves all intended gates ran:

```text
ipanic clear write=2 errno=0 (ok)
load exit=0
watchdog_disable_rc=0
1
go=1 syncing and executing
```

## New evidence

After Android returned, these entries were absent:

```text
/proc/emmc_ipanic_header
/proc/emmc_ipanic_console
/proc/emmc_ipanic_threads
```

The fabric sidecars remained present but were byte-identical to their pre-test
copies and still contained the old `Kernel DLT expired` report. Therefore this
test generated no new Linux panic record and no second-kernel command-line
marker.

The strongest current inference is that the machine stalls after userspace has
requested kexec but before the new factory kernel initializes Lenovo's ipanic
driver. The likely window is the stock kernel's reboot notifier,
`machine_shutdown()`, x86 relocation stub, or earliest decompressor entry.

This also rules out Debian, Phosh, the rebuilt vendor kernel and the newer
mainline kernel as the cause of the current failure: even the byte-exact factory
kernel and ramdisk cannot cross the transition.

## Cleanup and current state

All `p90-*` staging files were removed from `/data/local/tmp`. Android remained
healthy at 100% battery. Factory `dumpstate` was pulled once more after cleanup
and retained the exact factory SHA-256.

## Next engineering target

A persistent Debian installation remains blocked until one of these entry paths
works:

1. a signed Lenovo/Intel boot route that accepts a custom kernel;
2. a rebuilt first-stage kernel whose kexec path avoids re-arming the SCU
   watchdog and adds relocation breadcrumbs;
3. a vendor-authorized module/signing route for an auditable handoff shim.

Do not flash the existing unsigned images. The next useful work is offline
source instrumentation of the rebuilt vendor kernel and research into the
Lenovo/Intel signed boot format, not another identical phone-side kexec run.
