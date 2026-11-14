#!/bin/sh
# Run tools/install.sh against an image file in place of the boot partition.
# As root, on any Linux:  sh tests/install-test.sh
set -e
t=$(mktemp -d)
trap 'umount "$t/m" 2>/dev/null || :; rm -rf "$t"' EXIT
dtb=s5p6818-artik710-raptor
boot() { mount -o loop "$t/boot.img" "$t/m"; (cd "$t/m" && "$@"); umount "$t/m"; }
run() { ARTIK_BOOT_DEV=$t/boot.img ARTIK_ROOT_DIR=$t/root sh "$t/rel/install.sh" "$@" >/dev/null; }

mkdir "$t/m" "$t/root" "$t/rel"
truncate -s 32M "$t/boot.img"
mke2fs -q -t ext4 "$t/boot.img"
boot sh -c "echo stock > Image; echo stock > uInitrd; echo stock > $dtb-rev00.dtb; echo other > $dtb-rev03.dtb; echo keep > explorer.dtb"

cp "$(dirname "$0")/../tools/install.sh" "$t/rel/"
cd "$t/rel"
echo new > Image; echo new > uInitrd; echo new > $dtb.dtb
head -c 3000000 /dev/urandom > rootfs.ext4; truncate -s 8M rootfs.ext4
want=$(sha256sum < rootfs.ext4)
xz rootfs.ext4
sha256sum Image uInitrd $dtb.dtb rootfs.ext4.xz install.sh > SHA256SUMS

run		# try
boot sh -c "grep -q new Image && grep -q new uInitrd && grep -q new $dtb.dtb && grep -q keep explorer.dtb && ! ls $dtb-rev* 2>/dev/null"
grep -q stock "$t/root/stock-boot/Image" && grep -q other "$t/root/stock-boot/$dtb-rev03.dtb"
[ "$(sha256sum < "$t/root/ubuntu-26.04.img")" = "$want" ]

run --erase	# the kernel is the new one already: the saved stock files stay
cmp rootfs.ext4.xz "$t/root/ubuntu-26.04.img.xz"
grep -q stock "$t/root/stock-boot/Image"

run --restore
boot sh -c "grep -q stock Image && grep -q stock uInitrd && grep -q other $dtb-rev03.dtb && [ ! -e $dtb.dtb ]"
[ ! -e "$t/root/ubuntu-26.04.img.xz" ]

echo broken > Image	# nothing is touched if a file is not the released one
run 2>/dev/null && exit 1
boot grep -q stock Image
echo ok
