#!/system/bin/sh

if [ -e /data/local/tmp/p90-debian-autoboot-disabled ]; then
    exit 0
fi

exec /data/local/tmp/p90-debian-autoboot
