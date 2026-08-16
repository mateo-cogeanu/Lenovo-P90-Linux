#!/bin/sh
set -eu

echo "Disabled after 2026-08-10 diagnosis: do not rerun the temporary executable-replacement display path." >&2
echo "See docs/12-native-debian-and-phosh-under-stock-kernel.md." >&2
exit 2

factory_dumpstate_sha=1b34af36f7bcc7344165474c405fbe64c983684ab30a23cda31871289a5f8e5c
launcher_sha=f73b10356c3d7cceb2b7d656530f2cb77019aa0bc59a5e9f910668abfaa8858f
launcher=work/p90-native-port/dirtycow-root/libs/x86/debian-xorg-phosh-probe
phone_launcher=/data/local/tmp/p90-debian-xorg-phosh-probe
phone_log=/data/local/tmp/p90-debian-xorg-phosh.txt
approval=/data/local/tmp/p90-freeze-surfaceflinger-approved
evidence_dir=work/p90-native-port/live-test/2026-08-10-native-debian
evidence=$evidence_dir/xorg-phoc-phosh-exclusive-guarded.txt
temporary_dir=$(mktemp -d /tmp/p90-exclusive-phosh.XXXXXX)
launcher_started=0
recovery_pid=0

cleanup() {
    # These operations are deliberately redundant with the independent timer.
    adb shell 'if [ -f /data/local/tmp/p90-freeze-surfaceflinger-approved ]; then setprop ctl.stop surfaceflinger; sleep 1; setprop ctl.start surfaceflinger; fi; sleep 2; input keyevent 224' \
        >/dev/null 2>&1 || true
    if [ "$launcher_started" = 1 ]; then
        adb shell '/data/local/tmp/p90-dcow /data/local/tmp/p90-dumpstate-original /system/bin/dumpstate' \
            >/dev/null 2>&1 || true
    fi
    if [ "$recovery_pid" -gt 0 ]; then
        wait "$recovery_pid" >/dev/null 2>&1 || true
    fi
    adb shell 'rm -f /data/local/tmp/p90-exclusive-watchdog-proof.txt /data/local/tmp/p90-exclusive-watchdog-fired.txt /data/local/tmp/p90-debian-xorg-phosh-probe /data/local/tmp/p90-freeze-surfaceflinger-approved' \
        >/dev/null 2>&1 || true
    rm -rf "$temporary_dir"
}
trap cleanup EXIT HUP INT TERM

hash_file() {
    shasum -a 256 "$1" | awk '{print $1}'
}

wait_for_log() {
    pattern=$1
    attempts=$2
    i=0
    while [ "$i" -lt "$attempts" ]; do
        if adb shell "cat $phone_log 2>/dev/null" | tr -d '\r' | grep -q "$pattern"; then
            return 0
        fi
        i=$((i + 1))
        sleep 1
    done
    return 1
}

mkdir -p "$evidence_dir"
test "$(hash_file "$launcher")" = "$launcher_sha"
test "$(adb devices | awk 'NR > 1 && $2 == "device" { count++ } END { print count + 0 }')" = 1

adb pull /system/bin/dumpstate "$temporary_dir/dumpstate" >/dev/null
test "$(hash_file "$temporary_dir/dumpstate")" = "$factory_dumpstate_sha"
adb pull /data/local/tmp/p90-dumpstate-original "$temporary_dir/dumpstate-original" >/dev/null
test "$(hash_file "$temporary_dir/dumpstate-original")" = "$factory_dumpstate_sha"
test "$(adb shell 'cat /sys/kernel/kexec_loaded' | tr -d '\r')" = 0
test "$(adb shell 'cat /sys/kernel/kexec_crash_loaded' | tr -d '\r')" = 0

adb push "$launcher" "$phone_launcher" >/dev/null
adb shell "rm -f $phone_log"

# Prove the independent, host-held ADB recovery channel before relying on it.
# Unlike a detached Android shell child, this remote command remains attached to
# a live host adb process and therefore is not reaped when its parent shell ends.
adb shell 'sleep 2; echo survived' > "$temporary_dir/watchdog-proof.txt" &
proof_pid=$!
sleep 4
wait "$proof_pid"
test "$(tr -d '\r' < "$temporary_dir/watchdog-proof.txt")" = survived
adb shell "echo approved > $approval"

# This command is held by a separate host adb process and runs outside the
# dumpstate service group. Even if init kills the display launcher,
# SurfaceFlinger is restarted and Android is woken after at most 15 seconds.
adb shell 'rm -f /data/local/tmp/p90-exclusive-watchdog-fired.txt; sleep 15; if [ -f /data/local/tmp/p90-freeze-surfaceflinger-approved ]; then setprop ctl.stop surfaceflinger; sleep 1; setprop ctl.start surfaceflinger; echo fired; else echo not-needed; fi > /data/local/tmp/p90-exclusive-watchdog-fired.txt; sleep 2; input keyevent 224' \
    > "$temporary_dir/watchdog-output.txt" 2>&1 &
recovery_pid=$!

adb shell "/data/local/tmp/p90-dcow $phone_launcher /system/bin/dumpstate --no-pad" >/dev/null
launcher_started=1
adb shell 'setprop ctl.start dumpstate'
wait_for_log 'phoc-phosh-pid=' 12

# Restore the complete factory service before changing display ownership. The
# already-running launcher is resident in memory.
adb shell '/data/local/tmp/p90-dcow /data/local/tmp/p90-dumpstate-original /system/bin/dumpstate' >/dev/null
adb pull /system/bin/dumpstate "$temporary_dir/dumpstate-restored" >/dev/null
test "$(hash_file "$temporary_dir/dumpstate-restored")" = "$factory_dumpstate_sha"

wait_for_log 'Phosh ready after' 8
wait_for_log 'surfaceflinger-freeze-result=0' 8
sleep 8
wait_for_log 'surfaceflinger-continue-result=0' 4
adb shell 'setprop ctl.start surfaceflinger; sleep 2; input keyevent 224'

for i in 1 2 3 4 5 6 7 8 9 10 11 12; do
    state=$(adb shell 'getprop init.svc.dumpstate' | tr -d '\r')
    [ "$state" = stopped ] && break
    sleep 1
done
adb pull "$phone_log" "$evidence" >/dev/null

{
    echo "factory_dumpstate_sha=$factory_dumpstate_sha"
    echo "launcher_service=dumpstate"
    echo "surfaceflinger=$(adb shell 'getprop init.svc.surfaceflinger' | tr -d '\r')"
    echo "dumpstate=$(adb shell 'getprop init.svc.dumpstate' | tr -d '\r')"
    echo "watchdog=$(adb shell 'cat /data/local/tmp/p90-exclusive-watchdog-fired.txt 2>/dev/null' | tr -d '\r')"
    echo "kexec_loaded=$(adb shell 'cat /sys/kernel/kexec_loaded' | tr -d '\r')"
    echo "kexec_crash_loaded=$(adb shell 'cat /sys/kernel/kexec_crash_loaded' | tr -d '\r')"
} >> "$evidence"

test "$(adb shell 'getprop init.svc.surfaceflinger' | tr -d '\r')" = running
test "$(adb shell 'cat /sys/kernel/kexec_loaded' | tr -d '\r')" = 0
test "$(adb shell 'cat /sys/kernel/kexec_crash_loaded' | tr -d '\r')" = 0
adb pull /system/bin/dumpstate "$temporary_dir/dumpstate-final" >/dev/null
test "$(hash_file "$temporary_dir/dumpstate-final")" = "$factory_dumpstate_sha"
echo "Guarded exclusive Phosh evidence written to $evidence"
