// SPDX-License-Identifier: GPL-2.0
/*
 * zigbee-ash: send an ASH RST frame to the EM358x Zigbee NCP and check that
 * it answers with RSTACK. Build: gcc -O2 -static -o zigbee-ash zigbee-ash.c
 *
 *   zigbee-ash [-r] [-b baud] [tty]    default: /dev/ttySAC0 57600 XON/XOFF
 *   zigbee-ash -t                      offline self-check of CRC and parser
 *
 * -r selects RTS/CTS (zigbeed "-n 0" = 115200 RTS/CTS); the stock board runs
 * zigbeed "-n 1" = 57600 XON/XOFF.
 *
 * ASH values (zigpy/bellows, bellows/ash.py: class Reserved, RstFrame,
 * RStackFrame, AshFrame.append_crc, send_reset; bellows/types/named.py:
 * NcpResetCode):
 *   FLAG 0x7E ends a frame, ESCAPE 0x7D then byte ^ 0x20,
 *   XON 0x11, XOFF 0x13, SUBSTITUTE 0x18, CANCEL 0x1A are reserved;
 *   RST control 0xC0 (no data), RSTACK control 0xC1 + version 0x02 + reset
 *   code; CRC = binascii.crc_hqx(data, 0xFFFF) (CRC-CCITT, poly 0x1021),
 *   sent big-endian. send_reset() writes 32 CANCEL bytes, then the frame.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define ASH_FLAG	0x7E
#define ASH_ESC		0x7D
#define ASH_XON		0x11
#define ASH_XOFF	0x13
#define ASH_SUB		0x18
#define ASH_CANCEL	0x1A
#define ASH_RST		0xC0
#define ASH_RSTACK	0xC1
#define ASH_ERROR	0xC2
#define ASH_VERSION	0x02

static uint16_t crc_ccitt(const uint8_t *p, size_t n)
{
	uint16_t crc = 0xFFFF;

	while (n--) {
		crc ^= (uint16_t)*p++ << 8;
		for (int i = 0; i < 8; i++)
			crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
	}
	return crc;
}

static const char *reset_name(uint8_t c)
{
	switch (c) {
	case 0x00: return "RESET_UNKNOWN_REASON";
	case 0x01: return "RESET_EXTERNAL";
	case 0x02: return "RESET_POWER_ON";
	case 0x03: return "RESET_WATCHDOG";
	case 0x06: return "RESET_ASSERT";
	case 0x09: return "RESET_BOOTLOADER";
	case 0x0B: return "RESET_SOFTWARE";
	default: return "?";
	}
}

/*
 * Feed one received byte. Returns 1 and fills frame/len when a frame ended
 * at FLAG, 0 otherwise. Reserved flow-control bytes are dropped, CANCEL and
 * SUBSTITUTE discard the frame in progress.
 */
static int ash_rx(uint8_t b, uint8_t *frame, size_t *len)
{
	static uint8_t buf[256];
	static size_t n;
	static int esc, bad;

	switch (b) {
	case ASH_XON:
	case ASH_XOFF:
		return 0;
	case ASH_CANCEL:
		n = 0; esc = 0; bad = 0;
		return 0;
	case ASH_SUB:
		bad = 1;
		return 0;
	case ASH_ESC:
		esc = 1;
		return 0;
	case ASH_FLAG: {
		int done = !bad && !esc && n > 0;

		if (done) {
			memcpy(frame, buf, n);
			*len = n;
		}
		n = 0; esc = 0; bad = 0;
		return done;
	}
	}
	if (esc) {
		b ^= 0x20;
		esc = 0;
	}
	if (n < sizeof(buf))
		buf[n++] = b;
	else
		bad = 1;
	return 0;
}

/* 0: RSTACK ok, 1: not RSTACK (ignore), -1: RSTACK/ERROR but invalid. */
static int check_frame(const uint8_t *f, size_t n, int verbose)
{
	if (verbose) {
		printf("rx frame:");
		for (size_t i = 0; i < n; i++)
			printf(" %02X", f[i]);
		printf(" 7E\n");
	}
	if (n < 3)
		return 1;
	if (crc_ccitt(f, n - 2) != (uint16_t)(f[n - 2] << 8 | f[n - 1])) {
		printf("bad CRC\n");
		return -1;
	}
	if (f[0] == ASH_ERROR && n == 5) {
		printf("ERROR frame: version 0x%02X code 0x%02X\n", f[1], f[2]);
		return -1;
	}
	if (f[0] != ASH_RSTACK)
		return 1;
	if (n != 5 || f[1] != ASH_VERSION) {
		printf("RSTACK with unexpected length %zu or version 0x%02X\n",
		       n, f[1]);
		return -1;
	}
	printf("RSTACK: ASH version %u, reset code 0x%02X (%s)\n",
	       f[1], f[2], reset_name(f[2]));
	return 0;
}

