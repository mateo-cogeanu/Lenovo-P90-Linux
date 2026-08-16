#!/system/bin/sh

if [ -e /data/local/tmp/p90-debian-autoboot-disabled ]; then
    exit 0
fi

exec /system/bin/p90-debian-autoboot-v34
