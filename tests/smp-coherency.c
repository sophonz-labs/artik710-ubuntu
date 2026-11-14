// SPDX-License-Identifier: GPL-2.0
/*
 * Check that TLB and instruction cache invalidations issued on one CPU reach
 * every other CPU.
 *
 * The two Cortex-A53 clusters of the S5P6818 are joined by a CCI-400 that only
 * forwards DVMv7 messages. Of an AArch64 kernel's TLB invalidations, those by
 * address do not arrive in the other cluster, and those by ASID only while the
 * ASID is below 256. A kernel without a workaround leaves stale translations
 * there and user memory gets corrupted at random.
 *
 * For every ordered pair of CPUs one thread keeps reading through a mapping
 * while the other replaces it:
 *   tlb-page   one page remapped        (kernel invalidates by address)
 *   tlb-mm     4 MiB remapped           (kernel invalidates by ASID: without
 *                                        the workaround this only fails once
 *                                        some 256 processes have run)
 *   icache     a function rewritten     (user-space invalidation by address)
 * The change is complete when the writer goes on, so in the TLB tests one read
 * of the old contents after that is stale. The instruction cache test allows
 * 1000 in a row: code that was already fetched may still run.
 *
 * Build:  gcc -O2 -static -pthread -o smp-coherency smp-coherency.c
 * Run:    smp-coherency [rounds]      exit status 1 if anything was stale
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define BIG	(4UL << 20)
#define SETTLE	1000	/* reads the reader gets after each change */

enum { TLB_PAGE, TLB_MM, ICACHE, MODES };
static const char *const mode_name[MODES] = { "tlb-page", "tlb-mm", "icache" };

/* Both threads spin on gen and seen: a cache line each, or the writer starves. */
static _Alignas(64) atomic_uint gen;	/* odd while the writer is changing things */
static _Alignas(64) atomic_ulong seen;
static atomic_ulong stale;
static atomic_int stop, mode;
static volatile unsigned *data;	/* the mapping that gets replaced */
static unsigned *code;		/* mov w0, #gen; ret */

static void pin(int cpu)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	if (sched_setaffinity(0, sizeof(set), &set)) {
		perror("sched_setaffinity");
		exit(2);
	}
}

static void *reader(void *arg)
{
	unsigned bad = 0;

	pin((long)arg);
	while (!stop) {
		unsigned g1 = gen, v, g2;

		v = mode == ICACHE ? ((unsigned (*)(void))code)() : *data;
		g2 = gen;
		if (g1 != g2 || (g1 & 1)) {
			bad = 0;
			continue;
		}
		seen++;
		if (v == g1)
			bad = 0;
		else if (++bad == (mode == ICACHE ? SETTLE : 1))
			stale++;
	}
	return NULL;
}

/* Let the reader look at the current state for a while. */
static void settle(void)
{
	unsigned long until = seen + 2 * SETTLE;

	while (seen < until)
		;
}

int main(int argc, char **argv)
{
	int rounds = argc > 1 ? atoi(argv[1]) : 100;
	int ncpu = sysconf(_SC_NPROCESSORS_CONF), fd, failed = 0;
	unsigned *ctl[2];

	fd = memfd_create("smp-coherency", 0);
	if (fd < 0 || ftruncate(fd, 2 * BIG)) {
		perror("memfd");
		return 2;
	}
	ctl[0] = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	ctl[1] = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, BIG);
	data = mmap(NULL, BIG, PROT_READ, MAP_SHARED | MAP_POPULATE, fd, 0);
	code = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (ctl[0] == MAP_FAILED || ctl[1] == MAP_FAILED ||
	    data == MAP_FAILED || code == MAP_FAILED) {
		perror("mmap");
		return 2;
	}
	code[0] = 0x52800000;	/* mov w0, #0 */
	code[1] = 0xd65f03c0;	/* ret */
	__builtin___clear_cache((char *)code, (char *)code + 8);

	for (int m = 0; m < MODES; m++) {
		printf("%-8s  writer cpu ->\n", mode_name[m]);
		for (int r = 0; r < ncpu; r++) {
			pthread_t t;

			/* gen is 16 bits wide in the icache test (mov immediate) */
			gen = 0;
			*ctl[0] = *ctl[1] = 0;
			code[0] = 0x52800000;
			__builtin___clear_cache((char *)code, (char *)code + 8);
			mode = m;
			stop = 0;
			if (pthread_create(&t, NULL, reader, (void *)(long)r)) {
				perror("pthread_create");
				return 2;
			}
			printf("  reader %d ", r);
			for (int w = 0; w < ncpu; w++) {
				unsigned long before = stale;

				if (w == r) {
					printf(" -");
					continue;
				}
				pin(w);
				settle();
				for (int i = 0; i < rounds; i++) {
					unsigned next = gen + 2;
					int k = (next / 2) & 1;

					gen++;
					if (m == ICACHE) {
						code[0] = 0x52800000 | next << 5;
						__builtin___clear_cache((char *)code,
									(char *)code + 4);
					} else {
						size_t len = m == TLB_MM ? BIG : 4096;

						*ctl[k] = next;
						if (mmap((void *)data, len, PROT_READ,
							 MAP_SHARED | MAP_FIXED | MAP_POPULATE,
							 fd, k * BIG) == MAP_FAILED) {
							perror("mmap");
							return 2;
						}
					}
					gen++;
					settle();
				}
				printf(" %c", stale == before ? '.' : 'X');
				failed |= stale != before;
			}
			printf("\n");
			stop = 1;
			pthread_join(t, NULL);
		}
	}
	printf("%s\n", failed ? "FAIL: stale reads (X)" : "ok");
	return failed;
}
