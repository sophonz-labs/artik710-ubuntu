#!/bin/sh
# Compress, decompress and hash the same data on every CPU at once and compare
# every result. Memory that gets corrupted (see smp-coherency.c) shows up as a
# wrong hash or a crashed process.
#   stress-integrity [rounds per CPU]     exit status 1 if a result was wrong
n=${1:-40}
cpus=$(nproc)
f=/dev/shm/stress-integrity.$$
trap 'rm -f $f $f.*' EXIT
head -c 33554432 /dev/urandom > $f
ref=$(sha256sum < $f)
for j in $(seq $cpus); do
	(
		echo $n > $f.$j		# all wrong if this shell dies
		bad=0
		# shellcheck disable=SC2034
		for i in $(seq $n); do
			[ "$(gzip -1 < $f | gunzip | sha256sum)" = "$ref" ] || bad=$((bad + 1))
		done
		echo $bad > $f.$j
	) &
done
wait
bad=0
for r in $f.*; do bad=$((bad + $(cat $r))); done
echo "$((n * cpus)) rounds on $cpus CPUs, $bad with a wrong result"
[ $bad = 0 ]
