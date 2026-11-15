#!/bin/sh
# Put a release on an ARTIK710. Run it as root on the board, in the directory
# with the release files, from the stock system or from this one.
#
#   sh install.sh            try it: the new system runs from an image file on
#                            the stock root filesystem, which stays as it is
#   sh install.sh --erase    replace the stock system: at the next boot the
#                            image is written over the whole root partition
#   sh install.sh --restore  put the stock kernel back
#
# Kernel, device tree and initramfs go to the boot partition (eMMC p2); what the
# stock system had there is kept in /root/stock-boot. The second boot partition
# (p3), the bootloader and its environment are not touched.
#
# For tests/install-test.sh: ARTIK_BOOT_DEV and ARTIK_ROOT_DIR stand in for the
# boot partition and for /root.
set -e
cd "$(dirname "$0")"
dev=${ARTIK_BOOT_DEV:-/dev/mmcblk0p2}
# The system from the image has the stock root filesystem on /mnt.
r=/root
[ ! -e /mnt/root/ubuntu-26.04.img ] || r=/mnt/root
r=${ARTIK_ROOT_DIR:-$r}
dtb=s5p6818-artik710-raptor

case ${1:-} in
"" | --erase) sha256sum -c SHA256SUMS ;;
--restore) [ -d "$r/stock-boot" ] || { echo "no $r/stock-boot: the stock kernel was not saved here" >&2; exit 1; } ;;
*) sed -n '2,/^set -e/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 1 ;;
esac
if [ -z "${1:-}" ] && [ "$r" = /mnt/root ]; then
	echo "The system in $r/ubuntu-26.04.img is running. To replace it," >&2
	echo "run --restore, reboot into the stock system and install from there." >&2
	exit 1
fi

# The stock system has the boot partition on /boot, read-only.
m=$(awk -v d="$dev" '$1 == d { print $2; exit }' /proc/mounts)
if [ -n "$m" ]; then
	mount -o remount,rw "$m"
else
	m=$(mktemp -d)
	mount "$dev" "$m"
	trap 'umount "$m"; rmdir "$m"' EXIT
fi

# put FILE...: copy to the boot partition and check what is on the disk.
# The sync first: ext4 cannot reuse the blocks of removed files before it has
# committed, and Linux 7.2 leaves zeros in a new file when the partition runs
# out of blocks under it. Nothing reports that, and U-Boot starts such a kernel.
put() {
	sync
	cp -a "$@" "$m/"
	sync
	echo 3 > /proc/sys/vm/drop_caches
	for f; do
		cmp "$f" "$m/${f##*/}" || {
			echo "The boot partition did not take ${f##*/}: run this again before any reboot." >&2
			exit 1
		}
	done
}

if [ "${1:-}" = --restore ]; then
	rm -f "$m/Image" "$m/uInitrd" "$m/$dtb"*.dtb "$r/ubuntu-26.04.img.xz"
	put "$r/stock-boot"/*
	echo "The stock system starts at the next boot."
	echo "$r/ubuntu-26.04.img and $r/stock-boot are not needed any more then."
	exit
fi

# Only the stock system has a device tree per board revision.
if [ -e "$m/$dtb-rev00.dtb" ]; then
	mkdir -p "$r/stock-boot"
	cp -a "$m/Image" "$m/uInitrd" "$m/$dtb"-rev*.dtb "$r/stock-boot/"
fi
# U-Boot takes the plain name once no file for a revision is left.
rm -f "$m/Image" "$m/uInitrd" "$m/$dtb"*.dtb
put Image uInitrd $dtb.dtb

if [ -z "${1:-}" ]; then
	xz -dc rootfs.ext4.xz | dd of="$r/ubuntu-26.04.img" bs=1M conv=sparse status=none
	sync
	echo "Reboot to start Ubuntu 26.04 from $r/ubuntu-26.04.img."
	echo "Login: artik, password artik. To go back: sh install.sh --restore"
else
	cp rootfs.ext4.xz "$r/ubuntu-26.04.img.xz"
	sync
	echo "Reboot to write Ubuntu 26.04 over the root partition. This takes a few"
	echo "minutes and EVERYTHING on that partition is lost. Last way out:"
	echo "sh install.sh --restore"
fi
