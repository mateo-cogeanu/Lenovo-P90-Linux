# PowerVR/libhybris bring-up on the Lenovo P90

Date: 2026-08-15

## Result reached

The stock Moorefield/PowerVR stack can now be loaded from a 32-bit glibc
process through libhybris.  The successful diagnostic returned:

```
OpenGL ES 3.0 build 1.3@2876724
Imagination Technologies
PowerVR Rogue Hood
```

This proves that the stock `pvrsrvkm` kernel driver, Rogue firmware, vendor
EGL implementation, GLES implementation, and a Linux/glibc client can create
and use a hardware context.  It is not Mesa/llvmpipe.

The primary evidence is in:

```
work/p90-native-port/live-test/2026-08-15-libhybris-x86-tls/
  probe-p90-current-bridge.txt
  probe-powerVR-blue-frame.txt
  probe-bionic-powervr-pbuffer.txt
```

Every root diagnostic was bounded by an independent restore watchdog.  The
factory `/system/bin/dumpstate` was restored afterward and repeatedly matched:

```
1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c
```

## Required x86 TLS work

The KitKat Bionic libraries directly access fixed x86 TLS slots.  Slots 0--6
are occupied by Debian's i386 glibc TCB, so staged copies—not `/system`
files—were patched as follows:

- Bionic errno: slot 2 to slot 7.
- Android GL dispatch/preinit: slot 3 to slot 8.

The exact hash-checked patcher is:

```
outputs/p90-native-linux-port/tools/patch-kitkat-x86-hybris-tls.py
```

PowerVR also uses compiler-emulated TLS through pthread keys.  A valid glibc
pthread key may be zero, but libhybris treated key zero as invalid.  The
reproducible source patch is:

```
outputs/p90-native-linux-port/patches/libhybris-x86-glibc-pthread-key-zero.patch
```

## PowerVR current-context bridge

`eglMakeCurrent` succeeds through Android META-EGL, but on this x86/glibc
combination the PowerVR GLES emulated-TLS value remains null.  Native Bionic
was used as the control experiment and showed the missing relationship:

1. META-EGL wrapper word 4 points to the vendor EGL context.
2. Vendor-context offset `0x20` points to the PowerVR GLES context.
3. PowerVR's internal current-context setter stores that GLES pointer in its
   compiler-emulated TLS object.

Calling that bridge explicitly in the diagnostic changes the TLS value from
null to the GLES context and makes `glGetString` return the real Rogue driver.
The reproducible diagnostic source is:

```
outputs/p90-native-linux-port/tools/libhybris-hwcomposer-tls-test.cpp
```

This is currently a device/DDK-specific proof.  Before compositor integration,
it should be moved into a narrowly guarded P90 libhybris EGL compatibility
shim with runtime validation of the exact vendor library hashes.

## Remaining presentation blocker

Hardware rendering initializes, but the first hwcomposer presentation does
not yet reach the panel.  The measured failure is:

```
gralloc allocate: width=1080 height=1920 format=0x2 usage=0x1a00
status=-14 (EFAULT), handle=null, stride=0
eglSwapBuffers: EGL_FALSE, EGL_BAD_SURFACE (0x300d)
```

The PowerVR log correspondingly reports `DequeueBufferWrapper` failure and an
invalid native window.  Opening the framebuffer side before gralloc did not
change the result, so that experiment was rejected.

The next task is to compare Moorefield's working SurfaceFlinger gralloc call
path with libhybris's `alloc_device_t::alloc` call and repair the buffer-handle
ABI/initialization.  Only after a real buffer is allocated and presented should
the compatibility shim be integrated into a root Wayland compositor.

## Full-HWC and WSBM diagnosis

A native 32-bit Bionic probe was extended to follow Android's HWC sample
initialization order before allocating the framebuffer-target buffer:

1. load `gralloc.moorefield.so`;
2. attempt the legacy framebuffer open (this vendor HAL returns success with a
   null framebuffer device);
3. open `hwcomposer.moorefield.so`;
4. query and unblank display 0; and
5. open `alloc_device_t` and allocate the `1080x1920`, RGBX8888, `0x1a00`
   buffer.

HWC initialization itself succeeds and reports the real panel:

```
HWC version: 1.3
configs: 1
width: 1080
height: 1920
unblank: 0
```

The allocator's exported `alloc`, `free`, `dump`, and `close` pointers are all
non-null.  The failure is deeper in the Intel display stack: the process jumps
to address zero from `libwsbm.so`.  A ptrace stack/map probe identified the
first observed return site as `wsbmBOUnmap+0x19`; later runs reached
`wsbmBOUnreference+0x7c`, whose object-destruction method is also null.  This
shows that standalone HWC is receiving an incompletely initialized WSBM buffer
object, rather than libhybris merely using a wrong public gralloc function
signature.

