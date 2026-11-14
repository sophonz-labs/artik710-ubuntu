// SPDX-License-Identifier: GPL-2.0
/*
 * SPI transfer test for /dev/spidevB.C.
 *
 *   spi-loop [-d /dev/spidev2.0] [-s hz] [-w]
 *
 * 1. Asks for the controller's internal loopback (SPI_LOOP) and says whether
 *    the driver accepts it.
 * 2. Runs a 32 byte transfer (below the 256 byte FIFO: PIO) and a 4096 byte
 *    transfer (DMA when the controller has DMA channels) with a known pattern.
 * 3. The data is compared when SPI_LOOP was accepted or with -w (MOSI wired
 *    to MISO); otherwise only the completion of the transfers is checked,
 *    since MISO floats.
 *
 * Exit status 0 = all transfers completed (and matched when compared).
 * Build: gcc -O2 -static -o spi-loop spi-loop.c
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <linux/spi/spidev.h>

static int xfer(int fd, size_t len, uint32_t hz, int compare)
{
	static uint8_t tx[4096], rx[4096];
	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long)tx,
		.rx_buf = (unsigned long)rx,
		.len = len,
		.speed_hz = hz,
		.bits_per_word = 8,
	};
	struct timespec a, b;
	size_t i, bad = 0;
	int ret;

	for (i = 0; i < len; i++)
		tx[i] = (uint8_t)(i * 7 + 0x5a);
	memset(rx, 0xee, len);

	clock_gettime(CLOCK_MONOTONIC, &a);
	ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
	clock_gettime(CLOCK_MONOTONIC, &b);
	if (ret < 0) {
		printf("len %4zu: FAIL ioctl: %s\n", len, strerror(errno));
		return 1;
	}

	for (i = 0; i < len; i++)
		bad += rx[i] != tx[i];

	printf("len %4zu: done in %ld us, rx[0..3] = %02x %02x %02x %02x, %zu/%zu bytes differ%s\n",
	       len, (long)((b.tv_sec - a.tv_sec) * 1000000 +
			   (b.tv_nsec - a.tv_nsec) / 1000),
	       rx[0], rx[1], rx[2], rx[3], bad, len,
	       compare ? "" : " (not compared)");

	return compare && bad;
}

int main(int argc, char **argv)
{
	const char *dev = "/dev/spidev2.0";
	uint32_t hz = 1000000;
	uint8_t mode = SPI_LOOP, bits = 8;
	int wired = 0, loop, fd, opt, fails = 0;

	while ((opt = getopt(argc, argv, "d:s:w")) != -1) {
		switch (opt) {
		case 'd': dev = optarg; break;
		case 's': hz = strtoul(optarg, NULL, 0); break;
		case 'w': wired = 1; break;
		default:
			fprintf(stderr, "usage: %s [-d dev] [-s hz] [-w]\n", argv[0]);
			return 2;
		}
	}

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		perror(dev);
		return 1;
	}

	loop = ioctl(fd, SPI_IOC_WR_MODE, &mode) == 0;
	printf("SPI_LOOP: %s\n", loop ? "accepted, data is compared" :
	       "not supported by the driver");
	if (!loop) {
		mode = 0;
		if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) {
			perror("SPI_IOC_WR_MODE");
			return 1;
		}
	}
	if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
	    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &hz) < 0) {
		perror("spidev setup");
		return 1;
	}

	fails += xfer(fd, 32, hz, loop || wired);
	fails += xfer(fd, 4096, hz, loop || wired);

	close(fd);
	printf("%s\n", fails ? "RESULT: FAIL" : "RESULT: PASS");
	return !!fails;
}
