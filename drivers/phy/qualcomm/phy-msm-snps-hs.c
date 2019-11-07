// SPDX-License-Identifier: GPL-2.0-only
/**
 * Copyright (C) 2016 Linaro Ltd
 */
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/reset.h>
#include <linux/io.h>

#define USB2_PHY_USB_PHY_UTMI_CTRL0		(0x3c)
#define OPMODE_MASK				(0x3 << 3)
#define OPMODE_NONDRIVING			(0x1 << 3)
#define SLEEPM					BIT(0)

#define USB2_PHY_USB_PHY_UTMI_CTRL5		(0x50)
#define POR					BIT(1)

#define USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON0	(0x54)
#define RETENABLEN				BIT(3)
#define FSEL_MASK				(0x7 << 4)
#define FSEL_DEFAULT				(0x3 << 4)

#define USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON1	(0x58)
#define VBUSVLDEXTSEL0				BIT(4)
#define PLLBTUNE				BIT(5)

#define USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON2	(0x5c)
#define VREGBYPASS				BIT(0)

#define USB2_PHY_USB_PHY_HS_PHY_CTRL1		(0x60)
#define VBUSVLDEXT0				BIT(0)

#define USB2_PHY_USB_PHY_HS_PHY_CTRL2		(0x64)
#define USB2_SUSPEND_N				BIT(2)
#define USB2_SUSPEND_N_SEL			BIT(3)

#define USB2_PHY_USB_PHY_CFG0			(0x94)
#define UTMI_PHY_DATAPATH_CTRL_OVERRIDE_EN	BIT(0)
#define UTMI_PHY_CMN_CTRL_OVERRIDE_EN		BIT(1)

#define USB2_PHY_USB_PHY_REFCLK_CTRL		(0xa0)
#define REFCLK_SEL_MASK				(0x3 << 0)
#define REFCLK_SEL_DEFAULT			(0x2 << 0)

#define USB2PHY_USB_PHY_RTUNE_SEL		(0xb4)
#define RTUNE_SEL				BIT(0)

#define TXPREEMPAMPTUNE0(x)			(x << 6)
#define TXPREEMPAMPTUNE0_MASK			(BIT(7) | BIT(6))
#define USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X0	0x6c
#define USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X1	0x70
#define USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X2	0x74
#define USB2PHY_USB_PHY_PARAMETER_OVERRIDE_X3	0x78
#define TXVREFTUNE0_MASK			0xF
#define PARAM_OVRD_MASK			0xFF

struct qcom_usb_hs_phy {
	void __iomem		*base;

	struct phy *phy;
	struct clk *ref_clk;
	struct regulator *vdd; // TODO
	struct regulator *v1p8;
	struct regulator *v3p3;
	struct reset_control *reset;

};

static int qcom_usb_hs_phy_set_mode(struct phy *phy,
				    enum phy_mode mode, int submode)
{
	return 0;
}

static void rmw(void __iomem *ptr, u32 mask, u32 val)
{
	u32 write_val, tmp = readl_relaxed(ptr);

	tmp &= ~mask;		/* retain other bits */
	write_val = tmp | val;

	writel_relaxed(write_val, ptr);

	/* Read back to see if val was written */
	tmp = readl_relaxed(ptr);
	tmp &= mask;		/* clear other bits */

	if (tmp != val)
		pr_err("%s: write: %x to QSCRATCH FAILED\n",
			__func__, val);
}