The following variants did not fix it and are rejected:

- registering Android's sample HWC callbacks;
- omitting HWC callback registration to match libhybris;
- initializing a native PowerVR EGL pbuffer/context before HWC; and
- suppressing WSBM unmap in one bounded diagnostic process.

The last variant only moved the fault to another missing WSBM destructor
callback.  It deliberately leaked its temporary mapping and must not be used
as a production workaround.  Evidence and retained diagnostic sources:

```
work/p90-native-port/live-test/2026-08-15-libhybris-x86-tls/current/
  probe-bionic-gralloc-hwc-init.txt
  probe-bionic-gralloc-hwc-pointers.txt
  probe-bionic-gralloc-stack-maps.txt
  probe-bionic-egl-first-gralloc.txt
  probe-bionic-wsbm-no-unmap.txt

outputs/p90-native-linux-port/tools/
  bionic-moorefield-gralloc-test.c
  android-root-bionic-gralloc-stack-probe.c
  p90-wsbm-null-unmap-guard.c
```

All runs used an independent recovery watchdog.  After every run,
SurfaceFlinger was running and the factory `dumpstate` SHA-256 matched the
value documented above.

## SurfaceFlinger presentation route

The safer accelerated architecture is to leave Android's working
SurfaceFlinger/HWC/WSBM owner alive and submit a PowerVR-rendered native window
as a SurfaceFlinger layer.  The pinned libhybris tree already contains the
glibc `libsf` wrapper and `test_sf`, but its Android-side
`libsf_compat_layer.so` had not been built.

The missing 32-bit KitKat library was built from:

- pinned libhybris `compat/surface_flinger` source;
- official AOSP `frameworks/native` tag `android-4.4.4_r2`, commit
  `9c7db08049e1d36c2f60dd703f27c432bc8cfbc5`; and
- the P90's matching 32-bit Android system libraries used only at link time.

Artifact:

```
work/p90-native-port/build-tools/libsf_compat_layer-p90-kitkat.so
SHA-256: 67e96b9a1d8690b16b1f631bafa8ebe27215c053b9f331eac7617408085655f1
```

It is staged only at
`/data/local/tmp/p90-hybris/android-patched/libsf_compat_layer.so`; no `/system`
library was replaced.  It is compiled with exceptions and RTTI disabled to
match the KitKat platform build; the first build retained
`__gxx_personality_v0` and was correctly rejected by the Android linker.

Two instrumented full-screen controls then succeeded:

1. A native 32-bit Bionic SurfaceFlinger client created a window, reported
   `PowerVR Rogue Hood`, and completed 240 swaps with `GL_NO_ERROR`.
2. A 32-bit glibc/libhybris client loaded the compatibility library, created
   the same SurfaceFlinger window, installed the P90 current-context bridge,
   reported `PowerVR Rogue Hood`, and completed 240 swaps with `GL_NO_ERROR`.

The decisive glibc evidence is:

```
stage=current ... bridge=1 renderer=PowerVR Rogue Hood egl-error=0x3000
complete=1 gl-error=0x0
```

Artifacts and evidence:

```
work/p90-native-port/build-tools/
  p90-bionic-surfaceflinger-powervr-test
    SHA-256: 66c3ff6df24c965afdce45549f708cba1727ed006d9b17b314d5c73827997cbd
  p90-libhybris-surfaceflinger-powervr-test
    SHA-256: 26c29ac22b8f37c01f534310ab438c186e8baafc401eed998139a78a99eb5b53

work/p90-native-port/live-test/2026-08-15-libhybris-x86-tls/current/
  probe-libhybris-surfaceflinger-success.txt
```

SurfaceFlinger remained running after both tests.  This closes the hardware
rendering-and-presentation proof: the viable compatibility architecture is a
Linux/Wayland compositor rendering with PowerVR into an Android
SurfaceFlinger-owned native window.  Direct standalone HWC/gralloc probing
should not be used for integration.

## Relation to the larger port

- The existing Phosh/Phoc session is already Wayland, but nested through Xorg
  fbdev and software rendering.
- A successful PowerVR hwcomposer root compositor can replace that software
  display layer and provide accelerated Wayland clients, including Foot.
- A newer upstream kernel remains a separate long-term driver-port project;
  Linux 6.x does not contain a ready driver for this exact Moorefield Rogue
  device.
- Android-free power-on also remains separate because the firmware rejects the
  unsigned OSIP image.  No unchanged unsigned-flash or kexec attempt should be
  repeated.

## Working Wayland/Phosh PowerVR bridge (2026-08-15)

The SurfaceFlinger proof was extended into a small P90-specific Wayland server:

```
outputs/p90-native-linux-port/tools/p90-wayland-sf-compositor.cpp
work/p90-native-port/generated/p90-wayland/
  xdg-shell-server-protocol.h
  xdg-shell-protocol.c
```

