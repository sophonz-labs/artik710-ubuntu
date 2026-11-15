// SPDX-License-Identifier: GPL-2.0
/*
 * Load every CPU and check the results: each thread repeats the same integer,
 * memory and floating point work and compares it with the result that the
 * main thread computed before. A CPU that runs too fast for its voltage gets
 * wrong answers or hangs. Nothing in the image starts the watchdog, so a hung
 * board stays stopped until someone resets it.
 *
 *   cpu-load [seconds]   (default 10)
 *
 * Prints "cpu-load: ok ..." and exits 0, or "cpu-load: FAIL ..." and 1.
 *
 * Build:  gcc -O2 -static -pthread -o cpu-load cpu-load.c
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define WORDS	(256 * 1024 / 8)	/* 256 KiB per thread: past L1, in L2 */

struct result {
	uint64_t sum;
	double fp;
};

struct job {
	int cpu;
	uint64_t *buf;
	long rounds;
	int bad;
};

static struct result golden;
static volatile int stop;

static void work(uint64_t *buf, struct result *r)
{
	uint64_t x = 88172645463325252ULL, s = 0;
	double f = 0;
	long i;

	for (i = 0; i < WORDS; i++) {	/* xorshift64 */
		x ^= x << 13;
		x ^= x >> 7;
		x ^= x << 17;
		buf[i] = x;
	}
	for (i = 0; i < WORDS; i++) {	/* read back in a scattered order */
		s = s * 31 + buf[(i * 7919) % WORDS];
		f += (double)(buf[i] >> 11) / (double)((buf[i] & 0xffff) + 1);
	}
	r->sum = s;
	r->fp = f;
}

static void *run(void *arg)
{
	struct job *j = arg;
	struct result r;
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(j->cpu, &set);
	if (sched_setaffinity(0, sizeof(set), &set))
		j->bad = -1;	/* CPU offline: reported, not a failure of the CPU */
	while (!stop) {
		work(j->buf, &r);
		if (j->bad >= 0 && (r.sum != golden.sum || r.fp != golden.fp))
			j->bad++;
		j->rounds++;
	}
	return NULL;
}

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 10;
	long n = sysconf(_SC_NPROCESSORS_CONF), total = 0;
	pthread_t th[64];
	struct job jobs[64];
	uint64_t *b = malloc(WORDS * 8);
	int i, fail = 0;

	if (n > 64)
		n = 64;
	if (!b)
		return 2;
	work(b, &golden);
	for (i = 0; i < n; i++) {
		jobs[i] = (struct job){ .cpu = i, .buf = malloc(WORDS * 8) };
		if (!jobs[i].buf || pthread_create(&th[i], NULL, run, &jobs[i]))
			return 2;
	}
	sleep(secs);
	stop = 1;
	for (i = 0; i < n; i++) {
		pthread_join(th[i], NULL);
		total += jobs[i].rounds;
		if (jobs[i].bad < 0) {
			printf("cpu-load: cpu%d is offline\n", i);
		} else if (jobs[i].bad || !jobs[i].rounds) {
			printf("cpu-load: FAIL cpu%d, %d of %ld rounds wrong\n",
			       i, jobs[i].bad, jobs[i].rounds);
			fail = 1;
		}
	}
	if (!fail)
		printf("cpu-load: ok, %ld rounds on %ld cpus\n", total, n);
	return fail;
}
