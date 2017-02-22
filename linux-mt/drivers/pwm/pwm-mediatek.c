/*
 * Mediatek Pulse Width Modulator driver
 *
 * Copyright (C) 2015 John Crispin <blogic@openwrt.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/types.h>

#define NUM_PWM		7

/* PWM registers and bits definitions */
#define PWMCON			0x00
#define PWMHDUR			0x04
#define PWMLDUR			0x08
#define PWMGDUR			0x0c
#define PWMWAVENUM		0x28
#define PWMDWIDTH		0x2c
#define PWMTHRES		0x30

/**
 * struct mtk_pwm_chip - struct representing pwm chip
 *
 * @mmio_base: base address of pwm chip
 * @chip: linux pwm chip representation
 */
struct mtk_pwm_chip {
	void __iomem *mmio_base;
	struct pwm_chip chip;
	struct clk *clk_top;
	struct clk *clk_main;
	struct clk *clk_pwm[NUM_PWM];
};

static inline struct mtk_pwm_chip *to_mtk_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct mtk_pwm_chip, chip);
}

static inline u32 mtk_pwm_readl(struct mtk_pwm_chip *chip, unsigned int num,
				  unsigned long offset)
{
	return ioread32(chip->mmio_base + 0x10 + (num * 0x40) + offset);
}

static inline void mtk_pwm_writel(struct mtk_pwm_chip *chip,
				    unsigned int num, unsigned long offset,
				    unsigned long val)
{
	iowrite32(val, chip->mmio_base + 0x10 + (num * 0x40) + offset);
}

static int mtk_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			    int duty_ns, int period_ns)
{
	struct mtk_pwm_chip *pc = to_mtk_pwm_chip(chip);
	u32 resolution = 100 / 4;
	u32 clkdiv = 0;

	resolution = 1000000000 / (clk_get_rate(pc->clk_pwm[pwm->hwpwm]));

	while (period_ns / resolution  > 8191) {
		clkdiv++;
		resolution *= 2;
	}

	if (clkdiv > 7)
		return -1;

	mtk_pwm_writel(pc, pwm->hwpwm, PWMCON, BIT(15) | BIT(3) | clkdiv);
	mtk_pwm_writel(pc, pwm->hwpwm, PWMDWIDTH, period_ns / resolution);
	mtk_pwm_writel(pc, pwm->hwpwm, PWMTHRES, duty_ns / resolution);
	return 0;
}

static int mtk_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct mtk_pwm_chip *pc = to_mtk_pwm_chip(chip);
	u32 val;
	int ret;

	ret = clk_prepare(pc->clk_pwm[pwm->hwpwm]);
	if (ret < 0)
		return ret;

	val = ioread32(pc->mmio_base);
	val |= BIT(pwm->hwpwm);
	iowrite32(val, pc->mmio_base);

	return 0;
}

static void mtk_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct mtk_pwm_chip *pc = to_mtk_pwm_chip(chip);
	u32 val;

	val = ioread32(pc->mmio_base);
	val &= ~BIT(pwm->hwpwm);
	iowrite32(val, pc->mmio_base);
	clk_unprepare(pc->clk_pwm[pwm->hwpwm]);
}

static const struct pwm_ops mtk_pwm_ops = {
	.config = mtk_pwm_config,
	.enable = mtk_pwm_enable,
	.disable = mtk_pwm_disable,
	.owner = THIS_MODULE,
};

static int mtk_pwm_probe(struct platform_device *pdev)
{
	struct mtk_pwm_chip *pc;
	struct resource *r;
	int ret;

	pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pc->mmio_base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(pc->mmio_base))
		return PTR_ERR(pc->mmio_base);

	pc->clk_main = devm_clk_get(&pdev->dev, "main");
	if (IS_ERR(pc->clk_main))
		return PTR_ERR(pc->clk_main);

	pc->clk_top = devm_clk_get(&pdev->dev, "top");
	if (IS_ERR(pc->clk_top))
		return PTR_ERR(pc->clk_top);

	pc->clk_pwm[0] = devm_clk_get(&pdev->dev, "pwm1");
	if (IS_ERR(pc->clk_pwm[0]))
		return PTR_ERR(pc->clk_pwm[0]);

	pc->clk_pwm[1] = devm_clk_get(&pdev->dev, "pwm2");
	if (IS_ERR(pc->clk_pwm[1]))
		return PTR_ERR(pc->clk_pwm[1]);

	pc->clk_pwm[2] = devm_clk_get(&pdev->dev, "pwm3");
	if (IS_ERR(pc->clk_pwm[2]))
		return PTR_ERR(pc->clk_pwm[2]);

	pc->clk_pwm[3] = devm_clk_get(&pdev->dev, "pwm4");
	if (IS_ERR(pc->clk_pwm[3]))
		return PTR_ERR(pc->clk_pwm[3]);

	pc->clk_pwm[4] = devm_clk_get(&pdev->dev, "pwm5");
	if (IS_ERR(pc->clk_pwm[4]))
		return PTR_ERR(pc->clk_pwm[4]);

	ret = clk_prepare(pc->clk_top);
	if (ret < 0)
		return ret;

	ret = clk_prepare(pc->clk_main);
	if (ret < 0)
		goto disable_clk_top;

	platform_set_drvdata(pdev, pc);

	pc->chip.dev = &pdev->dev;
	pc->chip.ops = &mtk_pwm_ops;
	pc->chip.base = -1;
	pc->chip.npwm = NUM_PWM;

	ret = pwmchip_add(&pc->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		goto disable_clk_main;
	}

	return 0;

disable_clk_main:
	clk_unprepare(pc->clk_main);
disable_clk_top:
	clk_unprepare(pc->clk_top);

	return ret;
}

static int mtk_pwm_remove(struct platform_device *pdev)
{
	struct mtk_pwm_chip *pc = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < NUM_PWM; i++)
		pwm_disable(&pc->chip.pwms[i]);

	return pwmchip_remove(&pc->chip);
}

static const struct of_device_id mtk_pwm_of_match[] = {
	{ .compatible = "mediatek,mt7623-pwm" },
	{ }
};

MODULE_DEVICE_TABLE(of, mtk_pwm_of_match);

static struct platform_driver mtk_pwm_driver = {
	.driver = {
		.name = "mtk-pwm",
		.owner = THIS_MODULE,
		.of_match_table = mtk_pwm_of_match,
	},
	.probe = mtk_pwm_probe,
	.remove = mtk_pwm_remove,
};

module_platform_driver(mtk_pwm_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Crispin <blogic@openwrt.org>");
MODULE_ALIAS("platform:mtk-pwm");
