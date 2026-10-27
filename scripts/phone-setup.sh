#!/bin/bash
# klee: run INSIDE the Arch build rootfs on the phone (arun) once the phone is
# back: install the PC-built packages from /root/pkgs (apush them first) and
# the Arch Linux ARM packages they need, then the basic system settings.
set -e
pacman -S --noconfirm --needed dbus-glib mobile-broadband-provider-info bluez bluez-libs \
	pulseaudio alsa-lib libevdev openssh networkmanager python
cd /root/pkgs
pacman -U --noconfirm linux-klee-*.zst libglibutil-*.zst libgbinder-1*.zst libgbinder-radio-*.zst libmce-glib-*.zst \
	libhybris-*.zst klee-container-*.zst klee-base-*.zst ofono-sailfish-*.zst ofono-binder-plugin-*.zst \
	bluebinder-*.zst audiosystem-passthrough-*.zst pulseaudio-modules-droid-1*.zst pulseaudio-modules-droid-hidl-*.zst
echo klee > /etc/hostname
ln -sf /usr/share/zoneinfo/Europe/Istanbul /etc/localtime
sed -i 's/^#tr_TR.UTF-8/tr_TR.UTF-8/; s/^#en_US.UTF-8/en_US.UTF-8/' /etc/locale.gen && locale-gen
echo LANG=tr_TR.UTF-8 > /etc/locale.conf
systemctl enable ofono.service bluebinder.service bluetooth.service
echo SETUP_DONE
