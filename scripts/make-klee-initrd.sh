#!/bin/bash
# klee: init_boot.img for slot _b with our own initrd (arch/initrd/init):
# Arch Linux ARM's static busybox, dmsetup and e2fsck (+ their glibc libs from
# the sysroot), lp-map + klee-pinmap and the ut_data extents map.
set -euo pipefail
export LC_ALL=C LANG=C
A="$HOME/klee-ut/arch"
S="$A/sysroot"
R="$HOME/klee-ut/ztos-klee/ramdisk-overlay"
MK="$HOME/klee-ut/ubports-klee/workdir/downloads/android_system_tools_mkbootimg"
READELF="$A/xenv/bin/aarch64-readelf"
OUT="$A/out"
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
RD="$W/rd"
mkdir -p "$RD"/{bin,lib,etc/klee,dev,proc,sys,run,root}
ln -s lib "$RD/lib64"
install -m 755 "$A/initrd/init" "$RD/init"
install -m 755 "$S/usr/bin/busybox" "$RD/bin/busybox"
for b in dmsetup e2fsck; do install -m 755 "$S/usr/bin/$b" "$RD/bin/$b"; done
install -m 755 "$R/sbin/lp-map" "$R/sbin/klee-pinmap" "$RD/bin/"
# shared libraries, recursively from NEEDED
need() { "$READELF" -d "$1" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'; }
# until a pass adds nothing new
for i in 1 2 3 4 5; do
	for f in "$RD"/bin/dmsetup "$RD"/bin/e2fsck "$RD"/lib/*; do if [ -f "$f" ]; then need "$f"; fi; done | sort -u |
		while read -r l; do [ -e "$RD/lib/$l" ] || cp -L "$S/usr/lib/$l" "$RD/lib/$l"; done
done
cp -L "$S/usr/lib/ld-linux-aarch64.so.1" "$RD/lib/"
cp "$R/etc/klee/android_slot" "$R/etc/klee/ut_data.map" "$RD/etc/klee/"
sed -n 's/^blacklist /blacklist /p' "$R/etc/modprobe.d/klee-blacklist.conf" > "$RD/etc/klee/blacklist"
sh -n "$RD/init"; for f in lp-map klee-pinmap; do sh -n "$RD/bin/$f"; done
ls -la "$RD/lib" | awk '{print $5, $9}' | tail -n +4
(cd "$RD" && find . | sort | cpio -o -H newc -R 0:0 --quiet) | lz4 -l -12 -q > "$W/ramdisk.lz4"
mkdir -p "$OUT"
python3 "$MK/mkbootimg.py" --header_version 4 --ramdisk "$W/ramdisk.lz4" -o "$OUT/klee_init_boot.img"
[ "$(stat -c %s "$OUT/klee_init_boot.img")" -le 8388608 ] || { echo "init_boot > 8 MiB" >&2; exit 1; }
echo "ramdisk $(stat -c %s "$W/ramdisk.lz4") bytes (lz4)"
truncate -s 8388608 "$OUT/klee_init_boot.img"
(cd "$OUT" && sha256sum klee_init_boot.img)
