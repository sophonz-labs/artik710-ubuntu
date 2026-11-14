// SPDX-License-Identifier: GPL-2.0
/*
 * Read a file through a mapping on every CPU while the kernel reclaims its
 * pages, and check everything that is read.
 *
 * Reclaim unmaps a page on one CPU while others may still have it in their
 * TLB, frees it and uses it for something else. A CPU that missed the TLB
 * invalidation then reads another page's contents. On the S5P6818 that is what
 * happens between the two CPU clusters without the CCI-400 workaround (see
 * smp-coherency.c).
 *
 * The memory limit has to be much smaller than the file, or nothing is
 * reclaimed:
 *   systemd-run --wait --pipe -p MemoryMax=32M reclaim-coherency [seconds [MiB [file]]]
 * A file that is named is kept and used again as it is, so the next time it
 * can be on a read-only filesystem.
 * Exit status 1 if a page had the wrong contents, 2 if the test did not work.
 *
 * Build:  gcc -O2 -static -pthread -o reclaim-coherency reclaim-coherency.c
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#define PAGE	4096UL
#define RECENT	256	/* pages a thread goes back to: likely still in its TLB */

static uint64_t *map;	/* the first word of page i is i */
static unsigned long pages;
static atomic_ulong reads, wrong;
static time_t until;

static void *reader(void *arg)
{
	unsigned long recent[RECENT] = { 0 }, n = 0;
	uint64_t x = 88172645463325252ULL + (long)arg;
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET((long)arg, &set);
	sched_setaffinity(0, sizeof(set), &set);

	while (time(NULL) < until) {
		for (int k = 0; k < 1024; k++) {
			unsigned long i;

			x ^= x << 13;	/* xorshift */
			x ^= x >> 7;
			x ^= x << 17;
			if (x & 1)
				i = recent[(x >> 1) % RECENT];
			else
				i = recent[n++ % RECENT] = (x >> 1) % pages;
			if (map[i * (PAGE / 8)] != i)
				wrong++;
		}
		reads += 1024;
	}
	return NULL;
}

static int fill(const char *path)
{
	uint64_t page[PAGE / 8] = { 0 };
	int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);

	if (fd < 0)
		return fd;
	for (unsigned long i = 0; i < pages; i++) {
		page[0] = i;
		if (write(fd, page, PAGE) != PAGE)
			return -1;
		/* Only clean pages can be dropped, and dirty ones fill the limit. */
		if (i % 1024 == 1023 || i == pages - 1) {
			fdatasync(fd);
			posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
		}
	}
	return fd;
}

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 60;
	unsigned long mib = argc > 2 ? atol(argv[2]) : 256;
	const char *path = argc > 3 ? argv[3] : "/var/tmp/reclaim-coherency.tmp";
	long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
	pthread_t t[ncpu];
	struct rusage ru;
	int fd;

	pages = mib << 20 >> 12;
	fd = argc > 3 ? open(path, O_RDONLY) : -1;
	if (fd < 0 || lseek(fd, 0, SEEK_END) != (off_t)(pages * PAGE))
		fd = fill(path);
	if (fd < 0) {
		perror(path);
		return 2;
	}
	if (argc <= 3)
		unlink(path);
	map = mmap(NULL, pages * PAGE, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		return 2;
	}

	until = time(NULL) + secs;
	for (long c = 0; c < ncpu; c++)
		if (pthread_create(&t[c], NULL, reader, (void *)c)) {
			perror("pthread_create");
			return 2;
		}
	for (long c = 0; c < ncpu; c++)
		pthread_join(t[c], NULL);

	getrusage(RUSAGE_SELF, &ru);
	printf("%lu reads on %ld CPUs, %ld pages read back in, %lu reads with the wrong contents\n",
	       (unsigned long)reads, ncpu, ru.ru_majflt, (unsigned long)wrong);
	if (wrong) {
		printf("FAIL\n");
		return 1;
	}
	/* Every page fits in memory once: more faults than that means reclaim. */
	if ((unsigned long)ru.ru_majflt < pages) {
		printf("nothing was reclaimed: run it under a memory limit\n");
		return 2;
	}
	printf("ok\n");
	return 0;
}
