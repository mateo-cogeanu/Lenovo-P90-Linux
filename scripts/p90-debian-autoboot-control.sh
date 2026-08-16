#!/bin/sh
set -eu

marker=/data/local/tmp/p90-debian-autoboot-disabled

case ${1:-status} in
    status)
        adb get-state >/dev/null
        adb shell "if [ -e $marker ]; then echo autoboot=disabled; else echo autoboot=enabled; fi; \
            echo service=\$(getprop init.svc.flash_recovery); \
            echo surfaceflinger=\$(getprop init.svc.surfaceflinger); \
            ps | grep -E 'p90-debian-autoboot|Xorg|phoc|phosh' || true"
        ;;
    disable)
        adb shell "touch $marker"
        echo "Debian autoboot disabled for the next boot. The current session is unchanged."
        ;;
    enable)
        adb shell "rm -f $marker"
        echo "Debian autoboot enabled for the next boot."
        ;;
    disable-reboot)
        adb shell "touch $marker"
        adb reboot
        echo "Reboot requested with Debian autoboot disabled."
        ;;
    enable-reboot)
        adb shell "rm -f $marker"
        adb reboot
        echo "Reboot requested with Debian autoboot enabled."
        ;;
    *)
        echo "usage: $0 {status|disable|enable|disable-reboot|enable-reboot}" >&2
        exit 2
        ;;
esac