static int selftest(void)
{
	const uint8_t rst = ASH_RST;
	const uint8_t rx[] = { 0x11, 0xC1, 0x02, 0x0B, 0x0A, 0x52, 0x7E };
	uint8_t f[256];
	size_t n = 0;
	int got = 0;

	if (crc_ccitt(&rst, 1) != 0x38BC) {
		printf("selftest: CRC of RST is 0x%04X, want 0x38BC\n",
		       crc_ccitt(&rst, 1));
		return 1;
	}
	for (size_t i = 0; i < sizeof(rx); i++)
		if (ash_rx(rx[i], f, &n))
			got = check_frame(f, n, 0) == 0;
	printf("selftest: %s\n", got ? "OK" : "FAILED");
	return !got;
}

static speed_t to_speed(long baud)
{
	switch (baud) {
	case 9600: return B9600;
	case 19200: return B19200;
	case 38400: return B38400;
	case 57600: return B57600;
	case 115200: return B115200;
	default: return 0;
	}
}

int main(int argc, char **argv)
{
	const char *dev = "/dev/ttySAC0";
	long baud = 57600;
	int rtscts = 0, opt, fd, ret = 2;
	uint8_t tx[40], f[256];
	size_t n, txn = 0;
	struct termios t;
	struct timespec end, now;
	uint16_t crc;
	speed_t sp;

	while ((opt = getopt(argc, argv, "rb:t")) != -1) {
		switch (opt) {
		case 'r': rtscts = 1; break;
		case 'b': baud = strtol(optarg, NULL, 10); break;
		case 't': return selftest();
		default:
			fprintf(stderr, "usage: %s [-r] [-b baud] [tty] | -t\n",
				argv[0]);
			return 2;
		}
	}
	if (optind < argc)
		dev = argv[optind];
	sp = to_speed(baud);
	if (!sp) {
		fprintf(stderr, "unsupported baud %ld\n", baud);
		return 2;
	}

	fd = open(dev, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		perror(dev);
		return 2;
	}
	if (tcgetattr(fd, &t)) {
		perror("tcgetattr");
		return 2;
	}
	cfmakeraw(&t);
	cfsetispeed(&t, sp);
	cfsetospeed(&t, sp);
	t.c_cflag |= CLOCAL | CREAD;
	t.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS);
	t.c_iflag &= ~(IXON | IXOFF | IXANY);
	if (rtscts)
		t.c_cflag |= CRTSCTS;
	else
		t.c_iflag |= IXON | IXOFF;	/* NCP "-n 1": software flow */
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = 0;
	if (tcsetattr(fd, TCSANOW, &t)) {
		perror("tcsetattr");
		return 2;
	}
	tcflush(fd, TCIOFLUSH);

	memset(tx, ASH_CANCEL, 32);
	txn = 32;
	tx[txn++] = ASH_RST;
	crc = crc_ccitt(&tx[32], 1);	/* 0x38BC: neither byte is reserved */
	tx[txn++] = crc >> 8;
	tx[txn++] = crc & 0xFF;
	tx[txn++] = ASH_FLAG;
	printf("%s %ld 8N1 %s: tx 32x1A C0 %02X %02X 7E\n", dev, baud,
	       rtscts ? "RTS/CTS" : "XON/XOFF", crc >> 8, crc & 0xFF);
	if (write(fd, tx, txn) != (ssize_t)txn) {
		perror("write");
		return 2;
	}
	tcdrain(fd);

	/* The NCP reboots before it answers; allow 5 s. */
	clock_gettime(CLOCK_MONOTONIC, &end);
	end.tv_sec += 5;
	for (;;) {
		struct pollfd p = { .fd = fd, .events = POLLIN };
		uint8_t b[64];
		ssize_t r;
		long ms;

		clock_gettime(CLOCK_MONOTONIC, &now);
		ms = (end.tv_sec - now.tv_sec) * 1000 +
		     (end.tv_nsec - now.tv_nsec) / 1000000;
		if (ms <= 0) {
			printf("FAIL: no RSTACK within 5 s\n");
			break;
		}
		if (poll(&p, 1, ms) <= 0)
			continue;
		r = read(fd, b, sizeof(b));
		if (r < 0 && errno != EAGAIN) {
			perror("read");
			break;
		}
		for (ssize_t i = 0; i < r; i++) {
			if (!ash_rx(b[i], f, &n))
				continue;
			int c = check_frame(f, n, 1);

			if (c == 0) {
				printf("PASS\n");
				ret = 0;
				goto out;
			}
			if (c < 0)
				ret = 1;
		}
	}
out:
	close(fd);
	return ret;
}
