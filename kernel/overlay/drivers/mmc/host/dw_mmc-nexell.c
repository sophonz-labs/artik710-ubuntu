// SPDX-License-Identifier: GPL-2.0
/*
 * Nexell S5P6818 extension of the Synopsys DesignWare MMC host.
 *
 * The SoC puts a fixed divide-by-two between the "ciu" clock generator and the
 * controller's own card clock divider, and adds a register that sets the phase
 * of the drive and sample clocks.
 */
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "dw_mmc.h"
#include "dw_mmc-pltfm.h"

#define SDMMC_CLKCTRL			0x114
#define NEXELL_CLKCTRL_DRIVE_SHIFT(x)	((x) << 16)
#define NEXELL_CLKCTRL_SAMPLE_SHIFT(x)	((x) << 24)

static int dw_mci_nexell_init(struct dw_mci *host)
{
	/*
	 * The vendor driver's default phases; the ARTIK710 uses them for eMMC,
	 * SD and SDIO alike.
	 * shortcut: written at probe only, redo it on resume once system
	 * suspend is supported.
	 */
	mci_writel(host, CLKCTRL, NEXELL_CLKCTRL_DRIVE_SHIFT(2) |
				  NEXELL_CLKCTRL_SAMPLE_SHIFT(1));
	host->bus_hz /= 2;

	return 0;
}

static const struct dw_mci_drv_data nexell_drv_data = {
	.common_caps	= MMC_CAP_CMD23,
	.init		= dw_mci_nexell_init,
};

static const struct of_device_id dw_mci_nexell_match[] = {
	{ .compatible = "nexell,s5p6818-dw-mshc" },
	{}
};
MODULE_DEVICE_TABLE(of, dw_mci_nexell_match);

static int dw_mci_nexell_probe(struct platform_device *pdev)
{
	return dw_mci_pltfm_register(pdev, &nexell_drv_data);
}

static struct platform_driver dw_mci_nexell_driver = {
	.probe	= dw_mci_nexell_probe,
	.remove	= dw_mci_pltfm_remove,
	.driver	= {
		.name		= "dwmmc_nexell",
		.probe_type	= PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table	= dw_mci_nexell_match,
		.pm		= pm_ptr(&dw_mci_pmops),
	},
};
module_platform_driver(dw_mci_nexell_driver);

MODULE_DESCRIPTION("Nexell S5P6818 DW Multimedia Card driver");
MODULE_LICENSE("GPL");
