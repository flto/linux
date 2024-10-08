// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,sm8550-lpasscc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "common.h"
#include "gdsc.h"

enum {
	P_BI_TCXO,
	P_SLEEP_CLK,
};

static const struct parent_map lpass_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data lpass_cc_parent_data_0[] = {
	{ .fw_name = "bi_tcxo" },
};

#define _CLK_RCG2(reg, _name, mnd, parent, _freq_tbl, _ops) static struct clk_rcg2 _name = { \
	.cmd_rcgr = reg, \
	.mnd_width = mnd, \
	.hid_width = 5, \
	.parent_map = lpass_cc_parent_map_##parent, \
	.freq_tbl = _freq_tbl, \
	.clkr.hw.init = &(struct clk_init_data){ \
		.name = #_name, \
		.parent_data = lpass_cc_parent_data_##parent, \
		.num_parents = ARRAY_SIZE(lpass_cc_parent_data_##parent), \
		.ops = &_ops, \
	}, \
}
#define CLK_RCG2(reg, name, mnd, parent, freq_tbl) _CLK_RCG2(reg, name, mnd, parent, freq_tbl, clk_rcg2_ops)
#define CLK_RCG2_GP(reg, name, mnd, parent) _CLK_RCG2(reg, name, mnd, parent, NULL, clk_rcg2_gp_ops)

#define CLK_BRANCH(reg, _name, parent, has_hwctl) static struct clk_branch _name = { \
	.halt_reg = reg, \
	.halt_check = BRANCH_HALT, \
	.clkr = { \
		.enable_reg = reg, \
		.enable_mask = BIT(0), \
		.hw.init = &(struct clk_init_data){ \
			.name = #_name, \
			.parent_data = &(const struct clk_parent_data){ \
				.hw = &parent.clkr.hw, \
			}, \
			.num_parents = 1, \
			.flags = CLK_SET_RATE_PARENT, \
			.ops = &clk_branch2_ops, \
		}, \
	}, \
}

/* lpass_aon_cc */
CLK_RCG2(0x19000, lpass_aon_cc_main_clk_src, 0, 0, NULL);
CLK_RCG2(0x1b004, lpass_aon_cc_tx_mclk_clk_src, 0, 0, NULL);
CLK_BRANCH(0x11060, lpass_aon_cc_va_mem0_clk, lpass_aon_cc_main_clk_src, 1);
CLK_BRANCH(0x1b014, lpass_aon_cc_tx_mclk_clk, lpass_aon_cc_tx_mclk_clk_src, 1);

static struct clk_regmap *lpass_aon_cc_sm8550_clocks[] = {
	[LPASS_AON_CC_MAIN_CLK_SRC] = &lpass_aon_cc_main_clk_src.clkr,
	[LPASS_AON_CC_TX_MCLK_CLK_SRC] = &lpass_aon_cc_tx_mclk_clk_src.clkr,
	[LPASS_AON_CC_VA_MEM0_CLK] = &lpass_aon_cc_va_mem0_clk.clkr,
	[LPASS_AON_CC_TX_MCLK_CLK] = &lpass_aon_cc_tx_mclk_clk.clkr,
};

/* lpass_audio_cc */
CLK_RCG2_GP(0x19004, lpass_audio_cc_ext_if2_clk_src, 16, 0);
CLK_RCG2(0x2a004, lpass_audio_cc_wsa_mclk_clk_src, 8, 0, NULL);
CLK_RCG2(0x2c004, lpass_audio_cc_rx_mclk_clk_src, 8, 0, NULL);

