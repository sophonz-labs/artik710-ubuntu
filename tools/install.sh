#!/bin/sh
# Put a release on an ARTIK710. Run it as root on the board, from the stock
# system or from this one, in the directory with the release files. Without
# them, it first fetches the latest release to /root/artik710-ubuntu:
#   curl -fsSL https://github.com/sophonz-labs/artik710-ubuntu/releases/latest/download/install.sh | sh
#   (with a mode: ... | sh -s -- --erase)
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

case ${1:-} in "" | --erase | --restore) ;; *)
	# the comment at the top is the help, when this is a file
	case $0 in *install.sh) sed -n '2,/^set -e/p' "$0" | sed '$d; s/^# \{0,1\}//' ;; esac
	echo "usage: install.sh [--erase | --restore]" >&2
	exit 1 ;;
esac

# The command from the README, pasted on another machine, stops here.
[ -n "${ARTIK_BOOT_DEV:-}" ] || grep -q artik710 /proc/device-tree/compatible 2>/dev/null || {
	echo "This is not an ARTIK710." >&2
	exit 1
}

# Without the release files, as when it comes through a pipe: fetch them and go
# on with the copy that came with them. The shell reads this block as a whole,
# so that a download of this script that breaks off starts nothing.
case $0 in *install.sh) [ -e SHA256SUMS ] ;; *) false ;; esac || {
	u=https://github.com/sophonz-labs/artik710-ubuntu/releases/latest/download
	mkdir -p "$r/artik710-ubuntu"
	cd "$r/artik710-ubuntu"
	get() { if command -v curl >/dev/null; then curl -fL -o "$1" "$u/$1"; else wget -O "$1" "$u/$1"; fi; }
	# fails for a file that SHA256SUMS does not list, too
	ok() { grep " $f\$" SHA256SUMS | sha256sum -c --quiet; }
	get SHA256SUMS
	fs="Image uInitrd $dtb.dtb rootfs.ext4.xz install.sh"
	[ "${1:-}" != --restore ] || fs=install.sh
	for f in $fs; do
		# a file that is here already, from an earlier run, is not fetched again
		ok 2>/dev/null || { get "$f"; ok; }
	done
	exec sh install.sh "$@"
}

case ${1:-} in
"" | --erase) sha256sum -c SHA256SUMS ;;
--restore)
	ls "$r/stock-boot/Image" "$r/stock-boot/uInitrd" "$r/stock-boot/$dtb"-rev*.dtb >/dev/null 2>&1 || {
		echo "no stock kernel in $r/stock-boot: it was not saved here" >&2
		exit 1
	} ;;
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
	# Read-only again at the end: that empties the journal, which U-Boot does
	# not read, so that a reset without a shutdown finds the new files too.
	trap 'mount -o remount,ro "$m" || echo "$m is still writable: use reboot, not the reset button or the power switch" >&2' EXIT
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
	cp -p "$@" "$m/"
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

# The root filesystem first: if it does not fit, the board starts as before,
# and without a half-written file that fills its disk.
if [ -z "${1:-}" ]; then
	xz -dc rootfs.ext4.xz | dd of="$r/ubuntu-26.04.img" bs=1M conv=sparse status=none ||
		{ rm -f "$r/ubuntu-26.04.img"; exit 1; }
else
	cp rootfs.ext4.xz "$r/ubuntu-26.04.img.xz" || { rm -f "$r/ubuntu-26.04.img.xz"; exit 1; }
fi
sync	# now, so that the sync in put is short: no kernel is on the partition then

# Only the stock system has a device tree per board revision.
if [ -e "$m/$dtb-rev00.dtb" ]; then
	mkdir -p "$r/stock-boot"
	cp -a "$m/Image" "$m/uInitrd" "$m/$dtb"-rev*.dtb "$r/stock-boot/"
fi
# U-Boot takes the plain name once no file for a revision is left.
rm -f "$m/Image" "$m/uInitrd" "$m/$dtb"*.dtb
put Image uInitrd $dtb.dtb

if [ -z "${1:-}" ]; then
	echo "Reboot to start Ubuntu 26.04 from $r/ubuntu-26.04.img."
	echo "Login: artik, password artik. To go back: sh install.sh --restore"
else
	echo "Reboot to write Ubuntu 26.04 over the root partition. This takes a few"
	echo "minutes and EVERYTHING on that partition is lost. Last way out:"
	echo "sh install.sh --restore"
fi
