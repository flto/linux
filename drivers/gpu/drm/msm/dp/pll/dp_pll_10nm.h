/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 */

#ifndef __DP_PLL_10NM_H
#define __DP_PLL_10NM_H

#include "dp_pll.h"
#include "dp_reg.h"

#define DP_VCO_HSCLK_RATE_1620MHZDIV1000	1620000UL
#define DP_VCO_HSCLK_RATE_2700MHZDIV1000	2700000UL
#define DP_VCO_HSCLK_RATE_5400MHZDIV1000	5400000UL
#define DP_VCO_HSCLK_RATE_8100MHZDIV1000	8100000UL

#define NUM_DP_CLOCKS_MAX			6

#define DP_PHY_PLL_POLL_SLEEP_US		500
#define DP_PHY_PLL_POLL_TIMEOUT_US		10000

#define DP_VCO_RATE_8100MHZDIV1000		8100000UL
#define DP_VCO_RATE_9720MHZDIV1000		9720000UL
#define DP_VCO_RATE_10800MHZDIV1000		10800000UL

struct dp_pll_10nm {
	struct msm_dp_pll base;

	int id;
	struct platform_device *pdev;

	void __iomem *pll_base;
	void __iomem *phy_base;
	void __iomem *ln_tx0_base;
	void __iomem *ln_tx1_base;

	/* private clocks: */
	struct clk_hw *hws[NUM_DP_CLOCKS_MAX];
	u32 num_hws;

	/* clock-provider: */
	struct clk_hw_onecell_data *hw_data;

	/* lane and orientation settings */
	u8 lane_cnt;
	u8 orientation;

	/* COM PHY settings */
	u32 hsclk_sel;
	u32 dec_start_mode0;
	u32 div_frac_start1_mode0;
	u32 div_frac_start2_mode0;
	u32 div_frac_start3_mode0;
	u32 integloop_gain0_mode0;
	u32 integloop_gain1_mode0;
	u32 vco_tune_map;
	u32 lock_cmp1_mode0;
	u32 lock_cmp2_mode0;
	u32 lock_cmp3_mode0;
	u32 lock_cmp_en;

	/* PHY vco divider */
	u32 phy_vco_div;
	/*
	 * Certain pll's needs to update the same vco rate after resume in
	 * suspend/resume scenario. Cached the vco rate for such plls.
	 */
	unsigned long	vco_cached_rate;
	u32		cached_cfg0;
	u32		cached_cfg1;
	u32		cached_outdiv;

	uint32_t index;
};

#define to_dp_pll_10nm(x)	container_of(x, struct dp_pll_10nm, base)

int dp_vco_set_rate_10nm(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate);
unsigned long dp_vco_recalc_rate_10nm(struct clk_hw *hw,
				unsigned long parent_rate);
long dp_vco_round_rate_10nm(struct clk_hw *hw, unsigned long rate,
				unsigned long *parent_rate);
int dp_vco_prepare_10nm(struct clk_hw *hw);
void dp_vco_unprepare_10nm(struct clk_hw *hw);
#endif /* __DP_PLL_10NM_H */
