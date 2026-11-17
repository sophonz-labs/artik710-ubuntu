// SPDX-License-Identifier: GPL-2.0
/*
 * A program may read the ARM counter itself (CNTVCT_EL0, with its rate in
 * CNTFRQ_EL0) instead of asking the kernel for the time. Go does so from
 * version 1.26, in the delay of its spin loops, and with a counter that stands
 * still that delay never ends. On the S5P6818 the counter does not run, so the
 * kernel answers these reads itself, with the count of the PWM timer.
 *
 * On every CPU: the counter advances, at the rate that CNTFRQ_EL0 names, and
 * no read is below the one before it, on whatever CPU that was.
 *
 *   cntvct [-v] [seconds]   -v prints what each CPU reads. With seconds, the
 *                           check is repeated for that long: the PWM timer
 *                           wraps every 43 s
 *
 * Prints "cntvct: ok ..." and exits 0, or "cntvct: FAIL ..." and 1.
 *
 * Build:  gcc -O2 -static -o cntvct cntvct.c   (arm64 only)
 */
#define _GNU_SOURCE
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define READS	1000	/* for the cost of one read */

static uint64_t last, back_from, back_to;

static uint64_t cntvct(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
	if (v < last && !back_from) {
		back_from = last;
		back_to = v;
	}
	last = v;
	return v;
}

static inline uint64_t cntfrq(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

static uint64_t now_ns(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC_RAW, &t);
	return t.tv_sec * 1000000000ULL + t.tv_nsec;
}

static void denied(int sig)
{
	static const char m[] = "cntvct: FAIL reading the counter is not allowed\n";

	(void)sig;
	if (write(1, m, sizeof(m) - 1) < 0)
		_exit(2);
	_exit(1);
}

int main(int argc, char **argv)
{
	int verbose = argc > 1 && !strcmp(argv[1], "-v");
	int secs = argc > 1 + verbose ? atoi(argv[1 + verbose]) : 0;
	uint64_t end = now_ns() + secs * 1000000000ULL, frq = 0;
	int cpu, cpus, bad = 0;
	cpu_set_t all, one;

	signal(SIGILL, denied);
	signal(SIGSEGV, denied);
	if (sched_getaffinity(0, sizeof(all), &all)) {
		perror("cntvct: sched_getaffinity");
		return 1;
	}
	do {
		cpus = 0;
		for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
			uint64_t f, c0 = 0, c1 = 0, t0, t1, hz = 0;
			int try, i;

			CPU_ZERO(&one);
			CPU_SET(cpu, &one);
			if (!CPU_ISSET(cpu, &all) ||
			    sched_setaffinity(0, sizeof(one), &one))
				continue;
			cpus++;
			f = cntfrq();
			/* Being scheduled out spoils a measurement: three tries. */
			for (try = 0; try < 3; try++) {
				t0 = now_ns();
				c0 = cntvct();
				usleep(50000);
				c1 = cntvct();
				t1 = now_ns();
				hz = (c1 - c0) * 1000000000ULL / (t1 - t0);
				if (c1 > c0 && hz > f - f / 20 && hz < f + f / 20)
					break;
			}
			if (verbose) {
				t0 = now_ns();
				for (i = 0; i < READS; i++)
					c1 = cntvct();
				t1 = now_ns();
				printf("cpu %d: CNTFRQ_EL0 %llu, CNTVCT_EL0 %llu, counts at %llu Hz, %llu ns per read\n",
				       cpu, (unsigned long long)f, (unsigned long long)c1,
				       (unsigned long long)hz,
				       (unsigned long long)((t1 - t0) / READS));
			}
			if (back_from) {
				printf("cntvct: FAIL cpu %d: CNTVCT_EL0 went back from %llu to %llu\n",
				       cpu, (unsigned long long)back_from,
				       (unsigned long long)back_to);
				bad++;
			} else if (c1 == c0) {
				printf("cntvct: FAIL cpu %d: CNTVCT_EL0 stands still at %llu (CNTFRQ_EL0 is %llu)\n",
				       cpu, (unsigned long long)c0, (unsigned long long)f);
				bad++;
			} else if (try == 3) {
				printf("cntvct: FAIL cpu %d: CNTVCT_EL0 counts at %llu Hz, CNTFRQ_EL0 is %llu\n",
				       cpu, (unsigned long long)hz, (unsigned long long)f);
				bad++;
			}
			frq = f;
		}
		verbose = 0;
	} while (!bad && now_ns() < end);
	if (bad)
		return 1;
	printf("cntvct: ok  %d CPUs, %llu Hz", cpus, (unsigned long long)frq);
	if (secs)
		printf(", watched for %d s", secs);
	printf("\n");
	return 0;
}
