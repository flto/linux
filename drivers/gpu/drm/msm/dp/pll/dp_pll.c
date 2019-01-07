// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 */

#include "dp_pll.h"

int msm_dp_pll_util_parse_dt_clock(struct platform_device *pdev,
					struct msm_dp_pll *pll)
{
	u32 i = 0, rc = 0;
	struct dss_module_power *mp = &pll->mp;
	const char *clock_name;
	u32 clock_rate;

	mp->num_clk = of_property_count_strings(pdev->dev.of_node,
							"clock-names");
	if (mp->num_clk <= 0) {
		DRM_ERROR("clocks are not defined\n");
		goto clk_err;
	}

	mp->clk_config = devm_kzalloc(&pdev->dev,
			sizeof(struct dss_clk) * mp->num_clk, GFP_KERNEL);
	if (!mp->clk_config) {
		rc = -ENOMEM;
		mp->num_clk = 0;
		goto clk_err;
	}

	for (i = 0; i < mp->num_clk; i++) {
		of_property_read_string_index(pdev->dev.of_node, "clock-names",
							i, &clock_name);
		strlcpy(mp->clk_config[i].clk_name, clock_name,
				sizeof(mp->clk_config[i].clk_name));

		of_property_read_u32_index(pdev->dev.of_node, "clock-rate",
							i, &clock_rate);
		mp->clk_config[i].rate = clock_rate;

		if (!clock_rate)
			mp->clk_config[i].type = DSS_CLK_AHB;
		else
			mp->clk_config[i].type = DSS_CLK_PCLK;
	}

clk_err:
	return rc;
}

struct msm_dp_pll *msm_dp_pll_init(struct platform_device *pdev,
			enum msm_dp_pll_type type, int id)
{
	struct device *dev = &pdev->dev;
	struct msm_dp_pll *pll;

	switch (type) {
	case MSM_DP_PLL_10NM:
		pll = msm_dp_pll_10nm_init(pdev, id);
		break;
	default:
		pll = ERR_PTR(-ENXIO);
		break;
	}

	if (IS_ERR(pll)) {
		DRM_DEV_ERROR(dev, "%s: failed to init DP PLL\n", __func__);
		return pll;
	}

	pll->type = type;

	DBG("DP:%d PLL registered", id);

	return pll;
}

static const struct of_device_id dp_pll_dt_match[] = {
#ifdef CONFIG_DRM_MSM_DP_10NM_PLL
	{ .compatible = "qcom,dp-pll-10nm" },
#endif
	{}
};

static int dp_pll_driver_probe(struct platform_device *pdev)
{
	struct msm_dp_pll *pll;
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	enum msm_dp_pll_type type;

	match = of_match_node(dp_pll_dt_match, dev->of_node);
	if (!match)
		return -ENODEV;

	if (!strcmp(match->compatible, "qcom,dp-pll-10nm"))
		type = MSM_DP_PLL_10NM;
	else
		type = MSM_DP_PLL_MAX;

	pll = msm_dp_pll_init(pdev, type, 0);
	if (IS_ERR_OR_NULL(pll)) {
		dev_info(dev,
			"%s: pll init failed: %ld, need separate pll clk driver\n",
			__func__, PTR_ERR(pll));
		return -ENODEV;
	}

	platform_set_drvdata(pdev, pll);

	return 0;
}

static int dp_pll_driver_remove(struct platform_device *pdev)
{
	struct msm_dp_pll *pll = platform_get_drvdata(pdev);

	if (pll) {
		//msm_dsi_pll_destroy(pll);
		pll = NULL;
	}

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver dp_pll_platform_driver = {
	.probe      = dp_pll_driver_probe,
	.remove     = dp_pll_driver_remove,
	.driver     = {
		.name   = "msm_dp_pll",
		.of_match_table = dp_pll_dt_match,
	},
};

void __init msm_dp_pll_driver_register(void)
{
	platform_driver_register(&dp_pll_platform_driver);
}

void __exit msm_dp_pll_driver_unregister(void)
{
	platform_driver_unregister(&dp_pll_platform_driver);
}
