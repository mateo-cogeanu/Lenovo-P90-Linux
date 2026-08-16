#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
NDK=${ANDROID_NDK_ROOT:-/Users/mateocogeanu/Library/Android/sdk/ndk/29.0.13113456}
TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/darwin-x86_64
CXX=$TOOLCHAIN/bin/i686-linux-android21-clang++
SYSROOT=$TOOLCHAIN/sysroot
CORE=$ROOT/work/p90-native-port/upstream/aosp-4.4.4/system/core
HARDWARE=$ROOT/work/p90-native-port/upstream/aosp-4.4.4/hardware/libhardware
NATIVE=$ROOT/work/p90-native-port/upstream/aosp-frameworks-native-4.4.4
AV=$ROOT/work/p90-native-port/upstream/aosp-frameworks-av-android-4.4.4_r2
LIBS=$ROOT/work/p90-native-port/build-tools/p90-device-libs
GNUSTL=$ROOT/work/p90-native-port/toolchains/android-ndk-r16b/sources/cxx-stl/gnu-libstdc++/4.9
SOURCE=$ROOT/outputs/p90-native-linux-port/tools/bionic-camera-service-jpeg-capture.cpp
OUTPUT=$ROOT/work/p90-native-port/build-tools/p90-bionic-camera-service-jpeg-capture

"$CXX" --sysroot="$SYSROOT" -std=gnu++98 -DHAVE_SYS_UIO_H \
    -fPIE -pie -fno-exceptions -fno-rtti \
    -fno-stack-protector -Wno-unused-parameter -Wno-deprecated-register \
    -nostdinc++ -nostdlib++ -I"$GNUSTL/include" -I"$GNUSTL/libs/x86/include" \
    -I"$CORE/include" -I"$HARDWARE/include" -I"$NATIVE/include" \
    -I"$NATIVE/opengl/include" -I"$AV/include" \
    "$SOURCE" -L"$LIBS" \
    -Wl,-rpath-link,"$LIBS" -Wl,--allow-shlib-undefined \
    -lcamera_client -lcamera_metadata -lgui -lui -lbinder -lhardware \
    -lcutils -lutils -llog -lstdc++ -lEGL -ldl -lm \
    -o "$OUTPUT"

file "$OUTPUT"
shasum -a 256 "$OUTPUT"
