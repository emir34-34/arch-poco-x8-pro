#!/bin/sh

# These things can be done only of rootfs is writable i.e. is ramdisk.
if [ -w $LXC_ROOTFS_PATH ]; then
    rm $LXC_ROOTFS_PATH/sbin/adbd

    sed -i "/mount_all /d" $LXC_ROOTFS_PATH/init.*.rc
    sed -i "/swapon_all /d" $LXC_ROOTFS_PATH/init.*.rc
    sed -i "/on nonencrypted/d" $LXC_ROOTFS_PATH/init.rc

    # Config snippet scripts
    run-parts /var/lib/lxc/android/pre-start.d || true
fi

# Make sure bind-mount-points are available.
mkdir -p /dev/__properties__ /dev/socket

