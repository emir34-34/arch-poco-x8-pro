#!/bin/sh

# Doing a part of Android's first stage init. They have to be done in the mount
# hook or mounting things can clober what we're doing.

mknod -m 666 "${LXC_ROOTFS_MOUNT}/dev/null" c 1 3
mknod -m 600 "${LXC_ROOTFS_MOUNT}/dev/kmsg" c 1 11
mknod -m 666 "${LXC_ROOTFS_MOUNT}/dev/random" c 1 8
mknod -m 666 "${LXC_ROOTFS_MOUNT}/dev/urandom" c 1 9

# Don't expose the raw commandline to unprivileged processes.
chmod 440 "${LXC_ROOTFS_MOUNT}/proc/cmdline"

# Here we're doing the opposite of Android 7's init, but apparently
# Android 9 doesn't have /socket anymore, so save a bind mount.
if [ -w "${LXC_ROOTFS_MOUNT}" ]; then
    ln -s /dev/socket "${LXC_ROOTFS_MOUNT}/socket"
fi
