#!/bin/sh
# Run tools/install.sh against an image file in place of the boot partition.
# As root, on any Linux:  sh tests/install-test.sh
set -e
t=$(mktemp -d)
trap 'umount "$t/m" "$t/small" 2>/dev/null || :; rm -rf "$t"' EXIT
dtb=s5p6818-artik710-raptor
dev=$t/boot.img root=$t/root
boot() { mount -o loop "$t/boot.img" "$t/m"; (cd "$t/m" && "$@"); umount "$t/m"; }
run() { ARTIK_BOOT_DEV=$dev ARTIK_ROOT_DIR=$root sh "$t/rel/install.sh" "$@" >/dev/null; }
# as "curl URL | sh": the script comes through a pipe
piped() { (cd "$t/rel" && cat install.sh | PATH=$t/net:$PATH ARTIK_BOOT_DEV=$dev ARTIK_ROOT_DIR=$root sh -s -- "$@" >/dev/null 2>&1); }

mkdir "$t/m" "$t/root" "$t/rel" "$t/bin" "$t/small" "$t/net" "$t/alone"
truncate -s 32M "$t/boot.img"
mke2fs -q -t ext4 -b 4096 "$t/boot.img"
boot sh -c "echo stock > Image; echo stock > uInitrd; echo stock > $dtb-rev00.dtb; echo other > $dtb-rev03.dtb; echo keep > explorer.dtb"

cp "$(dirname "$0")/../tools/install.sh" "$t/rel/"
cd "$t/rel"
# a link in place of a file: the boot partition gets the file
echo new > kernel; ln -s kernel Image; echo new > uInitrd; echo new > $dtb.dtb
head -c 3000000 /dev/urandom > rootfs.ext4; truncate -s 8M rootfs.ext4
want=$(sha256sum < rootfs.ext4)
xz rootfs.ext4
sha256sum Image uInitrd $dtb.dtb rootfs.ext4.xz install.sh > SHA256SUMS

# The release page: this curl takes the files from the release directory. While
# $t/net/bad is there, the install.sh that it gives is not the released one.
cat > "$t/net/curl" <<EOF
#!/bin/sh
case \$4 in https://github.com/sophonz-labs/artik710-ubuntu/releases/latest/download/"\$3") ;; *) exit 1 ;; esac
echo "\$3" >> "$t/net/log"
{ [ "\$3" != install.sh ] || [ ! -e "$t/net/bad" ] || echo "touch $t/net/ran"; cat "$t/rel/\$3"; } > "\$3"
EOF
chmod +x "$t/net/curl"

# another machine than the board is refused (without the stand-in for its boot partition)
grep -q artik710 /proc/device-tree/compatible 2>/dev/null ||
	(ARTIK_ROOT_DIR=$t/small sh "$t/rel/install.sh" --restore 2>&1 || :) | grep -q 'not an ARTIK710'

# a mode that does not exist is refused before anything is fetched
piped --help && exit 1
sh "$t/rel/install.sh" --help 2>/dev/null | grep -q 'put the stock kernel back'
[ ! -e "$t/net/log" ]

# a download that is not the released file is not run, and nothing is touched
touch "$t/net/bad"
piped && exit 1
rm "$t/net/bad"
[ ! -e "$t/net/ran" ]
[ "$(wc -l < "$t/net/log")" = 6 ]
boot grep -q stock Image

# a root filesystem that does not fit: the boot partition stays as it is, and
# no half-written image is left
mount -t tmpfs -o size=1m tmpfs "$t/small"
root=$t/small
run 2>/dev/null && exit 1
run --erase 2>/dev/null && exit 1
[ -z "$(ls "$t/small")" ]
umount "$t/small"
root=$t/root
boot grep -q stock Image

# Try, with the script alone in a directory. It fetches the release, of the
# files that are there from the last time only the wrong one, and goes on with
# the copy that came with it.
cp install.sh "$t/alone/"
PATH=$t/net:$PATH ARTIK_BOOT_DEV=$dev ARTIK_ROOT_DIR=$root sh "$t/alone/install.sh" >/dev/null 2>&1
[ "$(wc -l < "$t/net/log")" = 8 ]
cmp install.sh "$t/root/artik710-ubuntu/install.sh"
boot sh -c "grep -q new Image && grep -q new uInitrd && grep -q new $dtb.dtb && grep -q keep explorer.dtb && ! ls $dtb-rev* 2>/dev/null"
grep -q stock "$t/root/stock-boot/Image"
grep -q other "$t/root/stock-boot/$dtb-rev03.dtb"
[ "$(sha256sum < "$t/root/ubuntu-26.04.img")" = "$want" ]

# Through a pipe it fetches the release too, whatever is in the directory. The
# kernel is the new one already: the saved stock files stay.
piped --erase
[ "$(wc -l < "$t/net/log")" = 9 ]
cmp rootfs.ext4.xz "$t/root/ubuntu-26.04.img.xz"
grep -q stock "$t/root/stock-boot/Image"

# --restore leaves the new kernel where it is if a saved stock file is missing
mv "$t/root/stock-boot/uInitrd" "$t/"
run --restore 2>/dev/null && exit 1
mv "$t/uInitrd" "$t/root/stock-boot/"
boot grep -q new Image

# through a pipe, --restore fetches the checksums only: it has the script
# already, and does not need the kernel that is not there any more
rm "$t/root/artik710-ubuntu/Image"
piped --restore
[ "$(wc -l < "$t/net/log")" = 10 ]
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
[ ! -L "$t/m/Image" ]
run --restore
grep -q stock "$t/m/Image"
grep -q "^$dev $t/m ext4 ro" /proc/mounts
dumpe2fs -h "$dev" 2>/dev/null | grep '^Filesystem features' > "$t/features"
grep -q has_journal "$t/features"
grep -q needs_recovery "$t/features" && exit 1
umount "$t/m"
dev=$t/boot.img

echo broken > Image	# nothing is touched if a file is not the released one
run 2>/dev/null && exit 1
boot grep -q stock Image
echo ok
