#!/bin/sh
set -eu

factory_sha=1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c
launcher=work/p90-native-port/build-tools/p90-wayland/p90-debian-autoboot-v32
installer=work/p90-native-port/build-tools/p90-wayland/p90-debian-autoboot-installer-v32
wallpaper_helper=work/p90-native-port/build-tools/p90-wayland/p90-phosh-wallpaper-live.so
hook=outputs/p90-native-linux-port/tools/p90-debian-autoboot-service.sh
verify_before=/tmp/p90-v32-dumpstate-before
verify_after=/tmp/p90-v32-dumpstate-after
restore_needed=0

cleanup() {
    if [ "$restore_needed" = 1 ]; then
        adb shell '/data/local/tmp/p90-dcow /data/local/tmp/p90-dumpstate-original /system/bin/dumpstate' >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT HUP INT TERM

adb pull /system/bin/dumpstate "$verify_before" >/dev/null
test "$(shasum -a 256 "$verify_before" | awk '{print $1}')" = "$factory_sha"
adb push "$launcher" /data/local/tmp/p90-debian-autoboot >/dev/null
adb push "$installer" /data/local/tmp/p90-debian-autoboot-installer-v32 >/dev/null
adb push "$wallpaper_helper" /data/local/tmp/p90-phosh-wallpaper-live.so >/dev/null
adb push "$hook" /data/local/tmp/p90-debian-autoboot-service.sh >/dev/null
test "$(adb shell 'wc -c </data/local/tmp/p90-phosh-wallpaper-live.so' | tr -d '\r ')" = \
    "$(wc -c <"$wallpaper_helper" | tr -d ' ')"

# Install the Debian-side helper first; this command is intentionally confined
# to the already-created P90 Debian rootfs.
command_file=$(mktemp)
trap 'rm -f "$command_file"; cleanup' EXIT HUP INT TERM
printf '%s\n' \
    'HOST:cp /data/local/tmp/p90-phosh-wallpaper-live.so /data/local/p90-debian/usr/local/lib/p90-phosh-wallpaper-live.so.v32 && chmod 0755 /data/local/p90-debian/usr/local/lib/p90-phosh-wallpaper-live.so.v32 && mv /data/local/p90-debian/usr/local/lib/p90-phosh-wallpaper-live.so.v32 /data/local/p90-debian/usr/local/lib/p90-phosh-wallpaper-live.so' \
    >"$command_file"
outputs/p90-native-linux-port/scripts/run-root-command-safely.sh "$command_file"
rm -f "$command_file"
test "$(adb shell 'wc -c </data/local/p90-debian/usr/local/lib/p90-phosh-wallpaper-live.so' | tr -d '\r ')" = \
    "$(wc -c <"$wallpaper_helper" | tr -d ' ')"

adb shell '/data/local/tmp/p90-dcow /data/local/tmp/p90-debian-autoboot-installer-v32 /system/bin/dumpstate --no-pad' >/dev/null
restore_needed=1
adb shell 'setprop ctl.start dumpstate'
sleep 1

i=0
while [ "$i" -lt 20 ]; do
    status=$(adb shell 'cat /data/local/tmp/p90-debian-autoboot-install.txt 2>/dev/null' | tr -d '\r' || true)
    if echo "$status" | grep -q '^remount-ro=0 '; then
        break
    fi
    i=$((i + 1))
    sleep 1
done
test "$i" -lt 20
printf '%s\n' "$status"

adb shell '/data/local/tmp/p90-dcow /data/local/tmp/p90-dumpstate-original /system/bin/dumpstate' >/dev/null
restore_needed=0
adb pull /system/bin/dumpstate "$verify_after" >/dev/null
test "$(shasum -a 256 "$verify_after" | awk '{print $1}')" = "$factory_sha"
