#!/system/bin/sh
# run a command in the Arch build rootfs, in a PRIVATE mount namespace
# (toybox mount cannot change propagation: busybox does).
# Mounts vanish with the process; nothing to umount by hand.
R=/data/klee_arch/rootfs
exec unshare -m sh -c '
/data/adb/ksu/bin/busybox mount --make-rprivate / || exit 99
R='$R'
mount -t proc proc $R/proc
mount -t sysfs sys $R/sys
mount -t tmpfs -o mode=755 dev $R/dev
mknod -m 666 $R/dev/null c 1 3; mknod -m 666 $R/dev/zero c 1 5; mknod -m 666 $R/dev/full c 1 7
mknod -m 666 $R/dev/random c 1 8; mknod -m 666 $R/dev/urandom c 1 9; mknod -m 666 $R/dev/tty c 5 0
mkdir -p $R/dev/pts $R/dev/shm; mount -t devpts -o newinstance,ptmxmode=666 devpts $R/dev/pts; ln -sf pts/ptmx $R/dev/ptmx
mount -t tmpfs tmpfs $R/dev/shm; mount -t tmpfs tmpfs $R/tmp
ln -sf /proc/self/fd $R/dev/fd
chroot $R /usr/bin/env -i HOME=/root TERM=xterm LANG=C.UTF-8 PATH=/usr/local/sbin:/usr/local/bin:/usr/bin /bin/bash -c "$0"
rc=$?
# no daemon may outlive the job (a gpg-agent held the mounts once)
for p in /proc/[0-9]*; do case "$(readlink $p/root)" in $R*) kill ${p#/proc/} 2>/dev/null;; esac; done
exit $rc
' "$*"
