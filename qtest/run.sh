#!/bin/bash
# klee: QEMU test of our initrd (arch/initrd/init) with our kernel, no root
# needed. A small fake phone disk stands in for the real one:
#   GPT "super"    : Android LP metadata, slot _a, with vendor_a (ext4)
#   GPT "userdata" : contains ut_data (ext4) split in two extents at odd
#                    places, mapped by an extents map like the phone's, with
#                    the KLEEZTOS1 trailer in its last block
#   ut_data        : /halium-rootfs (busybox + a fake systemd that reports and
#                    powers off) and the Android rootfs image
# vendor_boot's ramdisk is faked with QEMU's virtio modules + modules.load.
# Pass: the serial log shows "QTEST: ARCH ROOT REACHED".
set -euo pipefail
export LC_ALL=C
A="$HOME/klee-ut/arch"
Q="$A/qtest"
W="$Q/work"
K="$HOME/klee-ut/kout-klee"
rm -rf "$W"; mkdir -p "$W"
cd "$W"

# --- fake Android rootfs image (has /system) and vendor_a ---
mkdir -p ar/system/etc ar/vendor ar/data ar/cache ar/odm ar/product ar/system_ext
echo "ro.build.version.sdk=34" > ar/system/build.prop
mkfs.ext4 -q -F -L android -d ar android-rootfs.img 16M
mkdir -p ven/etc; echo "vendor of the QEMU test" > ven/etc/qtest
mkfs.ext4 -q -F -L vendor -d ven vendor_a.img 8M

# --- ut_data with the Arch root stand-in ---
mkdir -p ud/halium-rootfs/{bin,usr/lib/systemd,var/lib/lxc/android/rootfs,android,userdata,proc,sys,dev,run,tmp}
cp "$A/sysroot/usr/bin/busybox" ud/halium-rootfs/bin/busybox
cat > ud/halium-rootfs/usr/lib/systemd/systemd <<'EOF'
#!/bin/busybox sh
/bin/busybox echo "QTEST: ARCH ROOT REACHED (pid $$)" > /dev/kmsg
/bin/busybox cat /proc/mounts > /dev/kmsg
/bin/busybox echo "QTEST: pid_max $(/bin/busybox cat /proc/sys/kernel/pid_max)" > /dev/kmsg
/bin/busybox poweroff -f
EOF
chmod 755 ud/halium-rootfs/usr/lib/systemd/systemd
cp android-rootfs.img ud/halium-rootfs/var/lib/lxc/android/android-rootfs.img
MIB=160
UUID=94a86f74-0000-4000-8000-00000000c0de
mkfs.ext4 -q -F -U $UUID -L ut_data -b 4096 -d ud ut_data.img $((MIB * 256 - 1))
truncate -s ${MIB}M ut_data.img
printf 'KLEEZTOS1 %s' $UUID | dd of=ut_data.img bs=4096 seek=$((MIB * 256 - 1)) conv=notrunc status=none

# --- the disk: GPT super (8 MiB @1 MiB) + userdata (420 MiB) ---
SUPER_START=2048 SUPER_SECT=$((32 * 2048)) UD_START=$((2048 + 32 * 2048)) UD_SECT=$((420 * 2048))
truncate -s $(( (UD_START + UD_SECT + 2048) * 512 )) disk.img
sfdisk -q disk.img <<EOF
label: gpt
start=$SUPER_START, size=$SUPER_SECT, name=super
start=$UD_START, size=$UD_SECT, name=userdata
EOF
lpmake --metadata-size 65536 --super-name super --metadata-slots 2 \
	--device super:$((SUPER_SECT * 512)) --group main_a:$((24 * 1024 * 1024)) \
	--partition vendor_a:readonly:$(stat -c %s vendor_a.img):main_a \
	--force-full-image --output super.img >/dev/null 2>&1
# (lpmake --image wants sparse images) put vendor_a where lpdump says it is
VSTART=$(lpdump super.img 2>/dev/null | sed -n 's/^super: \([0-9]*\) \.\. [0-9]*: vendor_a .*/\1/p')
dd if=vendor_a.img of=super.img bs=512 seek=$VSTART conv=notrunc status=none
dd if=super.img of=disk.img bs=512 seek=$SUPER_START conv=notrunc status=none
# ut_data in userdata as two extents, the second half first (like a real
# fragmented file): logical [0,H) at 40 MiB, [H,end) at 240 MiB
H=$((MIB * 2048 / 2)); TOT=$((MIB * 2048))
dd if=ut_data.img of=disk.img bs=512 skip=0 count=$H seek=$((UD_START + 40 * 2048)) conv=notrunc status=none
dd if=ut_data.img of=disk.img bs=512 skip=$H count=$((TOT - H)) seek=$((UD_START + 240 * 2048)) conv=notrunc status=none
printf 'uuid=%s\nsectors=%s\n0 %s %s\n%s %s %s\n' $UUID $TOT $H $((40 * 2048)) $H $((TOT - H)) $((240 * 2048)) > ut_data.map

# --- ramdisks: fake vendor_boot (virtio modules) + our initrd ---
mkdir -p vr/lib/modules vr/first_stage_ramdisk
for m in virtio_pci_legacy_dev virtio_pci_modern_dev virtio_pci virtio_blk; do
	cp "$(find "$K/drivers" -name "$m.ko" | head -1)" vr/lib/modules/
	echo "$m.ko" >> vr/lib/modules/modules.load
done
"$A/xenv/bin/aarch64-strip" --strip-debug vr/lib/modules/*.ko
(cd vr && find . | sort | cpio -o -H newc -R 0:0 --quiet) | lz4 -l -12 -q > vendor_ramdisk.lz4
MAP="$W/ut_data.map" RAMDISK_OUT="$W/init_ramdisk.lz4" "$A/make-klee-initrd.sh" >/dev/null
cat vendor_ramdisk.lz4 init_ramdisk.lz4 > initrd.img

cp "$K/arch/arm64/boot/Image" Image
timeout "${QEMU_TIMEOUT:-600}" "$HOME/klee-ut/qemu/usr/bin/qemu-system-aarch64" \
	-machine virt,gic-version=3 -cpu max -smp 4 -m 2048 -nographic -no-reboot -nic none \
	-kernel Image -initrd initrd.img \
	-append "console=ttyAMA0 printk.devkmsg=on androidboot.force_normal_boot=1 loglevel=6" \
	-drive if=none,file=disk.img,format=raw,id=hd -device virtio-blk-pci,drive=hd,romfile= \
	> serial.log 2>&1 || true
grep -aE "klee-init|QTEST|klee-pinmap|lp-map|Kernel panic|Attempted to kill" serial.log | sed 's/^\[ *[0-9.]*\] //'
grep -aq "QTEST: ARCH ROOT REACHED" serial.log && echo "=== PASS" || echo "=== FAIL (see $W/serial.log)"
