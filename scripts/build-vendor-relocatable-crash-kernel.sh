#!/bin/sh
set -eu

: "${P90_LINUX_TREE:?Set P90_LINUX_TREE to Lenovo's complete extracted linux directory}"
: "${P90_TOOLCHAIN:?Set P90_TOOLCHAIN to Google's x86_64-linux-android-4.9 directory}"
: "${P90_BUILD_OUT:?Set P90_BUILD_OUT to a writable output directory}"

kernel_source="$P90_LINUX_TREE/kernel"
cross_prefix="$P90_TOOLCHAIN/bin/x86_64-linux-android-"
config_tool="$kernel_source/scripts/config"
target_cc="${cross_prefix}gcc"

# Some archived Android GCC builds ask collect2 for an unprefixed `ld` while
# linking the x86 VDSO.  Point only the target compiler at a directory that
# contains an `ld` symlink; do not alter HOSTCC/HOSTLD.
if [ -n "${P90_TARGET_LINKER_DIR:-}" ]; then
    test -x "$P90_TARGET_LINKER_DIR/ld"
    target_cc="$target_cc -B$P90_TARGET_LINKER_DIR/"
fi

test -f "$kernel_source/arch/x86/configs/x86_64_moor_defconfig"
test -x "$cross_prefix"gcc
test -x "$config_tool"

mkdir -p "$P90_BUILD_OUT"
cp "$kernel_source/arch/x86/configs/x86_64_moor_defconfig" \
    "$P90_BUILD_OUT/.config"

"$config_tool" --file "$P90_BUILD_OUT/.config" \
    --enable KEXEC \
    --enable RELOCATABLE \
    --enable CRASH_DUMP \
    --enable PROC_VMCORE \
    --set-val PHYSICAL_START 0x1000000 \
    --set-val PHYSICAL_ALIGN 0x1000000

host_flags='-Wall -Wmissing-prototypes -Wstrict-prototypes -O2 -fomit-frame-pointer -fcommon'

make -C "$kernel_source" O="$P90_BUILD_OUT" ARCH=x86_64 \
    CROSS_COMPILE="$cross_prefix" CC="$target_cc" \
    HOSTCFLAGS="$host_flags" defoldconfig
make -C "$kernel_source" O="$P90_BUILD_OUT" ARCH=x86_64 \
    CROSS_COMPILE="$cross_prefix" CC="$target_cc" \
    HOSTCFLAGS="$host_flags" \
    "-j${P90_JOBS:-4}" bzImage

grep -E '^(CONFIG_KEXEC|CONFIG_RELOCATABLE|CONFIG_CRASH_DUMP|CONFIG_PROC_VMCORE|CONFIG_PHYSICAL_START|CONFIG_PHYSICAL_ALIGN)=' \
    "$P90_BUILD_OUT/.config"
sha256sum "$P90_BUILD_OUT/arch/x86/boot/bzImage" "$P90_BUILD_OUT/.config"
