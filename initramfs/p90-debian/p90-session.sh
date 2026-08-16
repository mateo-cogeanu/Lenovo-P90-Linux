#!/bin/sh
set -u

export HOME=/root
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
export XDG_RUNTIME_DIR=/run/user/0
LOG=/var/log/p90-native-boot.log

exec >>"$LOG" 2>&1
echo "p90-session: native Debian session starting at $(date -u 2>/dev/null || true)"

rm -f /tmp/.X11-unix/X0 /run/user/0/wayland-*
/usr/lib/xorg/Xorg :0 -config /run/p90-xorg.conf \
    -nolisten tcp -noreset -novtswitch -sharevts \
    -logfile /var/log/Xorg.p90-native.log -verbose 6 &
xorg_pid=$!

ready=0
i=0
while [ "$i" -lt 15 ]; do
    if [ -S /tmp/.X11-unix/X0 ]; then
        ready=1
        break
    fi
    kill -0 "$xorg_pid" 2>/dev/null || break
    sleep 1
    i=$((i + 1))
done

if [ "$ready" -ne 1 ]; then
    echo "p90-session: Xorg did not create its socket; opening rescue shell"
    exec /bin/busybox sh
fi

export DISPLAY=:0
export WLR_BACKENDS=x11
export WLR_X11_OUTPUTS=1
export WLR_RENDERER=pixman
export WLR_RENDERER_ALLOW_SOFTWARE=1
export WLR_LIBINPUT_NO_DEVICES=1
export G_MESSAGES_DEBUG=all

echo "p90-session: Xorg owns psbfb; starting Phoc and Phosh"
/usr/bin/dbus-run-session -- /usr/bin/phoc \
    -C /usr/share/phosh/phoc.ini -E /usr/libexec/phosh
status=$?
echo "p90-session: Phoc/Phosh exited with status $status"
kill "$xorg_pid" 2>/dev/null || true
wait "$xorg_pid" 2>/dev/null || true
exec /bin/busybox sh