CLK_BRANCH(0x1901c, lpass_audio_cc_ext_if2_ibit_clk, lpass_audio_cc_ext_if2_clk_src, 0);
CLK_BRANCH(0x2a0d4, lpass_audio_cc_wsa_mclk_clk, lpass_audio_cc_wsa_mclk_clk_src, 1);
CLK_BRANCH(0x2a0f4, lpass_audio_cc_tx_mclk_wsa_clk, lpass_aon_cc_tx_mclk_clk_src, 1);
CLK_BRANCH(0x2c0d4, lpass_audio_cc_rx_mclk_clk, lpass_audio_cc_rx_mclk_clk_src, 1);
CLK_BRANCH(0x2c0e0, lpass_audio_cc_tx_mclk_rx_clk, lpass_aon_cc_tx_mclk_clk_src, 1);
CLK_BRANCH(0x2d004, lpass_audio_cc_wsa2_mclk_clk, lpass_audio_cc_wsa_mclk_clk_src, 1);
CLK_BRANCH(0x2d00c, lpass_audio_cc_tx_mclk_wsa2_clk, lpass_aon_cc_tx_mclk_clk_src, 1);
CLK_BRANCH(0x2e000, lpass_audio_cc_codec_mem_clk, lpass_aon_cc_main_clk_src, 1);
CLK_BRANCH(0x2e004, lpass_audio_cc_codec_mem0_clk, lpass_aon_cc_main_clk_src, 1);
CLK_BRANCH(0x2e010, lpass_audio_cc_codec_mem1_clk, lpass_aon_cc_main_clk_src, 1);
CLK_BRANCH(0x2e01c, lpass_audio_cc_codec_mem2_clk, lpass_aon_cc_main_clk_src, 1);
CLK_BRANCH(0x2e028, lpass_audio_cc_codec_mem3_clk, lpass_aon_cc_main_clk_src, 1);

static struct clk_regmap *lpass_audio_cc_sm8550_clocks[] = {
	[LPASS_AUDIO_CC_EXT_IF2_IBIT_CLK] = &lpass_audio_cc_ext_if2_ibit_clk.clkr,
	[LPASS_AUDIO_CC_WSA_MCLK_CLK_SRC] = &lpass_audio_cc_wsa_mclk_clk_src.clkr,
	[LPASS_AUDIO_CC_RX_MCLK_CLK_SRC] = &lpass_audio_cc_rx_mclk_clk_src.clkr,
	[LPASS_AUDIO_CC_EXT_IF2_CLK_SRC] = &lpass_audio_cc_ext_if2_clk_src.clkr,
	[LPASS_AUDIO_CC_WSA_MCLK_CLK] = &lpass_audio_cc_wsa_mclk_clk.clkr,
	[LPASS_AUDIO_CC_WSA2_MCLK_CLK] = &lpass_audio_cc_wsa2_mclk_clk.clkr,
	[LPASS_AUDIO_CC_RX_MCLK_CLK] = &lpass_audio_cc_rx_mclk_clk.clkr,
	[LPASS_AUDIO_CC_CODEC_MEM_CLK] = &lpass_audio_cc_codec_mem_clk.clkr,
	[LPASS_AUDIO_CC_CODEC_MEM0_CLK] = &lpass_audio_cc_codec_mem0_clk.clkr,
	[LPASS_AUDIO_CC_CODEC_MEM1_CLK] = &lpass_audio_cc_codec_mem1_clk.clkr,
	[LPASS_AUDIO_CC_CODEC_MEM2_CLK] = &lpass_audio_cc_codec_mem2_clk.clkr,
	[LPASS_AUDIO_CC_CODEC_MEM3_CLK] = &lpass_audio_cc_codec_mem3_clk.clkr,
	[LPASS_AUDIO_CC_TX_MCLK_WSA_CLK] = &lpass_audio_cc_tx_mclk_wsa_clk.clkr,
	[LPASS_AUDIO_CC_TX_MCLK_RX_CLK] = &lpass_audio_cc_tx_mclk_rx_clk.clkr,
	[LPASS_AUDIO_CC_TX_MCLK_WSA2_CLK] = &lpass_audio_cc_tx_mclk_wsa2_clk.clkr,
};

/* lpass_core_cc */
CLK_RCG2_GP(0x10000, lpass_core_cc_ext_if0_clk_src, 16, 0);
CLK_RCG2_GP(0x11000, lpass_core_cc_ext_if1_clk_src, 16, 0);
CLK_RCG2_GP(0x12000, lpass_core_cc_ext_if2_clk_src, 16, 0);
CLK_RCG2(0x1d000, lpass_core_cc_core_clk_src, 8, 0, NULL);

