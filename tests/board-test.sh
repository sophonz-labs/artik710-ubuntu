#!/bin/sh
# Check the hardware interfaces of an ARTIK710 Raptor board from the running
# system. Run it as root. Nothing has to be plugged in: what cannot be seen
# without a cable, a card or a hand on the board is reported as "skip".
#
#   board-test             every check
#   board-test uart i2c    only these
#
# Exit status 1 if a check failed.
fail=0
ok() { echo "ok    $*"; }
bad() { echo "FAIL  $*"; fail=1; }
skip() { echo "skip  $*"; }
# has FILE WHAT: the file is there
has() { if [ -e "$1" ]; then ok "$2"; else bad "$2: no $1"; fi; }

t_cpu() {
	n=$(nproc)
	if [ "$n" = 8 ]; then ok "cpu: 8 cores online"; else bad "cpu: $n of 8 cores online"; fi
	m=$(awk '/MemTotal/ { print int($2 / 1024) }' /proc/meminfo)
	if [ "$m" -ge 900 ]; then ok "memory: $m MiB"; else bad "memory: only $m MiB"; fi
	t=$(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null || echo 0)
	if [ "$t" -gt 10000 ] && [ "$t" -lt 95000 ]; then
		ok "thermal: $((t / 1000)) C"
	else
		bad "thermal: reads $t"
	fi
}

t_storage() {
	has /sys/block/mmcblk0/device/name "eMMC"
	if c=$(cat /sys/class/mmc_host/mmc1/mmc1:*/name 2>/dev/null); then
		ok "SD slot: card $c"
	else
		has /sys/class/mmc_host/mmc1 "SD slot controller"
		skip "SD slot: no card in it"
	fi
}

t_net() {
	e=$(echo /sys/devices/platform/soc/c0060000.ethernet/net/*)
	if [ "$(cat "$e/carrier" 2>/dev/null)" = 1 ]; then
		ok "ethernet: link up, $(cat "$e/speed") Mb/s"
	else
		has "$e" "ethernet"
		skip "ethernet: no link"
	fi
}

t_uart() {
	# ttySAC3 is the console and ttySAC1 belongs to Bluetooth.
	for n in 0 2 4; do
		if o=$(uart-loopback /dev/ttySAC$n 2>&1); then
			ok "uart$n: internal loopback"
		else
			bad "uart$n: $(echo "$o" | head -n 1)"
		fi
	done
	skip "uart pins: wire TXD to RXD and run uart-loopback /dev/ttySACn wire"
}

t_i2c() {
	for n in 0 1 2 4 5 6 7 8 9 10 11; do
		# A bus with a line stuck low takes minutes to scan.
		if timeout 20 i2cdetect -y $n >/dev/null 2>&1; then
			ok "i2c-$n: scanned"
		else
			bad "i2c-$n: no bus, or it hangs"
		fi
	done
	has /sys/bus/i2c/devices/10-0032/driver "PMIC on i2c-10"
}

t_keys() {
	if grep -qs gpio-keys /proc/bus/input/devices; then
		ok "keys: input device"
	else
		bad "keys: no gpio-keys input device"
	fi
	skip "keys: nobody pressed one"
}

t_rtc() {
	a=$(cat /sys/class/rtc/rtc0/since_epoch 2>/dev/null) || { bad "rtc: no rtc0"; return; }
	sleep 2
	d=$(($(cat /sys/class/rtc/rtc0/since_epoch) - a))
	if [ $d -ge 1 ] && [ $d -le 3 ]; then ok "rtc: runs"; else bad "rtc: $d s in 2 s"; fi
}

t_watchdog() {
	# Not opened: that would start it.
	has /dev/watchdog0 "watchdog"
}

all="cpu storage net uart i2c keys rtc watchdog"
[ $# -gt 0 ] || set -- $all
for t; do
	case " $all " in
	*" $t "*) t_"$t" ;;
	*) echo "no check \"$t\": $all" >&2; exit 2 ;;
	esac
done
exit $fail
