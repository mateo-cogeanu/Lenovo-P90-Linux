#!/bin/sh
set -eu

: "${P90_LINUX_TREE:?Set P90_LINUX_TREE to Lenovo's complete extracted linux directory}"
: "${P90_TOOLCHAIN:?Set P90_TOOLCHAIN to Google's x86_64-linux-android-4.9 directory}"
: "${P90_BUILD_OUT:?Set P90_BUILD_OUT to a writable output directory}"

kernel_source="$P90_LINUX_TREE/kernel"
cross_prefix="$P90_TOOLCHAIN/bin/x86_64-linux-android-"

test -f "$kernel_source/arch/x86/configs/x86_64_moor_defconfig"
test -x "${cross_prefix}gcc"

mkdir -p "$P90_BUILD_OUT"
cp "$kernel_source/arch/x86/configs/x86_64_moor_defconfig" "$P90_BUILD_OUT/.config"

host_flags='-Wall -Wmissing-prototypes -Wstrict-prototypes -O2 -fomit-frame-pointer -fcommon'

make -C "$kernel_source" O="$P90_BUILD_OUT" ARCH=x86_64 \
    CROSS_COMPILE="$cross_prefix" HOSTCFLAGS="$host_flags" defoldconfig
make -C "$kernel_source" O="$P90_BUILD_OUT" ARCH=x86_64 \
    CROSS_COMPILE="$cross_prefix" HOSTCFLAGS="$host_flags" "-j${P90_JOBS:-4}" bzImage

sha256sum "$P90_BUILD_OUT/arch/x86/boot/bzImage" "$P90_BUILD_OUT/.config"