static int qcom_usb_hs_phy_power_on(struct phy *phy)
{
	struct qcom_usb_hs_phy *uphy = phy_get_drvdata(phy);
	int ret;

	ret = clk_prepare_enable(uphy->ref_clk);
	if (ret)
		return ret;

	ret = regulator_set_load(uphy->v1p8, 50000);
	if (ret < 0)
		goto err_1p8;

	ret = regulator_enable(uphy->v1p8);
	if (ret)
		goto err_1p8;

	ret = regulator_set_voltage_triplet(uphy->v3p3, 3050000, 3300000,
					    3300000);
	if (ret)
		goto err_3p3;

	ret = regulator_set_load(uphy->v3p3, 50000);
	if (ret < 0)
		goto err_3p3;

	ret = regulator_enable(uphy->v3p3);
	if (ret)
		goto err_3p3;

	if (uphy->reset) {
		ret = reset_control_reset(uphy->reset);
		if (ret)
			goto err_ulpi;
	}

	rmw(uphy->base + USB2_PHY_USB_PHY_CFG0, UTMI_PHY_CMN_CTRL_OVERRIDE_EN, UTMI_PHY_CMN_CTRL_OVERRIDE_EN);
	rmw(uphy->base + USB2_PHY_USB_PHY_UTMI_CTRL5, POR, POR);
	rmw(uphy->base + USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON0, FSEL_MASK, 0);
	rmw(uphy->base + USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON1, PLLBTUNE, PLLBTUNE);
	rmw(uphy->base + USB2_PHY_USB_PHY_REFCLK_CTRL, REFCLK_SEL_MASK, REFCLK_SEL_DEFAULT);
	rmw(uphy->base + USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON1, VBUSVLDEXTSEL0, VBUSVLDEXTSEL0);
	rmw(uphy->base + USB2_PHY_USB_PHY_HS_PHY_CTRL1, VBUSVLDEXT0, VBUSVLDEXT0);

	// from dtb
	writel_relaxed(0x40, uphy->base + 0x70);
	writel_relaxed(0x28, uphy->base + 0x74);

	// some stuff

	// rcal_mask = 0x1e00000
	// check rcal
	if (0)
		rmw(uphy->base + USB2PHY_USB_PHY_RTUNE_SEL, RTUNE_SEL, RTUNE_SEL);

	rmw(uphy->base + USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON2,
				VREGBYPASS, VREGBYPASS);

	rmw(uphy->base + USB2_PHY_USB_PHY_HS_PHY_CTRL2,
				USB2_SUSPEND_N_SEL | USB2_SUSPEND_N,
				USB2_SUSPEND_N_SEL | USB2_SUSPEND_N);

	rmw(uphy->base + USB2_PHY_USB_PHY_UTMI_CTRL0,
				SLEEPM, SLEEPM);

	rmw(uphy->base + USB2_PHY_USB_PHY_UTMI_CTRL5,
				POR, 0);

	rmw(uphy->base + USB2_PHY_USB_PHY_HS_PHY_CTRL2,
				USB2_SUSPEND_N_SEL, 0);

	rmw(uphy->base + USB2_PHY_USB_PHY_CFG0,
				UTMI_PHY_CMN_CTRL_OVERRIDE_EN, 0);

	return 0;
err_ulpi:
	regulator_disable(uphy->v3p3);
err_3p3:
	regulator_disable(uphy->v1p8);
err_1p8:
	clk_disable_unprepare(uphy->ref_clk);
	return ret;
}

static int qcom_usb_hs_phy_power_off(struct phy *phy)
{
	struct qcom_usb_hs_phy *uphy = phy_get_drvdata(phy);

	regulator_disable(uphy->v3p3);
	regulator_disable(uphy->v1p8);
	clk_disable_unprepare(uphy->ref_clk);

	return 0;
}

static const struct phy_ops qcom_usb_hs_phy_ops = {
	.power_on = qcom_usb_hs_phy_power_on,
	.power_off = qcom_usb_hs_phy_power_off,
	.set_mode = qcom_usb_hs_phy_set_mode,
	.owner = THIS_MODULE,
};

static int qcom_usb_hs_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qcom_usb_hs_phy *uphy;
	struct phy_provider *p;
	struct clk *clk;
	struct regulator *reg;
	struct reset_control *reset;
	struct resource *res;

	uphy = devm_kzalloc(dev, sizeof(*uphy), GFP_KERNEL);
	if (!uphy)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	uphy->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(uphy->base))
		return PTR_ERR(uphy->base);

	uphy->ref_clk = clk = devm_clk_get(dev, "ref");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	uphy->v1p8 = reg = devm_regulator_get(dev, "v1p8");
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	uphy->v3p3 = reg = devm_regulator_get(dev, "v3p3");
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	uphy->reset = reset = devm_reset_control_get(dev, "por");
	if (IS_ERR(reset)) {
		if (PTR_ERR(reset) == -EPROBE_DEFER)
			return PTR_ERR(reset);
		uphy->reset = NULL;
	}

	uphy->phy = devm_phy_create(dev, NULL, &qcom_usb_hs_phy_ops);
	if (IS_ERR(uphy->phy))
		return PTR_ERR(uphy->phy);

	dev_set_drvdata(dev, uphy);
	phy_set_drvdata(uphy->phy, uphy);

	p = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(p);
}

static const struct of_device_id qcom_usb_hs_phy_match[] = {
	{ .compatible = "qcom,usb-hs-phy-snps", },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_usb_hs_phy_match);

static struct platform_driver qcom_usb_hs_phy_driver = {
	.probe = qcom_usb_hs_phy_probe,
	.driver = {
		.name = "qcom_usb_hs_phy",
		.of_match_table = qcom_usb_hs_phy_match,
	},
};
module_platform_driver(qcom_usb_hs_phy_driver);

MODULE_DESCRIPTION("Qualcomm USB HS phy");
MODULE_LICENSE("GPL v2");