CLK_BRANCH(0x10018, lpass_core_cc_ext_if0_ibit_clk, lpass_core_cc_ext_if0_clk_src, 0);
CLK_BRANCH(0x11018, lpass_core_cc_ext_if1_ibit_clk, lpass_core_cc_ext_if1_clk_src, 0);
CLK_BRANCH(0x12018, lpass_core_cc_ext_if2_ibit_clk, lpass_core_cc_ext_if2_clk_src, 0);
CLK_BRANCH(0x23000, lpass_core_cc_sysnoc_mport_core_clk, lpass_core_cc_core_clk_src, 1);

static struct clk_regmap *lpass_core_cc_sm8550_clocks[] = {
	[LPASS_CORE_CC_EXT_IF0_CLK_SRC] = &lpass_core_cc_ext_if0_clk_src.clkr,
	[LPASS_CORE_CC_EXT_IF1_CLK_SRC] = &lpass_core_cc_ext_if1_clk_src.clkr,
	[LPASS_CORE_CC_EXT_IF2_CLK_SRC] = &lpass_core_cc_ext_if2_clk_src.clkr,
	[LPASS_CORE_CC_CORE_CLK_SRC] = &lpass_core_cc_core_clk_src.clkr,
	[LPASS_CORE_CC_EXT_IF0_IBIT_CLK] = &lpass_core_cc_ext_if0_ibit_clk.clkr,
	[LPASS_CORE_CC_EXT_IF1_IBIT_CLK] = &lpass_core_cc_ext_if1_ibit_clk.clkr,
	[LPASS_CORE_CC_EXT_IF2_IBIT_CLK] = &lpass_core_cc_ext_if2_ibit_clk.clkr,
	[LPASS_CORE_CC_SYSNOC_MPORT_CORE_CLK] = &lpass_core_cc_sysnoc_mport_core_clk.clkr,
};

