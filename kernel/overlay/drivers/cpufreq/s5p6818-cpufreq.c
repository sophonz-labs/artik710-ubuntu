// SPDX-License-Identifier: GPL-2.0
/*
 * Nexell S5P6818: choose the CPU voltage table (ASV level) from the ECID
 * fuses and start cpufreq-dt with it.
 *
 * The device tree has the voltages of level 0 as opp-microvolt and those of
 * level N as opp-microvolt-asvN. The choice follows Samsung's 4.4 ARTIK
 * kernel, drivers/cpufreq/s5p6818-cpufreq.h, s5p6818_asv_setup_table(), which
 * prints the level counted from 1.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitrev.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/minmax.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>

/* shortcut: fixed address, as in the vendor's cpu-sys.c; a DT node if it ever moves */
#define ECID_BASE	0xc0067000
#define ECID_EC2	0x5c		/* bit 15: fuses readable */
#define ECID_KEY_READY	BIT(15)
#define ASV_LEVELS	5

/* IDS (mA) and RO limits of the five vendor tables, level 0 first */
static const u8 asv_ids[ASV_LEVELS] = { 6, 15, 38, 78, 78 };
static const u8 asv_ro[ASV_LEVELS] = { 90, 130, 170, 200, 200 };

static int __init s5p6818_asv_level(void)
{
	void __iomem *ecid;
	u32 ec2, ecid1, ecid2;
	int ids, ro, idslv, rolv, ret;

	ecid = ioremap(ECID_BASE, 0x100);
	if (!ecid)
		return -ENOMEM;

	ret = readl_poll_timeout(ecid + ECID_EC2, ec2, ec2 & ECID_KEY_READY,
				 10, 10000);
	ecid1 = readl(ecid + 0x4);
	ecid2 = readl(ecid + 0x8);
	iounmap(ecid);
	if (ret) {
		pr_err("ECID not ready\n");
		return ret;
	}

	/* The fields are stored with their bits reversed (vendor mtol()). */
	if (ecid2 & BIT(0)) {
		int gs = bitrev8((ecid2 >> 1) & 0x7) >> 5;
		int ag = bitrev8((ecid2 >> 4) & 0xf) >> 4;
		int level = max(ag - gs, 0);

		pr_info("ASV level %d: group %d, shift %d\n", level, ag, gs);
		/* the vendor indexes past its tables here; refuse instead */
		if (level >= ASV_LEVELS) {
			pr_err("ASV level %d has no table\n", level);
			return -EINVAL;
		}
		return level;
	}

	ids = bitrev8((ecid1 >> 16) & 0xff);
	ro = bitrev8((ecid1 >> 24) & 0xff);

	for (idslv = 0; idslv < ASV_LEVELS - 1; idslv++)
		if (asv_ids[idslv] >= ids)
			break;
	for (rolv = 0; rolv < ASV_LEVELS - 1; rolv++)
		if (asv_ro[rolv] >= ro)
			break;

	pr_info("ASV level %d: IDS %d mA, RO %d\n", min(idslv, rolv), ids, ro);
	return min(idslv, rolv);
}

static int __init s5p6818_cpufreq_init(void)
{
	struct dev_pm_opp_config config = {};
	struct platform_device *pdev;
	struct device *cpu_dev = get_cpu_device(0);
	char name[] = "asvN";
	unsigned int cpu;
	int level, ret;

	/* only boards whose device tree has the table */
	if (!of_machine_is_compatible("nexell,s5p6818") || !cpu_dev ||
	    !of_property_present(cpu_dev->of_node, "operating-points-v2"))
		return -ENODEV;

	level = s5p6818_asv_level();
	if (level < 0)
		return level;	/* no cpufreq: the CPUs stay where the boot loader left them */

	name[3] = '0' + level;
	config.prop_name = name;

	for_each_possible_cpu(cpu) {
		cpu_dev = get_cpu_device(cpu);
		if (!cpu_dev)
			return -ENODEV;
		/* the token is never released: this code is built in */
		ret = dev_pm_opp_set_config(cpu_dev, &config);
		if (ret < 0)
			return ret;
	}

	pdev = platform_device_register_simple("cpufreq-dt", -1, NULL, 0);
	return PTR_ERR_OR_ZERO(pdev);
}
device_initcall(s5p6818_cpufreq_init);
