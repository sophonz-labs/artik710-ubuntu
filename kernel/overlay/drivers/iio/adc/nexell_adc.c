// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Nexell S5P6818 ADC (8 channels, 12 bit), raw reads only.
 *
 * Ported from the vendor driver drivers/iio/adc/nexell_adc.c in Samsung's
 * ARTIK 4.4 kernel (Copyright (C) 2016 Nexell Co., Ltd., Bon-gyu, KOO
 * <freestyle@nexell.co.kr>). Only the S5P6818 ("ADC_V2") block is kept.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <linux/iio/iio.h>

#define ADC_CON			0x00
#define ADC_DAT			0x04
#define ADC_INTENB		0x08
#define ADC_INTCLR		0x0c
#define ADC_PRESCON		0x10

#define ADC_CON_DATA_SEL(x)	(((x) & 0xf) << 10)
#define ADC_CON_CLK_CNT(x)	(((x) & 0xf) << 6)
#define ADC_CON_ASEL(x)		(((x) & 0x7) << 3)
#define ADC_CON_ADEN		BIT(0)
#define ADC_INT_BIT		BIT(0)
#define ADC_PRESCON_APEN	BIT(15)
#define ADC_PRESCON_PRES(x)	((x) & 0x3ff)

/* vendor values: DATA_SEL 0 = 5 clocks, CLK_CNT 6 for the 28 nm ADC */
#define ADC_DATA_SEL_VAL	0
#define ADC_CLK_CNT_VAL		6

/* stock DT sample_rate; the vendor divides by 6 (bits per sample) */
#define ADC_SAMPLE_RATE		200000
#define ADC_SAMPLE_BITS		6

#define ADC_TIMEOUT		msecs_to_jiffies(100)

struct nexell_adc {
	void __iomem *base;
	struct reset_control *rst;
	struct completion done;
	struct mutex lock;	/* one conversion at a time */
	u32 prescale;
	u32 value;
};

#define NEXELL_ADC_CHANNEL(_ch) {				\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.channel = _ch,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.datasheet_name = "ADC" #_ch,				\
}

static const struct iio_chan_spec nexell_adc_channels[] = {
	NEXELL_ADC_CHANNEL(0), NEXELL_ADC_CHANNEL(1),
	NEXELL_ADC_CHANNEL(2), NEXELL_ADC_CHANNEL(3),
	NEXELL_ADC_CHANNEL(4), NEXELL_ADC_CHANNEL(5),
	NEXELL_ADC_CHANNEL(6), NEXELL_ADC_CHANNEL(7),
};

static void nexell_adc_start(struct nexell_adc *adc, unsigned int ch)
{
	u32 con = readl(adc->base + ADC_CON);

	con &= ~(ADC_CON_ASEL(7) | ADC_CON_ADEN);
	con |= ADC_CON_ASEL(ch);
	writel(con, adc->base + ADC_CON);
	writel(con | ADC_CON_ADEN, adc->base + ADC_CON);
}

static int nexell_adc_hw_init(struct nexell_adc *adc)
{
	u32 pend;
	int ret;

	writel(ADC_CON_DATA_SEL(ADC_DATA_SEL_VAL) |
	       ADC_CON_CLK_CNT(ADC_CLK_CNT_VAL), adc->base + ADC_CON);
	writel(ADC_PRESCON_PRES(adc->prescale), adc->base + ADC_PRESCON);
	writel(ADC_PRESCON_PRES(adc->prescale) | ADC_PRESCON_APEN,
	       adc->base + ADC_PRESCON);

	/* The first conversion after power-on is invalid: run and drop it. */
	nexell_adc_start(adc, 0);
	ret = readl_poll_timeout(adc->base + ADC_INTCLR, pend,
				 pend & ADC_INT_BIT, 10, 100000);
	writel(ADC_INT_BIT, adc->base + ADC_INTCLR);
	readl(adc->base + ADC_DAT);

	writel(ADC_INT_BIT, adc->base + ADC_INTENB);

	return ret;
}

static irqreturn_t nexell_adc_isr(int irq, void *dev_id)
{
	struct nexell_adc *adc = dev_id;

	writel(ADC_INT_BIT, adc->base + ADC_INTCLR);
	adc->value = readl(adc->base + ADC_DAT);
	complete(&adc->done);

	return IRQ_HANDLED;
}

static int nexell_adc_read_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int *val, int *val2, long mask)
{
	struct nexell_adc *adc = iio_priv(indio_dev);

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	guard(mutex)(&adc->lock);

	reinit_completion(&adc->done);
	nexell_adc_start(adc, chan->channel);

	if (!wait_for_completion_timeout(&adc->done, ADC_TIMEOUT)) {
		dev_warn(indio_dev->dev.parent, "conversion timed out, resetting\n");
		reset_control_reset(adc->rst);
		nexell_adc_hw_init(adc);
		return -ETIMEDOUT;
	}

	*val = adc->value;
	return IIO_VAL_INT;
}

static const struct iio_info nexell_adc_info = {
	.read_raw = nexell_adc_read_raw,
};

static int nexell_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct nexell_adc *adc;
	struct clk *clk;
	unsigned long rate;
	int irq, ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	init_completion(&adc->done);
	ret = devm_mutex_init(dev, &adc->lock);
	if (ret)
		return ret;

	adc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(adc->base))
		return PTR_ERR(adc->base);

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "no clock\n");

	rate = clk_get_rate(clk);
	adc->prescale = rate / (ADC_SAMPLE_RATE * ADC_SAMPLE_BITS);
	if (!adc->prescale || adc->prescale > 0x3ff)
		return dev_err_probe(dev, -EINVAL, "clock %lu Hz out of range\n",
				     rate);

	adc->rst = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(adc->rst))
		return dev_err_probe(dev, PTR_ERR(adc->rst), "no reset\n");

	ret = reset_control_reset(adc->rst);
	if (ret)
		return dev_err_probe(dev, ret, "reset failed\n");

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, nexell_adc_isr, 0, dev_name(dev), adc);
	if (ret)
		return dev_err_probe(dev, ret, "cannot request irq\n");

	ret = nexell_adc_hw_init(adc);
	if (ret)
		return dev_err_probe(dev, ret, "first conversion timed out\n");

	/* same name as the stock driver, so stock scripts find the device */
	indio_dev->name = dev_name(dev);
	indio_dev->info = &nexell_adc_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = nexell_adc_channels;
	indio_dev->num_channels = ARRAY_SIZE(nexell_adc_channels);

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id nexell_adc_of_match[] = {
	{ .compatible = "nexell,s5p6818-adc" },
	{ }
};
MODULE_DEVICE_TABLE(of, nexell_adc_of_match);

static struct platform_driver nexell_adc_driver = {
	.probe = nexell_adc_probe,
	.driver = {
		.name = "nexell-adc",
		.of_match_table = nexell_adc_of_match,
	},
};
module_platform_driver(nexell_adc_driver);

MODULE_DESCRIPTION("Nexell S5P6818 ADC driver");
MODULE_LICENSE("GPL");
