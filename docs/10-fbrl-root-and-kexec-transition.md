# FBRL root access and kexec transition research

## Scope and safety boundary

This work uses IntelAndroid-FBRL only to replace files in Droidboot's RAM and
start a temporary root process. No FBRL command below names a persistent
partition. Raw p1, system and userdata are out of scope. Every execution test
keeps the exact signed S149 boot image available as the watchdog rollback path.

The production kernel's module-signature enforcement is not bypassed, and no
runtime kernel-memory patch is used. The only binary patch is to a temporary
RAM copy of our own `kexec-tools` executable, described below.

## Original FBRL package

The original developer archive is preserved at:

`work/p90-native-port/fbrl-analysis/IntelAndroid-FBRL-07-24-2015.7z`

- Published MD5: `f2b26ac80e089a257a3dd636a77d0fbd`
- Local SHA-256:
  `696e855212786c6a6bd3f3740086d33167d4996551ce127f179babf34050be9b`
- `fbrl.trigger` SHA-256:
  `629f596edb06d9455efff08d150143b967434786a7b757b0319d0eeeb650362f`
- `recovery.launcher` SHA-256:
  `99abcfaf61eb3301c59971fb7cda76a7b16b21734fcd84058eee351c488b461a`

Offline inspection of `launcher.bat`, the Droidboot ramdisk and its init files
explains the four triggers. T1 temporarily replaces RAM `/sbin/adbd` and invokes
`oem startftm`. T4 stops RAM `/system/bin/logcat`, replaces it, and restarts it
through `oem stop_partitioning`. The apparent fastboot `flash` operations use
absolute RAM paths, not GPT partition names.

The included TWRP/CWM ZIPs do not contain or start another kernel. The launcher
unpacks and runs recovery userspace under Lenovo's already-running kernel. This
is useful for root userspace but cannot itself solve the kexec transition.

## Root and loader proof

The T1 route executed our wrapper as:

```text
uid=0 gid=0 euid=0 egid=0
uname.machine=i686
```

Although the running kernel is x86_64, Droidboot userspace is 32-bit and reports
`i686`. Upstream kexec-tools therefore selected the i386 physical architecture
and the kernel rejected it. The auditable wrapper
`tools/fbrl-kexec-load-unload-chain.c` verifies the expected four instruction
bytes at file offset `0xa1c0` in the temporary RAM copy of our loader, then
replaces only that function prologue with `mov eax,0x003e0000; ret`. This makes
the loader pass `KEXEC_ARCH_X86_64`; it does not modify the kernel.

The direct zero-segment syscall and a complete load/unload then succeeded:

```text
direct_x86_64_zero result=0 errno=0 (Success)
patched physical_arch=KEXEC_ARCH_X86_64
load exit=0
unload exit=0
complete=1 load_result=0 unload_result=0
```

The successfully accepted pair was the relocatable vendor 3.10.20 bzImage plus
the preserved root-ADB vendor ramdisk. This proves root execution, the compat
`kexec_load` syscall, image parsing, segment allocation and kernel acceptance.

## Why execution still resets

Actual `kexec --exec` attempts with both the rebuilt kernel and the byte-exact
factory S149 kernel/ramdisk disconnect USB, then return through the signed stock
boot image with `androidboot.bootreason=watchdog`. A fresh control created no
new `emmc_ipanic` record, placing the failure before observable second-kernel
initialization.

The exact Lenovo driver source explains one platform defect. Writing `1` to
`/sys/class/misc/watchdog/disable` stops the Intel SCU watchdog, but
`kernel_kexec()` calls `kernel_restart_prepare()`. Lenovo's reboot notifier then
unconditionally calls `watchdog_set_appropriate_timeouts()`, rearming the SCU
watchdog. After the transition hangs, that watchdog returns the phone to the
untouched signed boot image.

A minimal module was built to call the ordinary device/x86 shutdown and
`machine_kexec()` without the reboot notifier. The production kernel rejected
the unsigned module with `ENOKEY` before its initialization function executed.
The matching published Moorefield defconfig says module signing is disabled,
so this is a material mismatch between Lenovo's published inputs and the shipped
kernel. Signature enforcement was not bypassed and this route is closed.

## CPU0-only exact-stock control (2026-08-09)

`tools/fbrl-kexec-cpu0-handoff.c` tests whether Moorefield hangs while stopping
secondary CPUs. It offlines CPUs 3, 2 and 1 through their standard sysfs
`online` attributes, refuses the handoff if any write fails, loads the exact
factory pair, stops the watchdog and calls the normal kexec execution command.

- Source SHA-256:
  `4be100152791bf998a4e5006543dbb5d2807f939bca0975ca93034b854ba3150`
- Static i686 binary SHA-256:
  `792ee3d6405f2ff7359310316aa0cbb1a480b5fd6bfbbe265b137e18b7e2991c`
- Factory bzImage SHA-256:
  `05e453aa92bf38b0ae499fc25744c697e82998bf5889e2938f9bfc59cb75429f`
- Factory ramdisk SHA-256:
  `b1dbd4345e31768d56a1427b5481c7059427c32f9d53eb4b75ae5361926fb047`

All temporary paths were staged successfully and T1 produced its expected USB
disconnect. The phone again returned as `P90_S149_160504_ROW` with boot reason
`watchdog`. Therefore secondary-CPU offlining does not fix the transition.

Post-test checks:

```text
/system/bin/dumpstate SHA-256 =
1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c
/sys/kernel/kexec_loaded = 0
/sys/kernel/kexec_crash_loaded = 0
```

The pulled integrity copy is retained at
`work/p90-native-port/live-test/cpu0-kexec-control/dumpstate-after`.

## Result and next boundary

Normal-shutdown kexec is now ruled out with the rebuilt kernel, the exact stock
control, explicit watchdog disable and a CPU0-only control. FBRL remains a safe
temporary root transport, not a second-kernel loader. Do not repeat these normal
kexec execution tests.

The standard crash-kexec path would avoid reboot notifiers, but the running
kernel has no reserved crash-memory region. Testing it would first require a
verified signed-boot-compatible way to add `crashkernel=`. The existing modified
OSIP command-line candidate remains unflashed and must not be used until its
bootloader acceptance and rollback behavior are established without relying on
an unsigned image.
