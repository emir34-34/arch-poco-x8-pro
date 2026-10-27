#!/system/bin/sh
# like ach.sh, but with the phone's real /dev (block devices, device-mapper)
R=/data/klee_arch/rootfs
exec unshare -m sh -c '
/data/adb/ksu/bin/busybox mount --make-rprivate / || exit 99
R=/data/klee_arch/rootfs
mount -t proc proc $R/proc
mount -t sysfs sys $R/sys
/data/adb/ksu/bin/busybox mount --rbind /dev $R/dev
mount -t tmpfs tmpfs $R/tmp
chroot $R /usr/bin/env -i HOME=/root TERM=xterm LANG=C.UTF-8 PATH=/usr/local/sbin:/usr/local/bin:/usr/bin /bin/bash -c "$0"
rc=$?
for p in /proc/[0-9]*; do case "$(readlink $p/root)" in $R*) kill ${p#/proc/} 2>/dev/null;; esac; done
exit $rc
' "$*"
