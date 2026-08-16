#!/system/bin/sh

OUT=/data/local/tmp/p90-post-kexec-reset.txt

{
    echo "identity:"
    id
    echo "properties:"
    getprop ro.boot.mode
    getprop ro.boot.bootreason
    getprop sys.boot.reason
    getprop ro.boot.bootloader
    echo "uptime:"
    cat /proc/uptime
    echo "command_line:"
    cat /proc/cmdline
    echo "kexec_state:"
    cat /sys/kernel/kexec_loaded /sys/kernel/kexec_crash_loaded 2>&1
    echo "watchdog_state:"
    ls -l /sys/class/misc/watchdog/disable 2>&1
    cat /sys/class/misc/watchdog/disable 2>&1
    echo "panic_settings:"
    cat /proc/sys/kernel/panic /proc/sys/kernel/panic_on_oops 2>&1
    echo "dmesg_reset_lines:"
    dmesg 2>&1 | grep -i -E 'kexec|watchdog|panic|reset|reboot|boot.reason|bootreason|ipanic|crash'
    echo "persistent_log_files:"
    find /logs -maxdepth 4 -type f -exec ls -l {} \; 2>&1
    echo "dropbox_files:"
    find /data/system/dropbox -maxdepth 1 -type f -exec ls -l {} \; 2>&1
} > "$OUT" 2>&1

chmod 0644 "$OUT"
exit 0
