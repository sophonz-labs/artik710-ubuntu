// SPDX-License-Identifier: GPL-2.0
/*
 * Send data through a serial port and check that it comes back.
 *
 *   uart-loopback /dev/ttySAC2        the port loops back inside the SoC:
 *                                     nothing has to be connected, and the
 *                                     pins are not part of the test
 *   uart-loopback /dev/ttySAC2 wire   TXD is wired to RXD
 *
 * Exit status 1 if the data did not come back as sent, 2 if the test did not
 * work.
 *
 * Build:  gcc -O2 -static -o uart-loopback uart-loopback.c
 */
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define LEN	4096	/* many times the FIFO, 0.4 s at 115200 baud */
#ifndef TIOCM_LOOP	/* in asm/termios.h, which does not go with termios.h */
#define TIOCM_LOOP	0x8000
#endif

int main(int argc, char **argv)
{
	unsigned char out[LEN], in[LEN];
	int loop = TIOCM_LOOP, fd, n = 0, sent = 0;
	struct termios t;

	if (argc < 2) {
		fprintf(stderr, "usage: %s /dev/ttySACn [wire]\n", argv[0]);
		return 2;
	}
	fd = open(argv[1], O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0 || tcgetattr(fd, &t)) {
		perror(argv[1]);
		return 2;
	}
	cfmakeraw(&t);
	cfsetspeed(&t, B115200);
	t.c_cflag |= CLOCAL | CREAD;
	t.c_cflag &= ~CRTSCTS;
	if (tcsetattr(fd, TCSANOW, &t) ||
	    (argc < 3 && ioctl(fd, TIOCMBIS, &loop))) {
		perror(argv[1]);
		return 2;
	}
	tcflush(fd, TCIOFLUSH);

	for (int i = 0; i < LEN; i++)
		out[i] = i * 7 + (i >> 8);

	while (n < LEN) {
		struct pollfd p = { fd, POLLIN | (sent < LEN ? POLLOUT : 0), 0 };
		int r;

		if (poll(&p, 1, 1000) <= 0)
			break;	/* nothing for a second */
		if (p.revents & POLLOUT) {
			r = write(fd, out + sent, LEN - sent);
			if (r > 0)
				sent += r;
		}
		if (p.revents & POLLIN) {
			r = read(fd, in + n, LEN - n);
			if (r > 0)
				n += r;
		}
	}
	if (argc < 3)
		ioctl(fd, TIOCMBIC, &loop);

	if (n != LEN || memcmp(in, out, LEN)) {
		int bad = 0;

		while (bad < n && in[bad] == out[bad])
			bad++;
		printf("%s: sent %d bytes, %d came back, the first %d as sent\nFAIL\n",
		       argv[1], sent, n, bad);
		return 1;
	}
	printf("%s: %d bytes came back as sent\nok\n", argv[1], LEN);
	return 0;
}