static struct gdsc lpass_audio_hm_gdsc = {
	.gdscr = 0x11090,
	.pd = {
		.name = "lpass_audio_hm_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = ALWAYS_ON,
};

static struct gdsc lpass_core_hm_gdsc = {
	.gdscr = 0x0,
	.pd = {
		.name = "lpass_core_hm_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE | ALWAYS_ON,
};

static struct gdsc *lpass_core_hm_sm8550_gdscs[] = {
	[LPASS_CORE_HM_GDSCR] = &lpass_core_hm_gdsc,
};

static struct gdsc *lpass_audio_hm_sm8550_gdscs[] = {
	[LPASS_AON_CC_LPASS_AUDIO_HM_GDSCR] = &lpass_audio_hm_gdsc,
};

static struct regmap_config lpass_core_cc_sm8550_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
};

static const struct qcom_cc_desc lpass_core_hm_sm8550_desc = {
	.config = &lpass_core_cc_sm8550_regmap_config,
	.gdscs = lpass_core_hm_sm8550_gdscs,
	.num_gdscs = ARRAY_SIZE(lpass_core_hm_sm8550_gdscs),
};

static const struct qcom_cc_desc lpass_core_cc_sm8550_desc = {
	.config = &lpass_core_cc_sm8550_regmap_config,
	.clks = lpass_core_cc_sm8550_clocks,
	.num_clks = ARRAY_SIZE(lpass_core_cc_sm8550_clocks),
};

static const struct qcom_cc_desc lpass_audio_cc_sm8550_desc = {
	.config = &lpass_core_cc_sm8550_regmap_config,
	.clks = lpass_audio_cc_sm8550_clocks,
	.num_clks = ARRAY_SIZE(lpass_audio_cc_sm8550_clocks),
};

static const struct qcom_cc_desc lpass_aon_cc_sm8550_desc = {
	.config = &lpass_core_cc_sm8550_regmap_config,
	.clks = lpass_aon_cc_sm8550_clocks,
	.num_clks = ARRAY_SIZE(lpass_aon_cc_sm8550_clocks),
	.gdscs = lpass_audio_hm_sm8550_gdscs,
	.num_gdscs = ARRAY_SIZE(lpass_audio_hm_sm8550_gdscs),
};

static int lpass_core_cc_sm8550_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	lpass_core_cc_sm8550_regmap_config.name = "lpass_core_cc";
	regmap = qcom_cc_map(pdev, &lpass_core_cc_sm8550_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return qcom_cc_really_probe(&pdev->dev, &lpass_core_cc_sm8550_desc, regmap);
}

static int lpass_aon_cc_sm8550_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	lpass_core_cc_sm8550_regmap_config.name = "lpass_aon_cc";
	regmap = qcom_cc_map(pdev, &lpass_aon_cc_sm8550_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return qcom_cc_really_probe(&pdev->dev, &lpass_aon_cc_sm8550_desc, regmap);
}

static int lpass_audio_cc_sm8550_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	lpass_core_cc_sm8550_regmap_config.name = "lpass_audio_cc";
	regmap = qcom_cc_map(pdev, &lpass_audio_cc_sm8550_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return qcom_cc_really_probe(&pdev->dev, &lpass_audio_cc_sm8550_desc, regmap);
}

static int lpass_hm_core_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	lpass_core_cc_sm8550_regmap_config.name = "lpass_hm_core";
	regmap = qcom_cc_map(pdev, &lpass_core_hm_sm8550_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	// lpass_top_cc_lpass_core_sway_ahb_ls
	// (depends on LPASS_CORE_HM being powered on)
	regmap_update_bits(regmap, 0x9000, BIT(0), BIT(0));

	return qcom_cc_really_probe(&pdev->dev, &lpass_core_hm_sm8550_desc, regmap);
}

static const struct of_device_id lpass_core_cc_sm8550_match_table[] = {
	{
		.compatible = "qcom,sm8550-lpasshm",
		.data = lpass_hm_core_probe,
	},
	{
		.compatible = "qcom,sm8550-lpasscorecc",
		.data = lpass_core_cc_sm8550_probe,
	},
	{
		.compatible = "qcom,sm8550-aoncc",
		.data = lpass_aon_cc_sm8550_probe,
	},
	{
		.compatible = "qcom,sm8550-audiocc",
		.data = lpass_audio_cc_sm8550_probe,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, lpass_core_cc_sm8550_match_table);

static int lpass_core_sm8550_probe(struct platform_device *pdev)
{
	int (*clk_probe)(struct platform_device *p);
	int ret;

	pm_runtime_enable(&pdev->dev);
	ret = pm_clk_create(&pdev->dev);
	if (ret)
		goto disable_pm_runtime;

	/*ret = pm_clk_add(&pdev->dev, "iface");
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to acquire iface clock\n");
		goto destroy_pm_clk;
	}*/

	ret = -EINVAL;
	clk_probe = of_device_get_match_data(&pdev->dev);
	if (!clk_probe)
		goto destroy_pm_clk;

	ret = clk_probe(pdev);
	if (ret)
		goto destroy_pm_clk;

	return 0;

destroy_pm_clk:
	//pm_clk_destroy(&pdev->dev);

disable_pm_runtime:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static const struct dev_pm_ops lpass_core_cc_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_clk_suspend, pm_clk_resume, NULL)
};

static struct platform_driver lpass_core_cc_sm8550_driver = {
	.probe = lpass_core_sm8550_probe,
	.driver = {
		.name = "lpass_core_cc-sm8550",
		.of_match_table = lpass_core_cc_sm8550_match_table,
		.pm = &lpass_core_cc_pm_ops,
	},
};

static int __init lpass_core_cc_sm8550_init(void)
{
	return platform_driver_register(&lpass_core_cc_sm8550_driver);
}
subsys_initcall(lpass_core_cc_sm8550_init);

static void __exit lpass_core_cc_sm8550_exit(void)
{
	platform_driver_unregister(&lpass_core_cc_sm8550_driver);
}
module_exit(lpass_core_cc_sm8550_exit);

MODULE_DESCRIPTION("QTI LPASS_CORE_CC SM8550 Driver");
MODULE_LICENSE("GPL v2");
