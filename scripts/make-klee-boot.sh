#!/bin/bash
# klee: boot.img for slot _b with our own kernel (kernel-klee -> kout-klee),
# made exactly like ztos-klee/make-ztos-images.sh made ztos_boot.img: header
# v4, no os_version/patch level in the header, AVB hash footer signed with the
# test key and the props HyperOS's LK reads (never newer than HyperOS, or the
# TEE's rollback level would move and hurt HyperOS's keystore).
set -euo pipefail
export LC_ALL=C LANG=C
DL="$HOME/klee-ut/ubports-klee/workdir/downloads"
MK="$DL/android_system_tools_mkbootimg"
AVB="$DL/avb/avbtool.py"
KIMG="$HOME/klee-ut/kout-klee/arch/arm64/boot/Image.lz4"
OUT="$HOME/klee-ut/arch/out"
mkdir -p "$OUT"
python3 "$MK/mkbootimg.py" --header_version 4 --kernel "$KIMG" --cmdline "printk.devkmsg=on" -o "$OUT/klee_boot.img"
[ "$(stat -c %s "$OUT/klee_boot.img")" -le 67108864 ] || { echo "boot > 64 MiB" >&2; exit 1; }
python3 "$AVB" add_hash_footer --image "$OUT/klee_boot.img" --partition_name boot \
	--partition_size 67108864 --algorithm SHA256_RSA4096 --key "$DL/avb/test/data/testkey_rsa4096.pem" \
	--prop com.android.build.boot.os_version:15 --prop com.android.build.boot.security_patch:2026-08-01
(cd "$OUT" && sha256sum klee_boot.img)
