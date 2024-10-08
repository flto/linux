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

static const struct freq_tbl ftbl_ext_mclk0_clk_src[] = {
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 lpass_audio_cc_wsa_mclk_clk_src = {
	.cmd_rcgr = 0x2a004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = lpass_cc_parent_map_0,
	.freq_tbl = ftbl_ext_mclk0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "lpass_audio_cc_wsa_mclk_clk_src",
		.parent_data = lpass_cc_parent_data_0,
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 lpass_audio_cc_rx_mclk_clk_src = {
	.cmd_rcgr = 0x2c004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = lpass_cc_parent_map_0,
	.freq_tbl = ftbl_ext_mclk0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "lpass_audio_cc_rx_mclk_clk_src",
		.parent_data = lpass_cc_parent_data_0,
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch lpass_audio_cc_wsa_mclk_2x_clk = {
	.halt_reg = 0x2a0cc,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x2a0cc,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2a0cc,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lpass_audio_cc_wsa_mclk_2x_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &lpass_audio_cc_wsa_mclk_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_wsa_mclk_clk = {
	.halt_reg = 0x2a0d4,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x2a0d4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2a0d4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lpass_audio_cc_wsa_mclk_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &lpass_audio_cc_wsa_mclk_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_wsa2_mclk_2x_clk = {
	.halt_reg = 0x2d000,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x2d000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2d000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lpass_audio_cc_wsa2_mclk_2x_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &lpass_audio_cc_wsa_mclk_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_wsa2_mclk_clk = {
	.halt_reg = 0x2d004,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x2d004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2d004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lpass_audio_cc_wsa2_mclk_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &lpass_audio_cc_wsa_mclk_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_rx_mclk_2x_clk = {
	.halt_reg = 0x2a0cc,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x2a0cc,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2a0cc,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lpass_audio_cc_rx_mclk_2x_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &lpass_audio_cc_rx_mclk_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_rx_mclk_clk = {
	.halt_reg = 0x2a0d4,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x2a0d4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2a0d4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lpass_audio_cc_rx_mclk_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &lpass_audio_cc_rx_mclk_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_rcg2 lpass_aon_cc_tx_mclk_clk_src = {
	.cmd_rcgr = 0x1b004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = lpass_cc_parent_map_0,
	.freq_tbl = ftbl_ext_mclk0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "lpass_aon_cc_tx_mclk_clk_src",
		.parent_data = lpass_cc_parent_data_0,
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch lpass_aon_cc_tx_mclk_clk = {
	.halt_reg = 0x1b014,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x1b014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1b014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lpass_aon_cc_tx_mclk_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &lpass_aon_cc_tx_mclk_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *lpass_aon_cc_sm8550_clocks[] = {
	[LPASS_AON_CC_TX_MCLK_CLK_SRC] = &lpass_aon_cc_tx_mclk_clk_src.clkr,
	[LPASS_AON_CC_TX_MCLK_CLK] = &lpass_aon_cc_tx_mclk_clk.clkr,
};

static struct clk_regmap *lpass_core_cc_sm8550_clocks[] = {
};

static struct clk_regmap *lpass_audio_cc_sm8550_clocks[] = {
	[LPASS_AUDIO_CC_WSA_MCLK_CLK_SRC] = &lpass_audio_cc_wsa_mclk_clk_src.clkr,
	[LPASS_AUDIO_CC_RX_MCLK_CLK_SRC] = &lpass_audio_cc_rx_mclk_clk_src.clkr,
	[LPASS_AUDIO_CC_WSA_MCLK_2X_CLK] = &lpass_audio_cc_wsa_mclk_2x_clk.clkr,
	[LPASS_AUDIO_CC_WSA_MCLK_CLK] = &lpass_audio_cc_wsa_mclk_clk.clkr,
	[LPASS_AUDIO_CC_WSA2_MCLK_2X_CLK] = &lpass_audio_cc_wsa2_mclk_2x_clk.clkr,
	[LPASS_AUDIO_CC_WSA2_MCLK_CLK] = &lpass_audio_cc_wsa2_mclk_clk.clkr,
	[LPASS_AUDIO_CC_RX_MCLK_2X_CLK] = &lpass_audio_cc_rx_mclk_2x_clk.clkr,
	[LPASS_AUDIO_CC_RX_MCLK_CLK] = &lpass_audio_cc_rx_mclk_clk.clkr,
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

	// enable moved to top cc
	//regmap_update_bits(regmap, 0x24000, BIT(0), BIT(0));

	//lpass_core_cc_lpm_core
	//lpass_core_cc_lpm_mem0_core
	regmap_update_bits(regmap, 0x1e000, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x1e004, BIT(0), BIT(0));

	//clk_lucid_evo_pll_configure(&lpass_lpaaudio_dig_pll, regmap,
	//			&lpass_lpaaudio_dig_pll_config);

	return qcom_cc_really_probe(&pdev->dev, &lpass_core_cc_sm8550_desc, regmap);
}

static int lpass_aon_cc_sm8550_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	lpass_core_cc_sm8550_regmap_config.name = "lpass_aon_cc";
	regmap = qcom_cc_map(pdev, &lpass_aon_cc_sm8550_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	// lpass_aon_cc_va_mem0_clk
	regmap_update_bits(regmap, 0x11060, BIT(0), BIT(0));

	return qcom_cc_really_probe(&pdev->dev, &lpass_aon_cc_sm8550_desc, regmap);
}

static int lpass_audio_cc_sm8550_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	lpass_core_cc_sm8550_regmap_config.name = "lpass_audio_cc";
	regmap = qcom_cc_map(pdev, &lpass_audio_cc_sm8550_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	//lpass_audio_cc_codec_mem0_clk
	//lpass_audio_cc_codec_mem1_clk
	//lpass_audio_cc_codec_mem2_clk
	//lpass_audio_cc_codec_mem3_clk
	regmap_update_bits(regmap, 0x2e004, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x2e010, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x2e01c, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x2e028, BIT(0), BIT(0));

	// lpass_audio_cc_tx_mclk_2x_wsa_clk
	// lpass_audio_cc_tx_mclk_wsa_clk
	regmap_update_bits(regmap, 0x2a0f0, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x2a0f4, BIT(0), BIT(0));

	// lpass_audio_cc_tx_mclk_2x_wsa2_clk
	// lpass_audio_cc_tx_mclk_wsa2_clk
	regmap_update_bits(regmap, 0x2d010, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x2d00c, BIT(0), BIT(0));

	// lpass_audio_cc_rx_mclk_2x_wsa_clk
	// lpass_audio_cc_rx_mclk_wsa_clk
	regmap_update_bits(regmap, 0x2c0dc, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x2c0e0, BIT(0), BIT(0));

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
