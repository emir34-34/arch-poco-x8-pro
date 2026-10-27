#!/bin/bash
# klee: the phone steps for the first Arch boot, in order. The phone starts in
# HyperOS (slot _a) with USB debugging, PC on Wi-Fi (no tethering needed).
#   home-steps.sh update      packages built after the copy -> the build rootfs
#                             and the Arch root inside ut_data (pacman --root)
#   home-steps.sh probe       fastboot: init_boot_b = probe-android initrd,
#                             slot _b; the phone must come back to fastboot
#   home-steps.sh flash       fastboot: init_boot_b = the real initrd, slot _b
#   home-steps.sh back        fastboot: slot _a (HyperOS)
# boot_b already holds our kernel (arch/out/klee_boot.img). Every fastboot
# step needs the user's OK first.
set -euo pipefail
A="$HOME/klee-ut/arch"
H="$A/phone"
R="$A/repo"
UPDATES="klee-container-1-5-any.pkg.tar.zst bluebinder-1.0.19-3-aarch64.pkg.tar.zst"

case "${1:-}" in
update)
	adb get-state | grep -q device || { echo "no adb device (HyperOS)"; exit 1; }
	[ "$(adb shell getprop ro.boot.slot_suffix | tr -d '\r')" = _a ] || { echo "not on slot _a"; exit 1; }
	for p in $UPDATES; do "$H/apush" "$R/$p"; done
	adb push "$HOME/klee-ut/ztos-klee/ramdisk-overlay/sbin/klee-pinmap" /data/local/tmp/ >/dev/null
	adb shell "su -c 'cp /data/local/tmp/klee-pinmap /data/klee_arch/rootfs/root/; cp /data/klee_ztos/ut_data.map /data/klee_arch/rootfs/root/'"
	"$H/arundev" <<EOF
set -e
cd /root/pkgs
pacman -U --noconfirm $UPDATES
export DM_DISABLE_UDEV=1
DEV=\$(readlink -f /dev/block/by-name/userdata)
KLEE_USERDATA=\$DEV sh /root/klee-pinmap /root/ut_data.map
dmsetup mknodes ut_data
mkdir -p /mnt/ut
mount -t ext4 -o noatime /dev/mapper/ut_data /mnt/ut
trap 'cd /; umount /mnt/ut; dmsetup remove ut_data' EXIT
pacman --root /mnt/ut/halium-rootfs --dbpath /mnt/ut/halium-rootfs/var/lib/pacman \
	--hookdir /mnt/ut/halium-rootfs/etc/pacman.d/hooks -U --noconfirm $UPDATES
# leftovers of the build chroot's test binds
rmdir /mnt/ut/halium-rootfs/{apex,linkerconfig,odm,product,system,system_ext,vendor} 2>/dev/null || true
pacman --root /mnt/ut/halium-rootfs --dbpath /mnt/ut/halium-rootfs/var/lib/pacman -Q klee-container bluebinder
sync
EOF
	adb shell "su -c 'grep -c klee_arch /proc/mounts; ls /dev/mapper | grep -c ut_data'" || true
	;;
probe|flash)
	img="$A/out/klee_init_boot.img"; [ "$1" = probe ] && img="$A/out/klee_init_boot_probe-android.img"
	fastboot devices | grep -q . || { echo "phone not in fastboot"; exit 1; }
	fastboot flash init_boot_b "$img"
	fastboot set_active b
	fastboot reboot
	;;
back)
	fastboot devices | grep -q . || { echo "phone not in fastboot"; exit 1; }
	fastboot set_active a
	fastboot reboot
	;;
*)
	sed -n '2,12p' "$0"; exit 2 ;;
esac
