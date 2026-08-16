# RAM-only kexec route and live results

## Why kexec is the current boot route

Droidboot's `boot` command is explicitly stubbed on this Intel build, its
`custom_boot` command only writes a four-byte IFWI/UMIP value, and no unlock
handler was found in the exact S149 Droidboot binary. Unsigned OSIP execution
was rejected. The signed stock kernel is therefore the only trusted code path
currently able to launch a second kernel without a persistent boot-slot write.

The preserved Lenovo configuration contains `CONFIG_KEXEC=y`,
`CONFIG_MODULES=y`, `CONFIG_KALLSYMS=y` and `CONFIG_X86_INTEL_MID=y`. A 32-bit
Android syscall probe distinguished implemented, capability-gated calls from
missing calls: invalid explicit x86_64 `kexec_load` flags returned `EPERM`, as
did an invalid privileged reboot request. The probe source is
`tools/kexec-syscall-probe.c`.

## kexec-tools build

Official kexec-tools 2.0.32 was downloaded from kernel.org. The archive hash,
verified against the adjacent signed checksum file, is:

```text
8f81422a5fd2362cf6cb001b511e535565ed0f32c2f4451fb5eb68fed6710a5d
```

It was cross-built as a 32-bit Android executable using NDK r16b. Bionic
compatibility implementations are retained in `tools/kexec-android-compat/`.
The phone executes the resulting binary and reports `kexec-tools 2.0.32`.

## Load/unload proof

Temporary root was obtained through the previously documented Dirty COW
`dumpstate` service technique. The root agent loaded the rebuilt Lenovo
bzImage into RAM and immediately unloaded it:

```text
uid=0 gid=0 euid=0 egid=0
load exit=0
unload exit=0
complete=1 load_result=0 unload_result=0
```

The complete original `/system/bin/dumpstate` was then restored and pulled.
Its SHA-256 remained exactly:

```text
1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c
```

The kexec image itself was RAM-only. The temporary-root method transiently
changed `dumpstate`; no boot/recovery/GPT/userdata/raw-image write occurred.

## Safe execution helper

The first waiter stayed mapped from the temporarily replaced `dumpstate`.
Restoring the backing executable invalidated that process before it could see
the trigger. The corrected agent loads the image and then `exec`s the stock
`/system/bin/sh`. That independent root shell survives restoration. The host
does not create the `go` file until the pulled factory SHA-256 matches.

This ordering gives the execution test a clean cold-boot rollback:

1. load kernel and initrd into RAM;
2. replace the root agent with `/system/bin/sh` and wait;
3. restore and pull-verify factory `dumpstate`;
4. create the trigger;
5. sync and enter kexec.

## Live execution results

### Rebuilt Lenovo kernel and supplied root-ADB ramdisk

At 16:32:57 on 2026-08-03, ADB disconnected immediately after the verified
trigger. No rescue ADB appeared. The phone automatically cold-booted Android
about 108 seconds later. Evidence after return:

```text
ro.boot.bootreason=watchdog
Linux version 3.10.20-x86_64_moor ... Wed May 4 12:16:25 CST 2016
```

The factory `dumpstate` hash still matched. This proves real kexec execution
was attempted, but does not prove the rebuilt kernel reached its entry point.

### Exact factory-kernel control

To separate a rebuild fault from a transition fault, the exact factory bzImage
and gzip/cpio ramdisk were byte-extracted from the signed S149 OSIP image:

| Component | OSIP offset | Bytes | SHA-256 |
|---|---:|---:|---|
| bzImage | `0x34d8` | 6,180,864 | `05e453aa92bf38b0ae499fc25744c697e82998bf5889e2938f9bfc59cb75429f` |
| ramdisk | `0x5e84d8` | 4,433,316 | `b1dbd4345e31768d56a1427b5481c7059427c32f9d53eb4b75ae5361926fb047` |

`file` identifies the factory bzImage as the installed May 2016 kernel and
`gzip -t` accepts the extracted ramdisk. Kexec reused the running factory
command line. This exact factory pair behaved identically: immediate ADB
disconnect, no second-kernel USB, then an automatic return with
`ro.boot.bootreason=watchdog` at the same interval. Therefore the current
failure is in the Moorefield kexec/platform transition, not in Debian or the
rebuilt kernel.

### Intel SCU watchdog experiment

Lenovo's kernel has `CONFIG_INTEL_SCU_WATCHDOG_EVO=y` and exposes
`/sys/class/misc/watchdog/disable`. A root helper successfully wrote `1` and
read it back:

```text
watchdog_disable_rc=0
1
```

The phone nevertheless reset at the same interval. Source audit explains why:
`kernel_kexec()` calls `kernel_restart_prepare()`, whose reboot notifier invokes
`watchdog_set_appropriate_timeouts()` unconditionally. That restarts the SCU
watchdog after the userspace stop and before `machine_kexec()`.

## In-kernel handoff experiment

`tools/p90-kexec-handoff-module.c` is a minimal proposed shim. It resolves the
already-loaded `kexec_image`, calls normal `device_shutdown()` and x86
`machine_shutdown()`, then calls `machine_kexec()` without the reboot-notifier
chain. It does nothing until root writes `1` to its `go` parameter.

The module was built with exact release
`3.10.20-x86_64_moor SMP preempt mod_unload`. A load/unload-only probe was
rejected before module initialization:

```text
insmod: init_module ... failed (Required key not available)
insmod exit=255
```

The published Lenovo defconfig says `CONFIG_MODULE_SIG` is disabled, while the
production kernel enforces a trusted signing key. This is a second material
difference between the GPL build inputs and the shipped kernel. No module code
executed. The next research target is a reversible runtime route around only
this enforcement (the preserved config also declares `CONFIG_DEVKMEM=y`), or a
different way to bypass the SCU reboot notifier.

## Current safety decision

Do not flash an unsigned kernel. Continue with RAM-only experiments. Before
every execution trigger, verify Android boot completion, full battery, and the
factory `dumpstate` hash. A watchdog or forced cold boot returns through the
exact signed S149 boot image.
