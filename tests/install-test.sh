#!/bin/sh
# Run tools/install.sh against an image file in place of the boot partition.
# As root, on any Linux:  sh tests/install-test.sh
set -e
t=$(mktemp -d)
trap 'umount "$t/m" 2>/dev/null || :; rm -rf "$t"' EXIT
dtb=s5p6818-artik710-raptor
dev=$t/boot.img
boot() { mount -o loop "$t/boot.img" "$t/m"; (cd "$t/m" && "$@"); umount "$t/m"; }
run() { ARTIK_BOOT_DEV=$dev ARTIK_ROOT_DIR=$t/root sh "$t/rel/install.sh" "$@" >/dev/null; }

mkdir "$t/m" "$t/root" "$t/rel" "$t/bin"
truncate -s 32M "$t/boot.img"
mke2fs -q -t ext4 "$t/boot.img"
boot sh -c "echo stock > Image; echo stock > uInitrd; echo stock > $dtb-rev00.dtb; echo other > $dtb-rev03.dtb; echo keep > explorer.dtb"

cp "$(dirname "$0")/../tools/install.sh" "$t/rel/"
cd "$t/rel"
# a link in place of a file: the boot partition gets the file
echo new > kernel; ln -s kernel Image; echo new > uInitrd; echo new > $dtb.dtb
head -c 3000000 /dev/urandom > rootfs.ext4; truncate -s 8M rootfs.ext4
want=$(sha256sum < rootfs.ext4)
xz rootfs.ext4
sha256sum Image uInitrd $dtb.dtb rootfs.ext4.xz install.sh > SHA256SUMS

# the boot partition stays as it is if the root filesystem cannot be written
mkdir "$t/root/ubuntu-26.04.img"
run 2>/dev/null && exit 1
rmdir "$t/root/ubuntu-26.04.img"
boot grep -q stock Image

run		# try
boot sh -c "[ ! -L Image ] && grep -q new Image && grep -q new uInitrd && grep -q new $dtb.dtb && grep -q keep explorer.dtb && ! ls $dtb-rev* 2>/dev/null"
grep -q stock "$t/root/stock-boot/Image" && grep -q other "$t/root/stock-boot/$dtb-rev03.dtb"
[ "$(sha256sum < "$t/root/ubuntu-26.04.img")" = "$want" ]

run --erase	# the kernel is the new one already: the saved stock files stay
cmp rootfs.ext4.xz "$t/root/ubuntu-26.04.img.xz"
grep -q stock "$t/root/stock-boot/Image"

# --restore leaves the new kernel where it is if a saved stock file is missing
mv "$t/root/stock-boot/uInitrd" "$t/"
run --restore 2>/dev/null && exit 1
mv "$t/uInitrd" "$t/root/stock-boot/"
boot grep -q new Image

run --restore
boot sh -c "grep -q stock Image && grep -q stock uInitrd && grep -q other $dtb-rev03.dtb && [ ! -e $dtb.dtb ]"
[ ! -e "$t/root/ubuntu-26.04.img.xz" ]

# A kernel that is not on the disk as it was copied stops the script. This cp
# changes a block of it under the filesystem: only a read from the disk sees it.
cat > "$t/bin/cp" <<EOF
#!/bin/sh
/bin/cp "\$@" && sync && for d; do :; done &&
	n=\$(filefrag -v "\$d/Image" | awk '\$1 == "0:" { print \$4 + 0 }') &&
	printf x | dd of="$t/boot.img" bs=4096 seek="\$n" conv=notrunc status=none
EOF
chmod +x "$t/bin/cp"
(PATH=$t/bin:$PATH; run --restore 2>&1) | grep -q 'did not take Image'
run --restore
boot sh -c '[ "$(cat Image)" = stock ]'

# The stock system has the partition mounted read-only. It is so again
# afterwards, with nothing left in the journal, which U-Boot does not read.
mount -o loop "$t/boot.img" "$t/m"
mount -o remount,ro "$t/m"
dev=$(awk -v m="$t/m" '$2 == m { print $1 }' /proc/mounts)
run
grep -q new "$t/m/Image"
run --restore
grep -q stock "$t/m/Image"
grep -q "^$dev $t/m ext4 ro" /proc/mounts
dumpe2fs -h "$dev" 2>/dev/null | grep -q needs_recovery && exit 1
umount "$t/m"
dev=$t/boot.img

echo broken > Image	# nothing is touched if a file is not the released one
run 2>/dev/null && exit 1
boot grep -q stock Image
echo ok
