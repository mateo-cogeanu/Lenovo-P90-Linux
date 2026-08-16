# Crash-memory and preserve-context results (2026-08-10)

## Safety boundary

These tests did not write raw p1, recovery, Droidboot, system or userdata. The
only persistent test write was to logical `boot` using the signed S149-derived
crash-memory candidate from chapter 09. When it entered Lenovo Recovery instead
of Android, the exact signed S149 boot image was immediately restored and
Android was verified. All later FBRL files used absolute paths in Droidboot RAM.

## Signed-command-line crash-memory candidate

The guarded candidate was
`work/p90-native-port/artifacts/stock-s149/boot-crashkernel-128M-at-1G.img`
(SHA-256 `ec43ec11fd40aa5072af9d7ac52d0118d0a14f97d384a6c311f9569ee29ff16e`).
It differs from exact S149 boot by 29 bytes, all within Intel's engineering
command-line field.

Droidboot accepted the logical write, but the next boot entered Lenovo Recovery
in `fota` mode. Recovery reported `/sys/kernel/kexec_crash_size = 0`, so no
crash-memory reservation was established. The exact signed stock boot image
(SHA-256 `0bc13d50f88c1f32297e932c3aff63c013c52dcc214255a5c5192b0d25c6646b`)
was restored. Android then reported build `P90_S149_160504_ROW`, boot mode
`main`, completed boot and 100% battery. Do not repeat this unchanged test.

## Preserve-context hypothesis

`KEXEC_PRESERVE_CONTEXT` was evaluated because it could enter
`machine_kexec()` without the normal reboot-notifier chain that rearms Lenovo's
SCU watchdog. The initial static i686 loader passed the x86_64 syscall flag but
its i386 bzImage loader failed before the syscall:

```text
Could not put setup code above the kernel parameters
preserve-context load exit=1
complete=1 load=1 unload=-1
```

Both kexec slots remained zero and no execution was requested.

## Native x86_64 Android kexec-tools

kexec-tools 2.0.32 was cross-built as a static native x86_64 Android executable
using Apple Clang, Android NDK r16b, LLD and small auditable Bionic shims:

```text
work/p90-native-port/kexec-tools/build-android-x86_64/build/sbin/kexec
SHA-256 fa1f71c1ca163388b4e15f0df14d6791d3ef578c5684337258d820e21dc4696c
```

It ran on the phone and reported `kexec-tools 2.0.32`. Unlike the i686 loader,
it needs no architecture patch. Compatibility source is retained under
`tools/kexec-android-compat/`. Static Bionic already supplies `setmntent` and
`endmntent`, so that source is compiled with
`P90_STATIC_BIONIC_HAS_MNTENT=1`; the custom purgatory include supplies the x86
port-I/O intrinsics absent from NDK headers.

## Native load-only probes

The first native probe selected `--type=bzImage` with exact stock and reproduced
the placement error. Header inspection explained why exact stock cannot use the
native `bzImage64` path:

```text
stock S149:   protocol=0x20c xloadflags=0x1 relocatable=0
vendor rescue: protocol=0x20c xloadflags=0x3 relocatable=1
```

The second probe used `--type=bzImage64`, the verified relocatable vendor rescue
kernel and its root-ADB ramdisk. Image construction completed, but native
`kexec_load` returned `EINVAL` with flags `0x3e0002`
(`KEXEC_ARCH_X86_64 | KEXEC_PRESERVE_CONTEXT`). Lenovo's matching Moorefield
defconfig enables `CONFIG_KEXEC=y` but not `CONFIG_KEXEC_JUMP`; the syscall's
permitted mask therefore excludes `KEXEC_PRESERVE_CONTEXT`.

Complete results are retained at
`work/p90-native-port/live-test/2026-08-10-preserve-context/`. Both kexec slots
were zero afterward. The probe never called `kexec --exec` and chained back to
the original Droidboot ADB daemon.

## Decision

All three current first-kernel transition variants are closed:

1. normal kexec is accepted, but hangs and watchdog-resets;
2. crash kexec has no signed-boot-compatible reserved memory;
3. preserve-context kexec is rejected because the production kernel lacks
   `CONFIG_KEXEC_JUMP`.

Do not repeat these transitions. The next useful milestone must be a genuinely
different trusted kernel-entry path, or native Debian userspace under the signed
stock kernel without claiming that it replaces the kernel.