It is a 32-bit glibc process because the P90 PowerVR userspace driver is
32-bit.  It implements the protocol subset needed by the nested wlroots backend:

- `wl_compositor`, `wl_surface`, `wl_region`, and `wl_shm`;
- a 1080x1920 `wl_output`;
- `xdg_wm_base`, `xdg_surface`, and a fullscreen `xdg_toplevel`;
- a keyboard-capable seat required by Phosh, but no pointer capability; and
- `wl_touch` events translated directly from `/dev/input/event2`.

Committed ARGB/XRGB `wl_shm` buffers are uploaded as `GL_BGRA_EXT` textures and
drawn into the proven libhybris SurfaceFlinger EGL surface.  Consequently Phoc
runs as a real nested Wayland compositor and Phosh receives touch semantics
without the former Xorg mouse-pointer emulation.  Phoc currently uses its
pixman renderer; PowerVR accelerates the final texture composition and display
submission, not every application's rasterization.

The old 3.10 kernel also lacks `memfd_create(2)`.  The retained compatibility
shim now supplies both `getrandom` and a `memfd_create` fallback backed by an
immediately unlinked POSIX shared-memory object:

```
outputs/p90-native-linux-port/tools/p90-getrandom-compat.c
```

The first run reached `WL-1` but could not allocate a wlroots output buffer and
logged `shared memfd open() failed: Function not implemented`.  After adding
the fallback, a guarded 60-second run produced the decisive sequence:

```
PowerVR ready: PowerVR Rogue Hood
touch ready: /dev/input/event2 x=0..1080 y=0..1920
Wayland ready: /data/local/p90-debian/run/user/0/wayland-0
outer-ready=1
phoc-output-Message: Output 'WL-1' added
first shm frame presented: 1080x1920 stride=4320
phosh-display-window=ready
probe-seconds=60
probe-complete=1
```

During that run Phoc, Phosh, and Squeekboard all remained alive.  The supervisor
then terminated both process groups.  SurfaceFlinger remained running and the
factory `dumpstate` was restored and verified with SHA-256
`1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c`.

The root supervisor deliberately re-execs its staged copy before doing any
work.  This moves execution away from the temporary Dirty-COW-backed
`dumpstate` mapping, so restoring the factory executable does not strand the
outer compositor or prevent bounded cleanup.  A separate root cleanup helper
also scans exact P90 process command lines and terminates stale test processes.
The optional `/data/local/tmp/p90-wayland-session-seconds` file selects a
bounded interactive duration from 30 to 1800 seconds; absent that file, the
supervisor defaults to 60 seconds.

Retained sources:

```
outputs/p90-native-linux-port/tools/
  p90-wayland-sf-compositor.cpp
  android-root-p90-wayland-sf-probe.c
  android-root-p90-wayland-phosh-probe.c
  android-root-p90-wayland-cleanup.c
  p90-getrandom-compat.c
```

Retained artifacts:

```
work/p90-native-port/build-tools/p90-wayland/
  p90-wayland-sf-compositor
  p90-wayland-phosh-probe
  p90-wayland-cleanup
  libhybris-common-clean.so.1.0.0
work/p90-native-port/build-tools/p90-getrandom-compat.so
```

Evidence is under
`work/p90-native-port/live-test/2026-08-15-wayland-sf/`.  In particular,
`phosh-wayland-probe-1.txt` preserves the missing-keyboard diagnosis and the
phone-side final probe log records the successful 60-second run.  The clean
libhybris build retains the x86 pthread-key-zero compatibility change but
removes all `P90_EMUTLS` diagnostic printing.

This is the first working PowerVR-presented Wayland/Phosh session on this port.
It is still a compatibility stage on the stock Android kernel and
SurfaceFlinger; it is not yet the eventual Android-free power-on replacement.

For the final persistent v18 configuration, touchscreen ownership moved from
the outer server's translated `wl_touch` path to Phoc's direct libinput backend.
Tracing proved that the outer path emitted correct events, but the tested
wlroots nested backend did not act on them. Direct libinput restored working
touch while retaining the same PowerVR-presented Wayland output. See
`docs/14-persistent-debian-phosh-autoboot.md` for the installed boot state.

The subsequent v19 configuration stops Android `media`, `zygote`, and thereby
`system_server` only after Phosh is ready.  This prevents Android applications
from competing for input or appearing behind Linux.  SurfaceFlinger remains
the sole Android graphical component because it owns the proprietary PowerVR
HWC/WSBM presentation path.  Frame delivery continued after the cutoff with
changing hashes and no GL errors.  Full details, recovery behavior, hashes,
and evidence paths are in
`docs/14-persistent-debian-phosh-autoboot.md` under “Linux-only UI update: v19”.
