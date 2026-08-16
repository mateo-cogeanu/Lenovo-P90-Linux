#!/system/bin/sh

OUT=/data/local/tmp/p90-crash-readonly.txt

{
    echo "uid_and_groups:"
    id
    echo "kernel_release:"
    cat /proc/sys/kernel/osrelease
    echo "command_line:"
    cat /proc/cmdline
    echo "memory_map:"
    cat /proc/iomem
    echo "kexec_interfaces:"
    ls -l /sys/kernel/kexec_crash_loaded /sys/kernel/kexec_loaded 2>&1
    cat /sys/kernel/kexec_crash_loaded /sys/kernel/kexec_loaded 2>&1
    echo "boot_log_crash_lines:"
    dmesg 2>&1 | grep -i -E 'crash|kexec|reserve'
} > "$OUT" 2>&1

chmod 0644 "$OUT"
exit 0
