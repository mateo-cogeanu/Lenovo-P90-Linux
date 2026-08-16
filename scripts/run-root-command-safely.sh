#!/bin/sh
set -eu

factory_sha=1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c
command_file=${1:?usage: run-root-command-safely.sh COMMAND_FILE}
temporary_dir=$(mktemp -d /tmp/p90-root-command.XXXXXX)
restore_needed=0

restore_dumpstate() {
    attempt=0
    while [ "$attempt" -lt 60 ]; do
        if adb get-state >/dev/null 2>&1 &&
           adb shell '/data/local/tmp/p90-dcow /data/local/tmp/p90-dumpstate-original /system/bin/dumpstate' >/dev/null 2>&1; then
            return 0
        fi
        attempt=$((attempt + 1))
        sleep 1
    done
    return 1
}

cleanup() {
    if [ "$restore_needed" = 1 ]; then
        restore_dumpstate || {
            echo 'ERROR: USB did not return; dumpstate restoration remains required' >&2
            echo 'Run: adb shell /data/local/tmp/p90-dcow /data/local/tmp/p90-dumpstate-original /system/bin/dumpstate' >&2
        }
    fi
    rm -rf "$temporary_dir"
}
trap cleanup EXIT HUP INT TERM

adb pull /system/bin/dumpstate "$temporary_dir/dumpstate-before" >/dev/null
test "$(shasum -a 256 "$temporary_dir/dumpstate-before" | awk '{print $1}')" = "$factory_sha"
adb push "$command_file" /data/local/tmp/p90-chroot-command.txt >/dev/null
adb shell 'rm -f /data/local/tmp/p90-chroot-command.done /data/local/tmp/p90-chroot-command.log'
adb shell '/data/local/tmp/p90-dcow /data/local/tmp/p90-chroot-command /system/bin/dumpstate --no-pad' >/dev/null
restore_needed=1
adb shell 'setprop ctl.start dumpstate'

i=0
while [ "$i" -lt 30 ]; do
    done_text=$(adb shell 'cat /data/local/tmp/p90-chroot-command.done 2>/dev/null' | tr -d '\r' || true)
    if echo "$done_text" | grep -q '^wait-status='; then
        break
    fi
    i=$((i + 1))
    sleep 1
done
test "$i" -lt 30
adb shell 'cat /data/local/tmp/p90-chroot-command.log'

restore_dumpstate
restore_needed=0
adb pull /system/bin/dumpstate "$temporary_dir/dumpstate-after" >/dev/null
test "$(shasum -a 256 "$temporary_dir/dumpstate-after" | awk '{print $1}')" = "$factory_sha"
