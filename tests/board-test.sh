#!/bin/sh
# Check the hardware interfaces of an ARTIK710 Raptor board from the running
# system. Run it as root. Nothing has to be plugged in: what cannot be seen
# without a cable, a card or a hand on the board is reported as "skip".
# The Zigbee check resets the Zigbee chip, the SPI check sends data on SPI2, the
# PWM check runs output 2 (GPIOC14) for a moment and the audio check plays two
# seconds of silence.
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

t_usb() {
	# The sockets are behind the hub on the HSIC port.
	h=$(grep -lsx 3503 /sys/bus/usb/devices/*/idProduct | head -n 1)
	if [ -n "$h" ]; then
		ok "usb: hub on the HSIC port"
		set -- "${h%/*}"/*/idProduct
		if [ -e "$1" ]; then
			ok "usb sockets: $# device(s)"
		else
			skip "usb sockets: nothing plugged in"
		fi
	else
		bad "usb: no hub (0424:3503)"
	fi
	u=/sys/class/udc/c0040000.usb
	has $u "usb: OTG port as a device"
	[ "$(cat $u/state 2>/dev/null)" = configured ] || skip "usb OTG port: no host on it"
}

t_wifi() {
	w=/sys/class/net/wlan0
	[ -e $w ] || { bad "wifi: no wlan0"; return; }
	f=$(cat $w/flags)
	[ $((f & 1)) = 1 ] || ip link set wlan0 up
	n=$(iw dev wlan0 scan 2>/dev/null | grep -c "^BSS")
	[ $((f & 1)) = 1 ] || ip link set wlan0 down
	if [ "$n" -gt 0 ]; then
		ok "wifi: a scan finds $n network(s)"
	else
		skip "wifi: wlan0 is there, a scan finds no network"
	fi
}

t_bluetooth() {
	a=$(btmgmt info 2>/dev/null | sed -n "s/^[[:space:]]*addr \([0-9A-F:]*\).*/\1/p" | head -n 1)
	case $a in
	"" | 00:00:00:00:00:00) bad "bluetooth: no controller" ;;
	*) ok "bluetooth: controller $a" ;;
	esac
}

t_zigbee() {
	if o=$(zigbee-ash /dev/ttySAC0 2>&1); then
		ok "zigbee: the chip answers a reset"
	else
		bad "zigbee: $(echo "$o" | tail -n 1)"
	fi
}

t_uart() {
	# ttySAC3 is the console, ttySAC1 belongs to Bluetooth and ttySAC0 is
	# the port of the Zigbee chip.
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

t_adc() {
	d=$(grep -ls c0053000.adc /sys/bus/iio/devices/*/name)
	[ -n "$d" ] || { bad "adc: no device"; return; }
	v=
	for n in 0 1 2 3 4 5 6 7; do
		r=$(cat "${d%/name}/in_voltage${n}_raw" 2>/dev/null) && [ "$r" -le 4095 ] ||
			{ bad "adc: channel $n does not convert"; return; }
		v="$v $r"
	done
	ok "adc:$v"
}

t_spi() {
	if o=$(spi-loop 2>&1); then
		ok "spi2: transfers complete"
	else
		bad "spi2: $(echo "$o" | grep -m 1 FAIL)"
	fi
	skip "spi2 pins: wire MOSI to MISO and run spi-loop -w"
}

t_pwm() {
	c=$(echo /sys/bus/platform/devices/c0018000.pwm/pwm/pwmchip*)
	# 20 Hz, and the pin is watched
	echo 2 > "$c/export" && echo 50000000 > "$c/pwm2/period" &&
		echo 25000000 > "$c/pwm2/duty_cycle" && echo 1 > "$c/pwm2/enable" ||
		{ bad "pwm: output 2 cannot be set"; return; }
	n=0 i=0
	while [ $i -lt 40 ]; do
		n=$((n + (($(busybox devmem 0xc001c018 32) >> 14) & 1)))
		i=$((i + 1))
	done
	echo 0 > "$c/pwm2/enable"
	echo 2 > "$c/unexport"
	if [ $n -gt 4 ] && [ $n -lt 36 ]; then
		ok "pwm: output 2 toggles"
	else
		bad "pwm: output 2 was high in $n of 40 samples"
	fi
}

t_hdmi() {
	c=$(echo /sys/class/drm/card*-HDMI-A-1)
	has "$c/status" "hdmi: connector"
	case $(cat "$c/status" 2>/dev/null) in
	connected) ok "hdmi: monitor, $(head -n 1 "$c/modes")" ;;
	disconnected) skip "hdmi: no monitor on it" ;;
	esac
}

# interrupts of the GPU so far
gpu_irqs() {
	awk '/gpmmu/ { for (i = 2; i <= 9; i++) s += $i; print s + 0 }' /proc/interrupts
}

t_gpu() {
	has /sys/bus/platform/drivers/lima/c0070000.gpu "gpu: Mali-400 with the lima driver"
	# Mesa is not in the image.
	command -v peglgears >/dev/null ||
		{ skip "gpu rendering: apt install mesa-utils libgl1-mesa-dri libegl-mesa0"; return; }
	n=$(gpu_irqs)
	# draws for 5 s, into memory
	o=$(EGL_PLATFORM=surfaceless peglgears 2>&1 | tail -n 1)
	if [ "$(gpu_irqs)" -gt "$n" ]; then ok "gpu: $o"; else bad "gpu: it drew nothing ($o)"; fi
}

t_audio() {
	if grep -qs S5P6818-HDMI /proc/asound/cards; then
		a=$(date +%s%N)
		if aplay -q -D hw:CARD=S5P6818HDMI -t raw -f S16_LE -r 48000 -c 2 -d 2 /dev/zero 2>/dev/null; then
			a=$((($(date +%s%N) - a) / 1000000))
			if [ $a -gt 1800 ] && [ $a -lt 2400 ]; then
				ok "audio: the HDMI card plays at the right speed"
			else
				bad "audio: the HDMI card took $a ms for 2 s of sound"
			fi
		else
			bad "audio: the HDMI card does not play"
		fi
	else
		bad "audio: no HDMI sound card"
	fi
	if grep -qs RT5658 /proc/asound/cards; then
		ok "audio: card of the codec"
	else
		skip "audio jacks: no codec answers"
	fi
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

all="cpu storage net usb wifi bluetooth uart zigbee i2c adc spi pwm hdmi gpu audio keys rtc watchdog"
[ $# -gt 0 ] || set -- $all
for t; do
	case " $all " in
	*" $t "*) t_"$t" ;;
	*) echo "no check \"$t\": $all" >&2; exit 2 ;;
	esac
done
exit $fail
