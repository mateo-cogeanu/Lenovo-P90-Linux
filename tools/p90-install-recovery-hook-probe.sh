#!/system/bin/sh

out=/data/local/tmp/p90-install-recovery-hook-probe.txt
{
    echo "hook-ran=1"
    id
    echo -n "context="
    cat /proc/self/attr/current
    echo "boot_completed=$(getprop sys.boot_completed)"
    echo "surfaceflinger=$(getprop init.svc.surfaceflinger)"
} >"$out" 2>&1
chmod 0644 "$out"
