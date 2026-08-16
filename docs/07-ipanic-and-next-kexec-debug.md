# Persistent panic logging and next kexec debug step

## What was captured

After the watchdog-reset kexec series, Android was booted and the live kernel's
persistent panic interfaces were inspected from ADB.  The phone exposes:

```text
/proc/emmc_ipanic_header
/proc/emmc_ipanic_console
/proc/emmc_ipanic_threads
/proc/ipanic_fabric_err
/proc/ipanic_fabric_recv_err
/sys/fs/pstore
/data/dontpanic
```

The readable panic data was archived under:

```text
work/p90-native-port/live-test/2026-08-03-ipanic-capture/
```

The capture contains about 2 MiB of preserved data:

```text
245412  emmc_ipanic_console.txt
464     emmc_ipanic_header.txt
1825745 emmc_ipanic_threads.txt
1331    ipanic_fabric_err.txt
1331    ipanic_fabric_recv_err.txt
```

`/sys/fs/pstore` and `/data/dontpanic` were present but did not list readable
files from the shell account during this capture.

## Interpretation

The preserved record is a real stock-kernel panic, but it should be treated as a
baseline until a fresh test is captured after clearing or fingerprinting the
existing record.  The log contains an Intel SCU watchdog panic while the stock
kernel was in the idle/suspend path:

```text
intel_scu_watchdog_evo: [SHTDWN] watchdog_warning_interrupt, WATCHDOG TIMEOUT!
Kernel panic - not syncing: Kernel Watchdog
```

The stack goes through `watchdog_warning_interrupt()`, `panic()`,
`start_kernel()`, `x86_64_start_reservations()` and
`x86_64_start_kernel()`.  That is consistent with an ordinary kernel lifetime
ending in a watchdog panic; it does not contain the current kexec helper markers.

The fabric error sidecar reports:

```text
Fabric error type: Kernel DLT expired
```

This confirms that Lenovo's persistent crash path is useful, but it does not yet
prove where the RAM-only kexec handoff stalls.

## Relevant source behavior

Lenovo's `emmc_ipanic` driver registers a panic notifier and writes console,
thread and global-buffer records to the eMMC panic area.  The proc entries also
have a write handler:

```text
emmc_ipanic_proc_write()
  schedule_work(&proc_removal_work)

emmc_ipanic_remove_proc_work()
  emmc_panic_erase(NULL, NULL)
  remove_proc_entry(...)
```

So a controlled future run can:

1. archive the current ipanic files;
2. write to one ipanic proc entry to clear the old stored panic;
3. run one RAM-only kexec test;
4. after Android returns, recapture the ipanic files immediately.

Clearing the panic record is a diagnostic write to the panic-log area, not a
boot, recovery, GPT, system or userdata write.  The old record should be kept in
the workspace before clearing.

## Next controlled test

The next useful execution test is an exact S149 stock bzImage plus exact S149
ramdisk, still RAM-only, with an explicit noisy command line:

```text
console=logk0 loglevel=8 no_console_suspend ignore_loglevel panic=10
```

The host-side ordering remains the same as the earlier safe shell handoff:

1. stage kexec-tools, exact stock bzImage and exact stock ramdisk in
   `/data/local/tmp`;
2. use the temporary `dumpstate` root entry only to load the kexec image;
3. exec a stock root shell that waits for a trigger;
4. restore and pull-verify factory `dumpstate`;
5. trigger kexec;
6. wait for Android to return and immediately capture ipanic data.

Do not flash unsigned images for this test.

## Local source patch

`patches/0006-p90-scu-watchdog-kexec-aware-reboot-notifier.patch` documents the
desired rebuilt-kernel behavior: do not re-arm the SCU watchdog during a kexec
restart when the reboot command is `kexec reboot`.

This patch is not enough to fix the first transition from the untouched stock
kernel if that kernel passes `NULL` to `kernel_restart_prepare()`.  Its value is
for rebuilt kernels and for documenting the second-stage behavior we want once a
native Linux kernel can enter and exit kexec cleanly.
