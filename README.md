# Arch Linux for the POCO X8 Pro (klee)

Arch Linux ARM on the Xiaomi POCO X8 Pro (codename **klee**, model 2511FRT34C,
MediaTek MT6899, Mali-G720 MC8), dual boot next to HyperOS, with our own
kernel, our own initrd and (coming) our own user interface. Nothing from
Ubuntu: this replaces the earlier
[Ubuntu Touch + ztOS port](https://github.com/emir34-34/ubuntu-touch-poco-x8-pro).

> **Work in progress.** Everything here is built and packaged, but this Arch
> system has **not been booted on the phone yet**. Don't flash anything from
> here unless you know how to get back (fastboot, slot `_a`).

## How it fits together

| Part | What | Where |
|---|---|---|
| Kernel | Google `android15-6.6` at 6.6.143 + the Halium base (binder, SysV IPC, GKI module quirks), **no Ubuntu AppArmor**, version `-klee`. KMI identical to the working kernel (0 CRC changes), so HyperOS's vendor modules load. | `kernel/` (patches + `klee.config`), flashed to `boot_b` |
| Initrd | Our own busybox script, 3.7 MB (Ubuntu's was 12 MB): vendor modules, super (Android slot `_a`), the pinned `ut_data` file, Arch root, Android container root, `switch_root`. Recovery (OrangeFox) is handed over untouched. | `initrd/init`, `scripts/make-klee-initrd.sh`, flashed to `init_boot_b` |
| Root filesystem | Arch Linux ARM in `/halium-rootfs` inside `ut_data`, a pinned file in HyperOS's userdata (no repartitioning) | |
| Android side | Halium 14 (LineageOS based) system image in an LXC container, with HyperOS's vendor partitions under it. Linux talks to the HALs over binder or loads them through libhybris. | `pkgbuilds/klee-container`, `pkgbuilds/halium-android-rootfs` |

HyperOS stays on slot `_a` and is never written.

## Packages

All cross-built on a PC (x86_64 Arch) for aarch64 with `scripts/xmakepkg`
(makepkg + clang/lld against a copy of the phone's `/usr`), then installed on
the phone with `pacman -U`. Versions match what worked on this phone under
Ubuntu Touch 24.04, so nothing changes under the hardware.

| Package | Version | For |
|---|---|---|
| `linux-klee` | 6.6.143 | the kernel's own modules (e.g. `hci_vhci`) |
| `libhybris` | 0cedf90 + upstream `pthread_cond_clockwait` hook | Mali GPU, Android HALs from glibc programs |
| `klee-hybris-runtime` | | `libtls-padding` + `libklee_free` preloads |
| `libglibutil`, `libgbinder`, `libgbinder-radio`, `libmce-glib` | 1.0.79, 1.1.45, 1.6.1, 1.1.0 | binder IPC |
| `ofono-sailfish`, `ofono-binder-plugin` | 1.29+git12, 1.1.25 | calls, SMS, mobile data (AIDL radio HAL, two SIM slots; ISIM logical channels off, the MediaTek modem refuses them) |
| `bluebinder` | 1.0.19 | Bluetooth HAL → `/dev/vhci` |
| `pulseaudio-modules-droid`, `-droid-hidl`, `audiosystem-passthrough` | 14.2.106, 1.5.0, 1.3.1 | audio HAL in PulseAudio 17 (`pulsecore-headers` makes the build possible) |
| `klee-container` | | Android container setup, MediaTek udev rules, **SELinux label guard** |
| `klee-base` | | `pid_max` 32768, USB network + SSH, Wi-Fi power-on, brightness, power key |

### Phone-specific traps kept in the packages

- **pid_max must stay at 32768**: MediaTek's kernel protection (MKP) resets
  the phone when a larger pid appears.
- **IMEI safety**: the Android container runs without SELinux, so modem files
  it rewrites lose their labels and HyperOS's modem then fails (no IMEI).
  `klee-selinux-guard` relabels them; `lxc-android-config.service` cannot
  start without it.
- `gbinder.conf` `ApiLevel = 34`: with the default the Android 16 vendor HALs
  answer every call with `BAD_TYPE`.

## Building

```sh
scripts/xmakepkg            # in a pkgbuilds/<name> directory
scripts/xinstall <pkg>      # unpack into the PC sysroot for later builds
scripts/xfetch <name>       # signed Arch Linux ARM package into the sysroot
scripts/make-klee-boot.sh   # boot.img (kernel), AVB test-key footer
scripts/make-klee-initrd.sh # init_boot.img (initrd)
```

Paths in `scripts/` point at the author's PC (`~/klee-ut/...`).
`klee-base` reads SSH keys from `/etc/klee/authorized_keys` (not in the repo).

## User interface

Our own, from scratch. Concept draft (Turkish): [docs/ui-konsept.md](docs/ui-konsept.md).

## License

Our scripts and packaging: GPL-2.0-or-later. Packaged software keeps its own
license (see each PKGBUILD). Kernel patches: GPL-2.0.
