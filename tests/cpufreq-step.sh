#!/bin/sh
# Step the CPUs of an ARTIK710 through their frequencies, the lowest first. At
# each step: PLL1 as the hardware has it, the voltage of the CPU rail as the
# kernel and as the PMIC have it, then a load on all CPUs that checks its own
# results (cpu-load). Stops at the first wrong value, and ends at 800 MHz with
# the governor that it found. Run it as root.
#
#   cpufreq-step [seconds of load per step] [highest kHz]
#
# The log is /var/log/cpufreq-step.log, written out line by line: if the board
# stops, the step after its last line is the one that did it.
set -u
T=${1:-10}
MAX=${2:-1400000}
P=/sys/devices/system/cpu/cpufreq/policy0
LOG=/var/log/cpufreq-step.log
GOV=$(cat $P/scaling_governor)
REG=$(grep -lx vdd_cpu /sys/class/regulator/*/name)
REG=${REG%/name}

log() { echo "$*" | tee -a $LOG; sync; }
fail() { log "FAIL  $*"; exit 1; }
temp() { echo $(($(cat /sys/class/thermal/thermal_zone0/temp) / 1000)); }
restore() {
	echo 800000 > $P/scaling_setspeed 2>/dev/null
	echo "$GOV" > $P/scaling_governor
}

[ "$(cat $P/scaling_driver 2>/dev/null)" = cpufreq-dt ] || fail "the driver is not cpufreq-dt"
[ -d "$REG" ] || fail "no regulator vdd_cpu"
trap restore EXIT
log "start $(date), $(dmesg | grep -o "ASV level.*" | head -n 1)"
echo userspace > $P/scaling_governor || fail "no userspace governor"

for f in $(tr ' ' '\n' < $P/scaling_available_frequencies | sort -n); do
	[ "$f" -le "$MAX" ] || break
	# The passive trip of the thermal zone lowers the limit.
	[ "$f" -le "$(cat $P/scaling_max_freq)" ] ||
		{ log "stop  $f kHz is above the limit, at $(temp) C"; exit 0; }
	echo "$f" > $P/scaling_setspeed || fail "$f kHz: cannot be set"
	sleep 1
	# PLL1: bit 28 output on, bit 29 power down, then P, M and S
	v=$(busybox devmem 0xc001000c 32)
	pll=$((((v >> 8 & 1023) * 24000 / (v >> 18 & 63)) >> (v & 255)))
	uv=$(cat "$REG/microvolts")
	dac=$(i2cget -f -y 10 0x32 0x36)	# DC1DAC of the PMIC
	[ $((v >> 28 & 3)) = 1 ] && [ "$pll" = "$f" ] || fail "$f kHz: PLL1 is $v, $pll kHz"
	[ "$uv" = $((600000 + dac * 12500)) ] || fail "$f kHz: the kernel has $uv uV, the PMIC $dac"
	o=$(cpu-load "$T") || fail "$f kHz, $uv uV: $o"
	log "ok    $f kHz, PLL1 $v, $uv uV, $(temp) C, ${o#cpu-load: ok, }"
done
log "all steps passed"
