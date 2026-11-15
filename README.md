# artik710-ubuntu

[![CI](https://github.com/sophonz-labs/artik710-ubuntu/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/sophonz-labs/artik710-ubuntu/actions/workflows/ci.yml)

Ubuntu 26.04 LTS (arm64) with Linux 7.2.15 for the Samsung ARTIK710 module, on the Raptor carrier board. Together they form the ARTIK 710 developer kit. The module has been discontinued for years, and this community project keeps it running a current Linux.

The image, the kernel and the installer are built from this repository.

## Status

All development and testing happened on one board, hardware revision 6. The table says what was checked and how.

| Interface | State | How it was checked |
|---|---|---|
| Serial console (UART3, 115200 baud) | works | used all the time |
| eMMC | works | holds the root filesystem |
| SD card slot | controller found | no card was available |
| Ethernet, 1 Gbit | works | link at 1000 Mb/s, DHCP, ssh |
| WiFi (BCM43455) | works | a scan finds the same networks at the same signal levels as the stock system, and the MAC address is the same; joining a network was not tested |
| Bluetooth (BCM43455) | works | controller comes up at 3 Mbaud with the address of the stock system, a scan finds devices; pairing was not tested |
| Zigbee (EM358x) | chip answers | it answers a reset on `/dev/ttySAC0`; the image has no Zigbee stack |
| USB host | hub found | the hub behind the HSIC port enumerates; nothing was plugged into the sockets |
| USB OTG port | device mode | the controller is there and a serial gadget binds; no host was connected |
| UART0, UART2, UART4 | work | internal loopback; the pins were not checked with a wire |
| I2C, 3 hardware and 8 GPIO buses | works | every bus scans, the PMIC answers |
| SPI2 (`/dev/spidev2.0`) | works | transfers through the FIFO and through DMA; clock, MOSI and MISO checked at the pins |
| PWM, outputs 0 to 2 | works | period measured, an output watched at its pin |
| ADC, 8 channels | works | the values of the stock system |
| GPIO | works | used by the tests |
| Watchdog | works | started by hand, it resets the board after a deliberate kernel panic; the image does not start it |
| Temperature sensor | works | reads about 50 C |
| RTC | works | runs |
| Power key | input device | nobody pressed it |
| HDMI | driver loads | reports "disconnected" without a monitor; no monitor was available, so no picture was seen |
| GPU (Mali-400, lima) | works | off-screen rendering with Mesa |
| Sound over HDMI | card plays | plays at the right speed; not heard through a monitor |
| Analog audio (ALC5658 codec) | untested | the codec does not answer on the test board, under the stock kernel either |
| CPU frequency scaling | works | 400 to 1400 MHz, as in the stock system; every frequency passes a load that checks its results, at the voltages of the stock system; the CPUs slow down at 80 C |

### Not ported

The stock kernel has drivers for these, and mainline Linux has none yet:

- LVDS and MIPI-DSI display outputs (panel connections).
- The video codec (VPU).
- The camera interface (VIP and MIPI-CSI).
- The data connection to the secure element (S3T9MF). Its clock and reset line can be driven from user space.

### Not tested

These need someone at the board, or hardware that was not available:

- `poweroff`. The PMIC switches the board off, and the handler is registered.
- Switching the power off and on. With release v0.1.0 installed, the test board once started with the PMIC at its power-on values (note 10 under [Notes on the hardware](#notes-on-the-hardware)), but it was not recorded whether the power switch was used.
- Suspend. The image refuses it (`/etc/systemd/sleep.conf.d/artik710.conf`): a board that does not wake up again has to be switched off and on by hand.
- An SD card.
- USB devices.
- A monitor.
- A wire between pins.

## Install

Installing puts this system on a board that runs the stock system. The stock system is the last image the module shipped with: Ubuntu 16.04 with a vendor kernel 4.4 (the kernel that the chip vendor built, not mainline Linux).

Requirements:

- The board runs the stock system and you have a root shell on it, through the serial console (the debug USB port, 115200 baud) or ssh.
- The board reaches github.com. If it does not, see [Without a network connection](#without-a-network-connection).
- For trying it: about 1.3 GB free on the root filesystem of the board.

Every step is one command on the board, as root. The installer has three modes.

### Try it

```
curl -fsSL https://github.com/sophonz-labs/artik710-ubuntu/releases/latest/download/install.sh | sh
```

This downloads the latest release to `/root/artik710-ubuntu`, compares the files with their checksums and installs them. The new system runs from an image file on the stock root filesystem (`/root/ubuntu-26.04.img`). The stock system stays as it is. Then reboot the board:

```
reboot
```

Inside the new system, the stock root filesystem is mounted on `/mnt`.

The board can come back with another IP address: on the test network, the stock system and the new one got different addresses from the same DHCP server.

In this mode, a shutdown ends with messages that a file system could not be unmounted. That is the stock root filesystem: the new system runs from a file on it. The data is written out before the board resets, and the next start replays the journal of that file system (`EXT4-fs (mmcblk0p7): recovery complete`).

### Go back

In the new system:

```
sudo sh /mnt/root/artik710-ubuntu/install.sh --restore
```

This puts the stock kernel back on the boot partition. Then reboot. In the stock system, before that reboot, the command is `sh /root/artik710-ubuntu/install.sh --restore`.

### Replace the stock system

```
curl -fsSL https://github.com/sophonz-labs/artik710-ubuntu/releases/latest/download/install.sh | sh -s -- --erase
```

At the next boot, the image is written over the whole root partition. This takes a few minutes, and everything on that partition is lost. Until that reboot, `--restore` still undoes the change.

In the new system, after trying it, the files are there already: `sudo sh /mnt/root/artik710-ubuntu/install.sh --erase`.

Back up anything you need from the board before `--erase`.

### Without a network connection

A release has six files, at https://github.com/sophonz-labs/artik710-ubuntu/releases :

| File | What it is |
|---|---|
| `Image` | the kernel |
| `s5p6818-artik710-raptor.dtb` | the device tree, which describes the board to the kernel |
| `uInitrd` | a small initramfs (a temporary root filesystem that starts the system) |
| `rootfs.ext4.xz` | the root filesystem image |
| `install.sh` | puts the files on the board |
| `SHA256SUMS` | checksums; `install.sh` verifies them |

Download them on a computer and copy them to the board. Replace BOARD with the address of the board:

```
gh release download --repo sophonz-labs/artik710-ubuntu --dir artik710-ubuntu
scp -r artik710-ubuntu root@BOARD:/root/
```

Without the `gh` tool, download the six files from the releases page into one directory.

Then run the installer on the board, in that directory: `sh install.sh`, `sh install.sh --erase` or `sh install.sh --restore`. With the files next to it, it downloads nothing.

### What the installer touches

- The release files stay in `/root/artik710-ubuntu`. `--restore` needs the installer from there.
- The kernel, the device tree and the initramfs go to the boot partition (eMMC partition 2). The stock files from there are saved in `/root/stock-boot`.
- The second boot partition (partition 3), the boot loaders and the U-Boot environment are not touched. U-Boot is the boot loader of the board. The stock U-Boot loads the new kernel from the boot partition, under the file names it already uses.
- Nothing in the boot loader area of the eMMC is written.
- When installing, the root filesystem is written first and the boot partition last. If the script stops before that, the board starts as it did before.
- After copying, the script reads the files back from the boot partition and compares them. If it says that the boot partition did not take a file, run it again before any reboot: U-Boot would start a damaged kernel.
- The stock system has the boot partition on `/boot`, read-only. The script makes it writable and sets it back to read-only at the end.
- On a machine that is not an ARTIK710, the script stops before it does anything.
- Do not reset or power off the board while the script runs. U-Boot does not read the journal of the boot partition, and the new files are complete for it only when the script has ended.

### Testing state of the installer

- Try mode and `--restore` were run on a real board with release v0.1.0: the command under [Try it](#try-it) on the stock system, `reboot` into the new system, `--restore` there, and `reboot` back into the stock system.
- `--erase` was tested only against an image file in CI. It has not been run on a board.

### WiFi and Bluetooth firmware

WiFi and Bluetooth need two files that cannot be shipped, because they are Samsung's. Every board has them in its stock system. The initramfs copies them into the new system automatically, including before `--erase` overwrites the stock system. If they are ever missing, they belong at these paths:

- `/usr/lib/firmware/brcm/brcmfmac43455-sdio.samsung,artik710-raptor.txt` (stock: `/etc/wifi/nvram.txt`)
- `/usr/lib/firmware/brcm/BCM4345C0.samsung,artik710-raptor.hcd` (stock: `/etc/bluetooth/BCM4345C0_003.001.025.0111.0205.hcd`)

## First steps

- Log in on the serial console or with ssh, as user `artik`, password `artik`. The system asks for a new password at the first login. `artik` may use `sudo`. `root` has no password and cannot log in.
- The SSH host keys are made at the first boot, so every board has its own.
- After `--erase`, the root filesystem grows to the size of its partition at the first boot.
- Ethernet uses DHCP.

### WiFi

WiFi uses netplan, the standard Ubuntu network configuration tool. Joining a network was not tested on the test board. Create the file `/etc/netplan/20-wifi.yaml`:

```yaml
network:
  version: 2
  wifis:
    wlan0:
      dhcp4: true
      access-points:
        "your network":
          password: "your password"
```

Then restrict the file and apply it:

```
sudo chmod 600 /etc/netplan/20-wifi.yaml
sudo netplan apply
```

### Self-test

```
sudo board-test
```

This prints one line per check: `ok`, `skip` (the check needs a cable, a card or a hand on the board) or `FAIL`. To run only some checks, name them:

```
sudo board-test uart i2c
```

The checks need nothing plugged in. Five of them do more than look:

- The CPU check loads all CPUs for three seconds.
- The Zigbee check resets the Zigbee chip.
- The SPI check sends data on SPI2.
- The PWM check runs output 2 for a moment.
- The audio check plays two seconds of silence.

### GPU

The image does not include Mesa, the open-source graphics libraries. Install it, then run the GPU check:

```
sudo apt install mesa-utils libgl1-mesa-dri libegl-mesa0
sudo board-test gpu
```

## Build from source

- You need Docker on an arm64 machine. CI builds on GitHub's `ubuntu-24.04-arm` runner. Development happened on an Apple Silicon Mac. An x86 machine was not tried.
- Everything runs in a container. The kernel tree and the root filesystem live in Docker volumes (`artik710-src`, `artik710-rootfs`, `artik710-ccache`), not in the checkout.
- A build from nothing takes about 15 minutes on the CI runner. The build downloads the kernel source from kernel.org and the packages from ports.ubuntu.com.

Run these in order:

```
docker build -t artik710-builder docker/builder
tools/kernel setup
tools/kernel build
tools/mk-initramfs
tools/mk-rootfs
tools/mk-release
```

What each step does:

- `docker build`: builds the build environment, Ubuntu 26.04 with the compiler and the image tools.
- `tools/kernel setup`: downloads Linux 7.2.15, applies the Armbian patches and this project's patches. Run it once per machine.
- `tools/kernel build`: builds the kernel, the device tree and the modules.
- `tools/mk-initramfs`: builds the initramfs.
- `tools/mk-rootfs`: builds the Ubuntu root filesystem with the modules and the test programs. `tools/mk-rootfs --fresh` starts it over.
- `tools/mk-release`: collects the six release files in `out/release/` and checks them.

The result in `out/release/` is what a release contains. Install it as described under [Install](#install).

## Repository layout

| Path | Contents |
|---|---|
| `kernel/patches/armbian-s5p6818-7.2/` | Armbian's S5P6818 patch series, unchanged (the source commit is in `SOURCE`). Armbian's NanoPi boards use the same chip |
| `kernel/patches/artik710/` | this project's changes to existing kernel files |
| `kernel/overlay/` | this project's new kernel files: the device tree of the board, the kernel configuration, new drivers |
| `rootfs/packages` | the Ubuntu packages in the image |
| `rootfs/overlay/` | files copied into the image |
| `tools/` | build scripts, the installer, tools for working with a board |
| `tests/` | test programs; they are installed in the image |
| `docker/builder/` | the build environment |

## Changing the kernel

The kernel tree lives in a Docker volume, not in the checkout, because macOS file systems are case-insensitive and the kernel tree is not.

Copy a kernel file out for editing:

```
tools/kernel pull drivers/spi/spi-s3c64xx.c
```

This copies the file to `kernel/work/`, which is not tracked by git. Edit it there.

Build the kernel with your change:

```
tools/kernel build
```

This copies `kernel/overlay/` and `kernel/work/` into the tree and builds it.

Save the changes to existing files as patches:

```
tools/kernel export
```

This writes the changes to existing files into `kernel/patches/artik710/`. Commit those patches. New files go straight into `kernel/overlay/`.

To open a shell in the kernel tree:

```
tools/kernel sh
```

## Working with a board

These tools are for development. They expect a board with the stock system installed, and ssh access to it as root with the key `~/.ssh/artik710_ed25519`.

| Tool | What it does |
|---|---|
| `tools/artik-ssh` | ssh to the board. `ARTIK_HOST` sets the address |
| `tools/artik-serial` | drives the serial console: sends commands, waits for output, talks to U-Boot. `ARTIK_TTY` sets the port |
| `tools/artik-testboot` | boots the kernel from `out/boot/` once, without installing it. If that kernel hangs, the watchdog resets the board and the installed system comes back |
| `tools/artik-push-rootfs` | copies the root filesystem image to the board for `artik-testboot` |

Example: boot a kernel that was built, together with the root filesystem you pushed:

```
tools/artik-push-rootfs
tools/artik-testboot root=/dev/mmcblk0p7
```

## Tests

The test programs are in `tests/`. They are installed in the image under `/usr/local/bin`.

| Program | What it checks |
|---|---|
| `board-test` | every hardware interface; see [First steps](#first-steps) |
| `uart-loopback /dev/ttySAC2` | a serial port, through the loopback inside the SoC. Add `wire` when TXD is wired to RXD |
| `spi-loop` | transfers on `/dev/spidev2.0`. `-w` compares the data when MOSI is wired to MISO |
| `zigbee-ash` | the Zigbee chip answers a reset |
| `smp-coherency` | TLB and cache invalidations reach every CPU (see [Notes on the hardware](#notes-on-the-hardware)) |
| `reclaim-coherency` | reads a file on every CPU while the kernel reclaims its pages |
| `stress-integrity` | compresses and hashes on every CPU and compares the results |
| `cpu-load` | computes on every CPU and compares the results: a CPU that is too fast for its voltage computes wrong |
| `cpufreq-step` | every CPU frequency, the lowest first: the clock and the voltage that the hardware really has, then `cpu-load` |

Some checks need no board. CI runs them with:

```
tools/check
```

- `tools/check lint` runs shellcheck and actionlint, the Python tool compiles, and the C tests build without a warning.
- `tools/check test` runs `tools/install.sh` against an image file (with dash and with bash as `sh`: the stock system has bash, the new one dash), the coherency and load tests (which must pass on any sound machine), and the self-check of the Zigbee test.

## CI and releases

- Every push to `main` and every pull request runs lint, the tests and a full build.
- When the build of a pull request has passed, a bot comments on the pull request with a link to the build output. Downloading the output needs a GitHub login. It is the same set of files as a release.
- A tag that starts with `v` publishes a release with the six files.

## Notes on the hardware

Findings from the port. Each is a short set of plain facts.

1. **Memory corruption on mainline kernels.** The two CPU clusters of the S5P6818 are joined by a CCI-400, the interconnect between them. It forwards only the older kind of TLB invalidation message (DVMv7). A TLB is the CPU's cache of address translations, and an invalidation tells the CPUs to drop stale entries. Of a 64-bit kernel's TLB invalidations, those by address do not reach the other cluster. Those by ASID (an address-space number) reach it only while the ASID is below 256. Without a workaround, stale translations stay in the other cluster, and user memory gets corrupted at random. `kernel/patches/artik710/0001-arm64-cci400-dvmv7-tlb-workaround.patch` makes the kernel use the invalidations that do arrive. `smp-coherency` shows the problem and the fix. This affects every S5P6818 board, not only this one.
2. **USB sockets.** They sit behind a USB3503 hub on the HSIC port. The hub appears only after the port power bit of the HSIC port is set in the EHCI controller, the USB 2.0 host controller.
3. **Display.** The boot loader has no display support and leaves the display blocks in reset. The display driver releases them.
4. **GPIO hogs.** A `gpio-hog` in the device tree makes the kernel panic at boot. The pin controller driver registers its GPIO chips before its tables are set up. Give the line to a driver instead.
5. **PWM clock.** The PWM block runs at 200 MHz on this board. The SoC file of the patch series assumes 100 MHz. The board file corrects it.
6. **PWM periods.** The driver refuses PWM periods above one second.
7. **SPI2 chip select.** The chip select (GPIOC10) reads high on the test board, whatever drives it, and also under the stock kernel. Transfers work. A device on that chip select was not tested.
8. **Audio codec.** The ALC5658 does not answer on the test board, also under the stock kernel. Its I2C lines have no pull-ups there.
9. **Timers.** The kernel uses the same 4 ms tick as the stock system. There is no high-resolution timer.
10. **Regulators.** The kernel sets six of the PMIC's regulators (the power management chip's voltage outputs). One is the rail of the CPUs, for the frequency scaling. Five are off after power-on (LDO1, 2, 5, 8 and 9): the stock kernel switches them on, and WiFi and Bluetooth do not work without them. The others stay as they are after power-on, which is what the stock system runs with. A reboot of the stock system does not show this, because the rails stay on through a reset of the SoC: test a change to the power rails after a power cycle. When the kernel starts from the power-on values, `sudo dmesg | grep 'LDO.*Bringing'` shows the rails that it moved to another voltage: LDO2, LDO5 and LDO9. With release v0.1.0 installed, the test board once started with these three lines in its log, and `board-test` passed for WiFi and Bluetooth.
11. **CPU voltage.** The voltage that the CPUs need at a frequency depends on the chip. Each chip has a level from 0 to 4 in its fuses, and the kernel has the voltage table of the stock kernel for each level. `sudo dmesg | grep ASV` shows the level. The test board has level 1; the other levels were not tested. `sudo cpufreq-step` checks every frequency of a board. A reset without a shutdown at 400 MHz, where the CPU rail has its lowest voltage, starts the board again. This was tried with `echo b > /proc/sysrq-trigger`, and with a kernel panic after the watchdog had been started by hand.
12. **Heat.** With all eight CPUs busy at 1400 MHz, the chip reaches 80 C in about half a minute. The kernel then lowers the frequency and holds the temperature there, at 1200 to 1300 MHz on the test board. At 105 C it shuts the system down.
13. **Addresses of WiFi and Bluetooth.** The BCM43455 has the factory addresses of both in its OTP memory. Its two enable lines (WL_REG_ON, BT_REG_ON) have pull-ups, so the chip keeps its power through a reset of the SoC, and it only starts afresh when both lines are low at the same time. After a reset without that, WiFi comes up with the default MAC address of its settings file, which the kernel replaces by a random one, and Bluetooth with `43:45:C0:00:1F:AC`. The device tree therefore drives BT_REG_ON low before the WiFi side starts. `ethtool -P wlan0` and `btmgmt info` show the addresses.
14. **Files on a nearly full ext4 partition.** Linux 7.0 and 7.2 can leave a stretch of zeros in a new file when files were removed from a nearly full ext4 filesystem just before: the blocks of the removed files are free for new data only after the next commit, and when the partition runs out of blocks in between, the kernel zeroes a part of the file that it has already written. Nothing reports it. This happened on the boot partition, which is 32 MiB and nearly full, and the board did not boot. Run `sync` between removing and copying, and compare the result. `install.sh` does both.
15. **Watchdog.** Nothing in the image starts the watchdog while the system runs. After a kernel panic, the board therefore stays stopped until someone resets it. At shutdown, systemd asks the watchdog for 10 minutes, which is more than it can count: the message `Failed to set watchdog hardware timeout to 10min` is harmless.
