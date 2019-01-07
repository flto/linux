// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/iopoll.h>
#include <linux/delay.h>

#include "dp_pll.h"
#include "dp_pll_10nm.h"
#include "dp_extcon.h"

static int dp_vco_pll_init_db_10nm(struct msm_dp_pll *pll,
		unsigned long rate)
{
	struct dp_pll_10nm *dp_res = to_dp_pll_10nm(pll);
	u32 spare_value = 0;

	spare_value = PLL_REG_R(dp_res->phy_base, REG_DP_PHY_SPARE0);
	dp_res->lane_cnt = spare_value & 0x0F;
	dp_res->orientation = (spare_value & 0xF0) >> 4;

	DRM_DEBUG_DP("%s: spare_value=0x%x, ln_cnt=0x%x, orientation=0x%x\n",
			__func__, spare_value, dp_res->lane_cnt, dp_res->orientation);

	switch (rate) {
	case DP_VCO_HSCLK_RATE_1620MHZDIV1000:
		DRM_DEBUG_DP("%s: VCO rate: %ld\n", __func__,
				DP_VCO_RATE_9720MHZDIV1000);
		dp_res->hsclk_sel = 0x0c;
		dp_res->dec_start_mode0 = 0x69;
		dp_res->div_frac_start1_mode0 = 0x00;
		dp_res->div_frac_start2_mode0 = 0x80;
		dp_res->div_frac_start3_mode0 = 0x07;
		dp_res->integloop_gain0_mode0 = 0x3f;
		dp_res->integloop_gain1_mode0 = 0x00;
		dp_res->vco_tune_map = 0x00;
		dp_res->lock_cmp1_mode0 = 0x6f;
		dp_res->lock_cmp2_mode0 = 0x08;
		dp_res->lock_cmp3_mode0 = 0x00;
		dp_res->phy_vco_div = 0x1;
		dp_res->lock_cmp_en = 0x00;
		break;
	case DP_VCO_HSCLK_RATE_2700MHZDIV1000:
		DRM_DEBUG_DP("%s: VCO rate: %ld\n", __func__,
				DP_VCO_RATE_10800MHZDIV1000);
		dp_res->hsclk_sel = 0x04;
		dp_res->dec_start_mode0 = 0x69;
		dp_res->div_frac_start1_mode0 = 0x00;
		dp_res->div_frac_start2_mode0 = 0x80;
		dp_res->div_frac_start3_mode0 = 0x07;
		dp_res->integloop_gain0_mode0 = 0x3f;
		dp_res->integloop_gain1_mode0 = 0x00;
		dp_res->vco_tune_map = 0x00;
		dp_res->lock_cmp1_mode0 = 0x0f;
		dp_res->lock_cmp2_mode0 = 0x0e;
		dp_res->lock_cmp3_mode0 = 0x00;
		dp_res->phy_vco_div = 0x1;
		dp_res->lock_cmp_en = 0x00;
		break;
	case DP_VCO_HSCLK_RATE_5400MHZDIV1000:
		DRM_DEBUG_DP("%s: VCO rate: %ld\n", __func__,
				DP_VCO_RATE_10800MHZDIV1000);
		dp_res->hsclk_sel = 0x00;
		dp_res->dec_start_mode0 = 0x8c;
		dp_res->div_frac_start1_mode0 = 0x00;
		dp_res->div_frac_start2_mode0 = 0x00;
		dp_res->div_frac_start3_mode0 = 0x0a;
		dp_res->integloop_gain0_mode0 = 0x3f;
		dp_res->integloop_gain1_mode0 = 0x00;
		dp_res->vco_tune_map = 0x00;
		dp_res->lock_cmp1_mode0 = 0x1f;
		dp_res->lock_cmp2_mode0 = 0x1c;
		dp_res->lock_cmp3_mode0 = 0x00;
		dp_res->phy_vco_div = 0x2;
		dp_res->lock_cmp_en = 0x00;
		break;
	case DP_VCO_HSCLK_RATE_8100MHZDIV1000:
		DRM_DEBUG_DP("%s: VCO rate: %ld\n", __func__,
				DP_VCO_RATE_8100MHZDIV1000);
		dp_res->hsclk_sel = 0x03;
		dp_res->dec_start_mode0 = 0x69;
		dp_res->div_frac_start1_mode0 = 0x00;
		dp_res->div_frac_start2_mode0 = 0x80;
		dp_res->div_frac_start3_mode0 = 0x07;
		dp_res->integloop_gain0_mode0 = 0x3f;
		dp_res->integloop_gain1_mode0 = 0x00;
		dp_res->vco_tune_map = 0x00;
		dp_res->lock_cmp1_mode0 = 0x2f;
		dp_res->lock_cmp2_mode0 = 0x2a;
		dp_res->lock_cmp3_mode0 = 0x00;
		dp_res->phy_vco_div = 0x0;
		dp_res->lock_cmp_en = 0x08;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int dp_config_vco_rate_10nm(struct msm_dp_pll *pll,
		unsigned long rate)
{
	u32 res = 0;
	struct dp_pll_10nm *dp_res = to_dp_pll_10nm(pll);

	res = dp_vco_pll_init_db_10nm(pll, rate);
	if (res) {
		DRM_ERROR("VCO Init DB failed\n");
		return res;
	}

	if (dp_res->lane_cnt != 4) {
		if (dp_res->orientation == ORIENTATION_CC2)
			PLL_REG_W(dp_res->phy_base, REG_DP_PHY_PD_CTL, 0x6d);
		else
			PLL_REG_W(dp_res->phy_base, REG_DP_PHY_PD_CTL, 0x75);
	} else {
		PLL_REG_W(dp_res->phy_base, REG_DP_PHY_PD_CTL, 0x7d);
	}

	/* Make sure the PHY register writes are done */
	wmb();

	PLL_REG_W(dp_res->pll_base, QSERDES_COM_SVS_MODE_CLK_SEL, 0x01);
	PLL_REG_W(dp_res->pll_base, QSERDES_COM_SYSCLK_EN_SEL, 0x37);
	PLL_REG_W(dp_res->pll_base, QSERDES_COM_SYS_CLK_CTRL, 0x02);
	PLL_REG_W(dp_res->pll_base, QSERDES_COM_CLK_ENABLE1, 0x0e);
	PLL_REG_W(dp_res->pll_base, QSERDES_COM_SYSCLK_BUF_ENABLE, 0x06);
	PLL_REG_W(dp_res->pll_base, QSERDES_COM_CLK_SEL, 0x30);
	PLL_REG_W(dp_res->pll_base, QSERDES_COM_CMN_CONFIG, 0x02);

	/* Different for each clock rates */
	PLL_REG_W(dp_res->pll_base,
		QSERDES_COM_HSCLK_SEL, dp_res->hsclk_sel);
	PLL_REG_W(dp_res->pll_base,
		QSERDES_COM_DEC_START_MODE0, dp_res->dec_start_mode0);
	PLL_REG_W(dp_res->pll_base,
		QSERDES_COM_DIV_FRAC_START1_MODE0, dp_res->div_frac_start1_mode0);
	PLL_REG_W(dp_res->pll_base,
		QSERDES_COM_DIV_FRAC_START2_MODE0, dp_res->div_frac_start2_mode0);
	PLL_REG_W(dp_res->pll_base,
		QSERDES_COM_DIV_FRAC_START3_MODE0, dp_res->div_frac_start3_mode0);
	PLL_REG_W(dp_res->pll_base,
		QSERDES_COM_INTEGLOOP_GAIN0_MODE0, dp_res->integloop_gain0_mode0);
	PLL_REG_W(dp_res->pll_base,
		QSERDES_COM_INTEGLOOP_GAIN1_MODE0, dp_res->integloop_gain1_mode0);
	PLL_REG_W(dp_res->pll_base,
		QSERDES_COM_VCO_TUNE_MAP, dp_res->vco_tune_map);
	PLL_REG_W(dp_res->pll_base,
		QSERDES_COM_LOCK_CMP1_MODE0, dp_res->lock_cmp1_mode0);
	PLL_REG_W(dp_res->pll_base,
		QSERDES_COM_LOCK_CMP2_MODE0, dp_res->lock_cmp2_mode0);
	PLL_REG_W(dp_res->pll_base,
		QSERDES_COM_LOCK_CMP3_MODE0, dp_res->lock_cmp3_mode0);
	/* Make sure the PLL register writes are done */
	wmb();

	PLL_REG_W(dp_res->pll_base, QSERDES_COM_BG_TIMER, 0x0a);
	PLL_REG_W(dp_res->pll_base, QSERDES_COM_CORECLK_DIV_MODE0, 0x0a);
	PLL_REG_W(dp_res->pll_base, QSERDES_COM_VCO_TUNE_CTRL, 0x00);
	PLL_REG_W(dp_res->pll_base, QSERDES_COM_BIAS_EN_CLKBUFLR_EN, 0x3f);
	PLL_REG_W(dp_res->pll_base, QSERDES_COM_CORE_CLK_EN, 0x1f);
	PLL_REG_W(dp_res->pll_base, QSERDES_COM_PLL_IVCO, 0x07);
	PLL_REG_W(dp_res->pll_base,
		QSERDES_COM_LOCK_CMP_EN, dp_res->lock_cmp_en);
	PLL_REG_W(dp_res->pll_base, QSERDES_COM_PLL_CCTRL_MODE0, 0x36);
	PLL_REG_W(dp_res->pll_base, QSERDES_COM_PLL_RCTRL_MODE0, 0x16);
	PLL_REG_W(dp_res->pll_base, QSERDES_COM_CP_CTRL_MODE0, 0x06);
	/* Make sure the PHY register writes are done */
	wmb();

	if (dp_res->orientation == ORIENTATION_CC2)
		PLL_REG_W(dp_res->phy_base, REG_DP_PHY_MODE, 0x4c);
	else
		PLL_REG_W(dp_res->phy_base, REG_DP_PHY_MODE, 0x5c);
	/* Make sure the PLL register writes are done */
	wmb();

	/* TX Lane configuration */
	PLL_REG_W(dp_res->phy_base,
			REG_DP_PHY_TX0_TX1_LANE_CTL, 0x05);
	PLL_REG_W(dp_res->phy_base,
			REG_DP_PHY_TX2_TX3_LANE_CTL, 0x05);

	/* TX-0 register configuration */
	PLL_REG_W(dp_res->ln_tx0_base, TXn_TRANSCEIVER_BIAS_EN, 0x1a);
	PLL_REG_W(dp_res->ln_tx0_base, TXn_VMODE_CTRL1, 0x40);
	PLL_REG_W(dp_res->ln_tx0_base, TXn_PRE_STALL_LDO_BOOST_EN, 0x30);
	PLL_REG_W(dp_res->ln_tx0_base, TXn_INTERFACE_SELECT, 0x3d);
	PLL_REG_W(dp_res->ln_tx0_base, TXn_CLKBUF_ENABLE, 0x0f);
	PLL_REG_W(dp_res->ln_tx0_base, TXn_RESET_TSYNC_EN, 0x03);
	PLL_REG_W(dp_res->ln_tx0_base, TXn_TRAN_DRVR_EMP_EN, 0x03);
	PLL_REG_W(dp_res->ln_tx0_base,
		TXn_PARRATE_REC_DETECT_IDLE_EN, 0x00);
	PLL_REG_W(dp_res->ln_tx0_base, TXn_TX_INTERFACE_MODE, 0x00);
	PLL_REG_W(dp_res->ln_tx0_base, TXn_TX_BAND, 0x4);

	/* TX-1 register configuration */
	PLL_REG_W(dp_res->ln_tx1_base, TXn_TRANSCEIVER_BIAS_EN, 0x1a);
	PLL_REG_W(dp_res->ln_tx1_base, TXn_VMODE_CTRL1, 0x40);
	PLL_REG_W(dp_res->ln_tx1_base, TXn_PRE_STALL_LDO_BOOST_EN, 0x30);
	PLL_REG_W(dp_res->ln_tx1_base, TXn_INTERFACE_SELECT, 0x3d);
	PLL_REG_W(dp_res->ln_tx1_base, TXn_CLKBUF_ENABLE, 0x0f);
	PLL_REG_W(dp_res->ln_tx1_base, TXn_RESET_TSYNC_EN, 0x03);
	PLL_REG_W(dp_res->ln_tx1_base, TXn_TRAN_DRVR_EMP_EN, 0x03);
	PLL_REG_W(dp_res->ln_tx1_base,
		TXn_PARRATE_REC_DETECT_IDLE_EN, 0x00);
	PLL_REG_W(dp_res->ln_tx1_base, TXn_TX_INTERFACE_MODE, 0x00);
	PLL_REG_W(dp_res->ln_tx1_base, TXn_TX_BAND, 0x4);
	/* Make sure the PHY register writes are done */
	wmb();

	/* dependent on the vco frequency */
	PLL_REG_W(dp_res->phy_base, REG_DP_PHY_VCO_DIV, dp_res->phy_vco_div);

	return res;
}

static bool dp_10nm_pll_lock_status(struct dp_pll_10nm *dp_res)
{
	u32 status;
	bool pll_locked;

	/* poll for PLL lock status */
	if (readl_poll_timeout_atomic((dp_res->pll_base +
			QSERDES_COM_C_READY_STATUS),
			status,
			((status & BIT(0)) > 0),
			DP_PHY_PLL_POLL_SLEEP_US,
			DP_PHY_PLL_POLL_TIMEOUT_US)) {
		DRM_ERROR("%s: C_READY status is not high. Status=%x\n",
				__func__, status);
		pll_locked = false;
	} else {
		pll_locked = true;
	}

	return pll_locked;
}

static bool dp_10nm_phy_rdy_status(struct dp_pll_10nm *dp_res)
{
	u32 status;
	bool phy_ready = true;

	/* poll for PHY ready status */
	if (readl_poll_timeout_atomic((dp_res->phy_base +
			REG_DP_PHY_STATUS),
			status,
			((status & (BIT(1))) > 0),
			DP_PHY_PLL_POLL_SLEEP_US,
			DP_PHY_PLL_POLL_TIMEOUT_US)) {
		DRM_ERROR("%s: Phy_ready is not high. Status=%x\n",
				__func__, status);
		phy_ready = false;
	}

	return phy_ready;
}

static int dp_pll_enable_10nm(struct clk_hw *hw)
{
	int rc = 0;
	struct msm_dp_pll *pll = hw_clk_to_pll(hw);
	struct dp_pll_10nm *dp_res = to_dp_pll_10nm(pll);
	u32 bias_en, drvr_en;

	PLL_REG_W(dp_res->phy_base, REG_DP_PHY_AUX_CFG2, 0x04);
	PLL_REG_W(dp_res->phy_base, REG_DP_PHY_CFG, 0x01);
	PLL_REG_W(dp_res->phy_base, REG_DP_PHY_CFG, 0x05);
	PLL_REG_W(dp_res->phy_base, REG_DP_PHY_CFG, 0x01);
	PLL_REG_W(dp_res->phy_base, REG_DP_PHY_CFG, 0x09);
	wmb(); /* Make sure the PHY register writes are done */

	PLL_REG_W(dp_res->pll_base, QSERDES_COM_RESETSM_CNTRL, 0x20);
	wmb();	/* Make sure the PLL register writes are done */

	if (!dp_10nm_pll_lock_status(dp_res)) {
		rc = -EINVAL;
		goto lock_err;
	}

	PLL_REG_W(dp_res->phy_base, REG_DP_PHY_CFG, 0x19);
	/* Make sure the PHY register writes are done */
	wmb();
	/* poll for PHY ready status */
	if (!dp_10nm_phy_rdy_status(dp_res)) {
		rc = -EINVAL;
		goto lock_err;
	}

	DRM_DEBUG_DP("%s: PLL is locked\n", __func__);

	if (dp_res->lane_cnt == 1) {
		bias_en = 0x3e;
		drvr_en = 0x13;
	} else {
		bias_en = 0x3f;
		drvr_en = 0x10;
	}

	if (dp_res->lane_cnt != 4) {
		if (dp_res->orientation == ORIENTATION_CC1) {
			PLL_REG_W(dp_res->ln_tx1_base,
				TXn_HIGHZ_DRVR_EN, drvr_en);
			PLL_REG_W(dp_res->ln_tx1_base,
				TXn_TRANSCEIVER_BIAS_EN, bias_en);
		} else {
			PLL_REG_W(dp_res->ln_tx0_base,
				TXn_HIGHZ_DRVR_EN, drvr_en);
			PLL_REG_W(dp_res->ln_tx0_base,
				TXn_TRANSCEIVER_BIAS_EN, bias_en);
		}
	} else {
		PLL_REG_W(dp_res->ln_tx0_base, TXn_HIGHZ_DRVR_EN, drvr_en);
		PLL_REG_W(dp_res->ln_tx0_base,
			TXn_TRANSCEIVER_BIAS_EN, bias_en);
		PLL_REG_W(dp_res->ln_tx1_base, TXn_HIGHZ_DRVR_EN, drvr_en);
		PLL_REG_W(dp_res->ln_tx1_base,
			TXn_TRANSCEIVER_BIAS_EN, bias_en);
	}

	PLL_REG_W(dp_res->ln_tx0_base, TXn_TX_POL_INV, 0x0a);
	PLL_REG_W(dp_res->ln_tx1_base, TXn_TX_POL_INV, 0x0a);
	PLL_REG_W(dp_res->phy_base, REG_DP_PHY_CFG, 0x18);
	udelay(2000);

	PLL_REG_W(dp_res->phy_base, REG_DP_PHY_CFG, 0x19);

	/*
	 * Make sure all the register writes are completed before
	 * doing any other operation
	 */
	wmb();

	/* poll for PHY ready status */
	if (!dp_10nm_phy_rdy_status(dp_res)) {
		rc = -EINVAL;
		goto lock_err;
	}

	PLL_REG_W(dp_res->ln_tx0_base, TXn_TX_DRV_LVL, 0x38);
	PLL_REG_W(dp_res->ln_tx1_base, TXn_TX_DRV_LVL, 0x38);
	PLL_REG_W(dp_res->ln_tx0_base, TXn_TX_EMP_POST1_LVL, 0x20);
	PLL_REG_W(dp_res->ln_tx1_base, TXn_TX_EMP_POST1_LVL, 0x20);
	PLL_REG_W(dp_res->ln_tx0_base, TXn_RES_CODE_LANE_OFFSET_TX, 0x06);
	PLL_REG_W(dp_res->ln_tx1_base, TXn_RES_CODE_LANE_OFFSET_TX, 0x06);
	PLL_REG_W(dp_res->ln_tx0_base, TXn_RES_CODE_LANE_OFFSET_RX, 0x07);
	PLL_REG_W(dp_res->ln_tx1_base, TXn_RES_CODE_LANE_OFFSET_RX, 0x07);
	/* Make sure the PHY register writes are done */
	wmb();

lock_err:
	return rc;
}

static int dp_pll_disable_10nm(struct clk_hw *hw)
{
	int rc = 0;
	struct msm_dp_pll *pll = hw_clk_to_pll(hw);
	struct dp_pll_10nm *dp_res = to_dp_pll_10nm(pll);

	/* Assert DP PHY power down */
	PLL_REG_W(dp_res->phy_base, REG_DP_PHY_PD_CTL, 0x2);
	/*
	 * Make sure all the register writes to disable PLL are
	 * completed before doing any other operation
	 */
	wmb();

	return rc;
}


int dp_vco_prepare_10nm(struct clk_hw *hw)
{
	int rc = 0;
	struct msm_dp_pll *pll = hw_clk_to_pll(hw);
	struct dp_pll_10nm *dp_res = to_dp_pll_10nm(pll);

	DRM_DEBUG_DP("%s: rate = %ld\n", __func__, pll->rate);
	if ((dp_res->vco_cached_rate != 0)
		&& (dp_res->vco_cached_rate == pll->rate)) {
		rc = dp_vco_set_rate_10nm(hw,
			dp_res->vco_cached_rate, dp_res->vco_cached_rate);
		if (rc) {
			DRM_ERROR("index=%d vco_set_rate failed. rc=%d\n",
				rc, dp_res->index);
			goto error;
		}
	}

	rc = dp_pll_enable_10nm(hw);
	if (rc) {
		DRM_ERROR("ndx=%d failed to enable dp pll\n",
					dp_res->index);
		goto error;
	}

	pll->pll_on = true;
error:
	return rc;
}

void dp_vco_unprepare_10nm(struct clk_hw *hw)
{
	struct msm_dp_pll *pll = hw_clk_to_pll(hw);
	struct dp_pll_10nm *dp_res = to_dp_pll_10nm(pll);

	if (!dp_res) {
		DRM_ERROR("Invalid input parameter\n");
		return;
	}

	if (!pll->pll_on) {
		DRM_ERROR("pll resource can't be enabled\n");
		return;
	}
	dp_res->vco_cached_rate = pll->rate;
	dp_pll_disable_10nm(hw);

	pll->pll_on = false;
}

int dp_vco_set_rate_10nm(struct clk_hw *hw, unsigned long rate,
					unsigned long parent_rate)
{
	struct msm_dp_pll *pll = hw_clk_to_pll(hw);
	int rc;

	DRM_DEBUG_DP("DP lane CLK rate=%ld\n", rate);

	rc = dp_config_vco_rate_10nm(pll, rate);
	if (rc)
		DRM_ERROR("%s: Failed to set clk rate\n", __func__);

	pll->rate = rate;

	return 0;
}

unsigned long dp_vco_recalc_rate_10nm(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct msm_dp_pll *pll = hw_clk_to_pll(hw);
	struct dp_pll_10nm *dp_res = to_dp_pll_10nm(pll);
	u32 div, hsclk_div, link_clk_div = 0;
	u64 vco_rate;

	div = PLL_REG_R(dp_res->pll_base, QSERDES_COM_HSCLK_SEL);
	div &= 0x0f;

	if (div == 12)
		hsclk_div = 6; /* Default */
	else if (div == 4)
		hsclk_div = 4;
	else if (div == 0)
		hsclk_div = 2;
	else if (div == 3)
		hsclk_div = 1;
	else {
		DRM_DEBUG_DP("unknown divider. forcing to default\n");
		hsclk_div = 5;
	}

	div = PLL_REG_R(dp_res->phy_base, REG_DP_PHY_AUX_CFG2);
	div >>= 2;

	if ((div & 0x3) == 0)
		link_clk_div = 5;
	else if ((div & 0x3) == 1)
		link_clk_div = 10;
	else if ((div & 0x3) == 2)
		link_clk_div = 20;
	else
		DRM_ERROR("%s: unsupported div. Phy_mode: %d\n", __func__, div);

	if (link_clk_div == 20) {
		vco_rate = DP_VCO_HSCLK_RATE_2700MHZDIV1000;
	} else {
		if (hsclk_div == 6)
			vco_rate = DP_VCO_HSCLK_RATE_1620MHZDIV1000;
		else if (hsclk_div == 4)
			vco_rate = DP_VCO_HSCLK_RATE_2700MHZDIV1000;
		else if (hsclk_div == 2)
			vco_rate = DP_VCO_HSCLK_RATE_5400MHZDIV1000;
		else
			vco_rate = DP_VCO_HSCLK_RATE_8100MHZDIV1000;
	}

	DRM_DEBUG_DP("returning vco rate = %lu\n", (unsigned long)vco_rate);

	dp_res->vco_cached_rate = pll->rate = vco_rate;
	return (unsigned long)vco_rate;
}

long dp_vco_round_rate_10nm(struct clk_hw *hw, unsigned long rate,
			unsigned long *parent_rate)
{
	unsigned long rrate = rate;
	struct msm_dp_pll *pll = hw_clk_to_pll(hw);

	if (rate <= pll->min_rate)
		rrate = pll->min_rate;
	else if (rate <= DP_VCO_HSCLK_RATE_2700MHZDIV1000)
		rrate = DP_VCO_HSCLK_RATE_2700MHZDIV1000;
	else if (rate <= DP_VCO_HSCLK_RATE_5400MHZDIV1000)
		rrate = DP_VCO_HSCLK_RATE_5400MHZDIV1000;
	else
		rrate = pll->max_rate;

	DRM_DEBUG_DP("%s: rrate=%ld\n", __func__, rrate);

	*parent_rate = rrate;
	return rrate;
}

