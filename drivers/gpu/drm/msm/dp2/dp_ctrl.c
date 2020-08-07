// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm-dp] %s: " fmt, __func__

#include <linux/types.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <drm/drm_fixed.h>
#include <drm/drm_dp_helper.h>
#include <linux/iopoll.h>
#include <linux/clk-provider.h>
#include <linux/rational.h>

#include "dp.xml.h"
#include "dp_display.h"

#define DP_KHZ_TO_HZ 1000
#define IDLE_PATTERN_COMPLETION_TIMEOUT_JIFFIES	(30 * HZ / 1000) /* 30 ms */
#define WAIT_FOR_VIDEO_READY_TIMEOUT_JIFFIES (HZ / 2)

#define MR_LINK_TRAINING1  0x8
#define MR_LINK_SYMBOL_ERM 0x80
#define MR_LINK_PRBS7 0x100
#define MR_LINK_CUSTOM80 0x200

#define POLLING_SLEEP_US			1000
#define POLLING_TIMEOUT_US			10000

#define REFTIMER_DEFAULT_VALUE			0x20000
#define SCRAMBLER_RESET_COUNT_VALUE		0xFC

int dp_ctrl_set_pattern(struct msm_dp *dp,
					u32 pattern)
{
	int bit, ret;
	u32 data;

	bit = BIT(pattern - 1);
	DRM_DEBUG_DP("hw: bit=%d train=%d\n", bit, pattern);
	dp_write(dp, REG_DP_STATE_CTRL, bit);

	bit = DP_MAINLINK_READY_LINK_TRAINING(BIT(pattern - 1));

	/* Poll for mainlink ready status */
	ret = readx_poll_timeout(readl, dp->base +
					REG_DP_MAINLINK_READY,
					data, data & bit,
					POLLING_SLEEP_US, POLLING_TIMEOUT_US);
	if (ret < 0) {
		DRM_ERROR("set pattern for link_train=%d failed\n", pattern);
		return ret;
	}
	return 0;
}

void dp_ctrl_reset(struct msm_dp *dp)
{
	u32 sw_reset;

	sw_reset = dp_read(dp, REG_DP_SW_RESET);

	sw_reset |= DP_SW_RESET_SW_RESET;
	dp_write(dp, REG_DP_SW_RESET, sw_reset);
	usleep_range(1000, 1100); /* h/w recommended delay */

	sw_reset &= ~DP_SW_RESET_SW_RESET;
	dp_write(dp, REG_DP_SW_RESET, sw_reset);
}

#include "dp_calc.h"

static void dp_ctrl_setup_tr_unit(struct msm_dp *dp)
{
	struct dp_vc_tu_mapping_table tu_calc_table;

	_dp_ctrl_calc_tu(&(struct dp_tu_calc_input) {
		.lclk = dp->phy_opts.dp.link_rate / 10,
		.pclk_khz = dp->mode.clock,
		.hactive = dp->mode.hdisplay,
		.hporch = dp->mode.htotal - dp->mode.hdisplay,
		.nlanes = dp->phy_opts.dp.lanes,
		.bpp = 24, /* XXX: use dp->mode_bpc */
		.pixel_enc = 444,
		.dsc_en = 0,
		.async_en = 0,
		.fec_en = 0,
		.num_of_dsc_slices = 0,
		.compress_ratio = 100,
	}, &tu_calc_table);

	dp_write(dp, REG_DP_VALID_BOUNDARY,
		 tu_calc_table.valid_boundary_link |
		 tu_calc_table.delay_start_link << 16);
	dp_write(dp, REG_DP_TU,
		 tu_calc_table.tu_size_minus1);
	dp_write(dp, REG_DP_VALID_BOUNDARY_2,
		 (tu_calc_table.boundary_moderation_en ? BIT(0) : 0) |
		 tu_calc_table.valid_lower_boundary_link << 1 |
		 tu_calc_table.upper_boundary_count << 16 |
		 tu_calc_table.lower_boundary_count << 20);
}

enum dp_link_voltage_level {
	DP_TRAIN_VOLTAGE_SWING_LVL_0	= 0,
	DP_TRAIN_VOLTAGE_SWING_LVL_1	= 1,
	DP_TRAIN_VOLTAGE_SWING_LVL_2	= 2,
	DP_TRAIN_VOLTAGE_SWING_MAX	= DP_TRAIN_VOLTAGE_SWING_LVL_2,
};

enum dp_link_preemaphasis_level {
	DP_TRAIN_PRE_EMPHASIS_LVL_0	= 0,
	DP_TRAIN_PRE_EMPHASIS_LVL_1	= 1,
	DP_TRAIN_PRE_EMPHASIS_LVL_2	= 2,
	DP_TRAIN_PRE_EMPHASIS_MAX	= DP_TRAIN_PRE_EMPHASIS_LVL_2,
};

static int dp_ctrl_update_vx_px(struct msm_dp *dp)
{
	int ret = 0, lane;
	u8 buf[4];
	u32 max_level_reached = 0;

	dp->phy_opts.dp.set_voltages = 1;
	phy_configure(dp->phy, &dp->phy_opts);
	dp->phy_opts.dp.set_voltages = 0;

	if (dp->phy_opts.dp.voltage[0] > DP_TRAIN_VOLTAGE_SWING_MAX)
		max_level_reached |= DP_TRAIN_MAX_SWING_REACHED;

	if (dp->phy_opts.dp.pre[0] == DP_TRAIN_PRE_EMPHASIS_MAX)
		max_level_reached  |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;

	for (lane = 0; lane <  dp->phy_opts.dp.lanes; lane++) {
		buf[lane] = dp->phy_opts.dp.voltage[0] |
			dp->phy_opts.dp.pre[0] << DP_TRAIN_PRE_EMPHASIS_SHIFT
			| max_level_reached;
	}

	ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_LANE0_SET,
					buf,  dp->phy_opts.dp.lanes);
	if (ret == dp->phy_opts.dp.lanes)
		ret = 0;

	return ret;
}

static bool dp_ctrl_train_pattern_set(struct msm_dp *dp,
		u8 pattern)
{
	u8 buf;
	int ret = 0;

	DRM_DEBUG_DP("sink: pattern=%x\n", pattern);

	buf = pattern;
	ret = drm_dp_dpcd_writeb(&dp->aux, DP_TRAINING_PATTERN_SET, buf);
	return ret == 1;
}

static int dp_ctrl_read_link_status(struct msm_dp *dp,
				    u8 *link_status)
{
	int len = 0;
	u32 const offset = DP_LANE_ALIGN_STATUS_UPDATED - DP_LANE0_1_STATUS;
	u32 link_status_read_max_retries = 100;

	while (--link_status_read_max_retries) {
		len = drm_dp_dpcd_read_link_status(&dp->aux,
			link_status);
		if (len != DP_LINK_STATUS_SIZE) {
			DRM_ERROR("DP link status read failed, err: %d\n", len);
			return len;
		}

		if (!(link_status[offset] & DP_LINK_STATUS_UPDATED))
			return 0;
	}

	return -ETIMEDOUT;
}

static int dp_link_adjust_levels(struct msm_dp *dp, u8 *link_status)
{
	int i;
	int v_max = 0, p_max = 0;

	/* use the max level across lanes */
	for (i = 0; i < dp->phy_opts.dp.lanes; i++) {
		u8 data_v = drm_dp_get_adjust_request_voltage(link_status, i);
		u8 data_p = drm_dp_get_adjust_request_pre_emphasis(link_status,
									 i);
		DRM_DEBUG_DP("lane=%d req_vol_swing=%d req_pre_emphasis=%d\n",
				i, data_v, data_p);
		if (v_max < data_v)
			v_max = data_v;
		if (p_max < data_p)
			p_max = data_p;
	}

	dp->phy_opts.dp.voltage[0] = v_max >> DP_TRAIN_VOLTAGE_SWING_SHIFT;
	dp->phy_opts.dp.pre[0] = p_max >> DP_TRAIN_PRE_EMPHASIS_SHIFT;

	/**
	 * Adjust the voltage swing and pre-emphasis level combination to within
	 * the allowable range.
	 */
	if (dp->phy_opts.dp.voltage[0] > DP_TRAIN_VOLTAGE_SWING_MAX) {
		DRM_DEBUG_DP("Requested vSwingLevel=%d, change to %d\n",
			dp->phy_opts.dp.voltage[0],
			DP_TRAIN_VOLTAGE_SWING_MAX);
		dp->phy_opts.dp.voltage[0] = DP_TRAIN_VOLTAGE_SWING_MAX;
	}

	if (dp->phy_opts.dp.pre[0] > DP_TRAIN_PRE_EMPHASIS_MAX) {
		DRM_DEBUG_DP("Requested preEmphasisLevel=%d, change to %d\n",
			dp->phy_opts.dp.pre[0],
			DP_TRAIN_PRE_EMPHASIS_MAX);
		dp->phy_opts.dp.pre[0] = DP_TRAIN_PRE_EMPHASIS_MAX;
	}

	if ((dp->phy_opts.dp.pre[0] > DP_TRAIN_PRE_EMPHASIS_LVL_1)
		&& (dp->phy_opts.dp.voltage[0] ==
			DP_TRAIN_VOLTAGE_SWING_LVL_2)) {
		DRM_DEBUG_DP("Requested preEmphasisLevel=%d, change to %d\n",
			dp->phy_opts.dp.pre[0],
			DP_TRAIN_PRE_EMPHASIS_LVL_1);
		dp->phy_opts.dp.pre[0] = DP_TRAIN_PRE_EMPHASIS_LVL_1;
	}

	DRM_DEBUG_DP("adjusted: v_level=%d, p_level=%d\n",
		dp->phy_opts.dp.voltage[0], dp->phy_opts.dp.pre[0]);

	return 0;
}

static int dp_ctrl_link_train_1(struct msm_dp *dp)
{
	int tries, old_v_level, ret = 0;
	u8 link_status[DP_LINK_STATUS_SIZE];
	int const maximum_retries = 5;

	dp_write(dp, REG_DP_STATE_CTRL, 0);

	ret = dp_ctrl_set_pattern(dp, DP_TRAINING_PATTERN_1);
	if (ret)
		return ret;
	dp_ctrl_train_pattern_set(dp, DP_TRAINING_PATTERN_1 |
		DP_LINK_SCRAMBLING_DISABLE);
	ret = dp_ctrl_update_vx_px(dp);
	if (ret)
		return ret;

	tries = 0;
	old_v_level = dp->phy_opts.dp.voltage[0];
	for (tries = 0; tries < maximum_retries; tries++) {
		drm_dp_link_train_clock_recovery_delay(dp->dpcd);

		ret = dp_ctrl_read_link_status(dp, link_status);
		if (ret)
			return ret;

		if (drm_dp_clock_recovery_ok(link_status, dp->phy_opts.dp.lanes)) {
			return ret;
		}

		if (dp->phy_opts.dp.voltage[0] >
			DP_TRAIN_VOLTAGE_SWING_MAX) {
			DRM_ERROR_RATELIMITED("max v_level reached\n");
			return -EAGAIN;
		}

		if (old_v_level != dp->phy_opts.dp.voltage[0]) {
			tries = 0;
			old_v_level = dp->phy_opts.dp.voltage[0];
		}

		DRM_DEBUG_DP("clock recovery not done, adjusting vx px\n");

		dp_link_adjust_levels(dp, link_status);
		ret = dp_ctrl_update_vx_px(dp);
		if (ret)
			return ret;
	}

	DRM_ERROR("max tries reached\n");
	return -ETIMEDOUT;
}

static void dp_ctrl_clear_training_pattern(struct msm_dp *dp)
{
	dp_ctrl_train_pattern_set(dp, DP_TRAINING_PATTERN_DISABLE);
	drm_dp_link_train_channel_eq_delay(dp->dpcd);
}

static int dp_ctrl_link_training_2(struct msm_dp *dp)
{
	int tries = 0, ret = 0;
	char pattern;
	int const maximum_retries = 5;
	u8 link_status[DP_LINK_STATUS_SIZE];

	dp_write(dp, REG_DP_STATE_CTRL, 0);

	if (drm_dp_tps3_supported(dp->dpcd))
		pattern = DP_TRAINING_PATTERN_3;
	else
		pattern = DP_TRAINING_PATTERN_2;

	ret = dp_ctrl_update_vx_px(dp);
	if (ret)
		return ret;

	ret = dp_ctrl_set_pattern(dp, pattern);
	if (ret)
		return ret;

	dp_ctrl_train_pattern_set(dp, pattern | DP_RECOVERED_CLOCK_OUT_EN);

	for (tries = 0; tries <= maximum_retries; tries++) {
		drm_dp_link_train_channel_eq_delay(dp->dpcd);

		ret = dp_ctrl_read_link_status(dp, link_status);
		if (ret)
			return ret;

		if (drm_dp_channel_eq_ok(link_status, dp->phy_opts.dp.lanes))
			return ret;

		dp_link_adjust_levels(dp, link_status);
		ret = dp_ctrl_update_vx_px(dp);
		if (ret)
			return ret;

	}

	return -ETIMEDOUT;
}

static int dp_ctrl_link_train(struct msm_dp *dp)
{
	int ret = 0;
	u8 encoding = DP_SET_ANSI_8B10B;

	dp->phy_opts.dp.pre[0] = 0;
	dp->phy_opts.dp.voltage[0] = 0;

	//dp_ctrl_config_ctrl(dp);

	{
		u8 values[2];
		int err;

		values[0] = drm_dp_link_rate_to_bw_code(dp->phy_opts.dp.link_rate * 100);
		values[1] = dp->phy_opts.dp.lanes;

		if (true)
			values[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

		err = drm_dp_dpcd_write(&dp->aux, DP_LINK_BW_SET, values, sizeof(values));
	}

	drm_dp_dpcd_write(&dp->aux, DP_MAIN_LINK_CHANNEL_CODING_SET,
				&encoding, 1);

	ret = dp_ctrl_link_train_1(dp);
	if (ret) {
		DRM_ERROR("link training #1 failed. ret=%d\n", ret);
		goto end;
	}

	/* print success info as this is a result of user initiated action */
	DRM_DEBUG_DP("link training #1 successful\n");

	ret = dp_ctrl_link_training_2(dp);
	if (ret) {
		DRM_ERROR("link training #2 failed. ret=%d\n", ret);
		goto end;
	}

	/* print success info as this is a result of user initiated action */
	DRM_DEBUG_DP("link training #2 successful\n");

end:
	dp_write(dp, REG_DP_STATE_CTRL, 0);

	dp_ctrl_clear_training_pattern(dp);
	return ret;
}

static int
dp_enable_ctrl_clock(struct msm_dp *dp)
{
	phy_configure(dp->phy, &dp->phy_opts);
	phy_power_on(dp->phy);
	return clk_bulk_prepare_enable(ARRAY_SIZE(dp->clk_ctrl), dp->clk_ctrl);
}

static int
dp_disable_ctrl_clock(struct msm_dp *dp)
{
	clk_bulk_disable_unprepare(ARRAY_SIZE(dp->clk_ctrl), dp->clk_ctrl);
	phy_power_off(dp->phy);
	return 0;
}

int dp_ctrl_on(struct msm_dp *dp)
{
	int ret = 0, i;
	u32 link_train_max_retries = 10;
	u32 config, data;

	dp_enable_ctrl_clock(dp);

	dp_ctrl_reset(dp);

	if (dp->mode.clock)
	{
	clk_set_rate(dp->pixel_clk, dp->mode.clock * 1000);
	clk_prepare_enable(dp->pixel_clk);

	{
	const struct drm_display_mode *mode = &dp->mode;

	dp_audio_setup(dp);

	dp_write(dp, REG_DP_MISC1_MISC0,
		DP_MISC1_MISC0_COLORIMETRY_CFG(DP_TEST_DYNAMIC_RANGE_VESA) |
		DP_MISC1_MISC0_TEST_BITS_DEPTH(dp->mode_bpc) |
		DP_MISC1_MISC0_SYNCHRONOUS_CLK);

	dp_write(dp, REG_DP_TOTAL_HOR_VER, mode->vtotal << 16 | mode->htotal);
	dp_write(dp, REG_DP_ACTIVE_HOR_VER, mode->vdisplay << 16 | mode->hdisplay);

	dp_write(dp, REG_DP_START_HOR_VER_FROM_SYNC,
		(mode->vtotal - mode->vsync_start) << 16 |
		(mode->htotal - mode->hsync_start));

	data = (mode->vsync_end - mode->vsync_start) << 16 |
	       (mode->hsync_end - mode->hsync_start);
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		data |= 1 << 31;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		data |= 1 << 15;

	dp_write(dp, REG_DP_HSYNC_VSYNC_WIDTH_POLARITY, data);

	/* XXX sm8150 has different p0 base */
	if (of_device_is_compatible(dp->pdev->dev.of_node, "qcom,sm8150-dp"))
		dp_write(dp, REG_DP_MMSS_DSC_DTO - 0x600, 0);
	else
		dp_write(dp, REG_DP_MMSS_DSC_DTO, 0);
	}
	{
	u32 rate = dp->phy_opts.dp.link_rate;
	u32 mvid, nvid;
	u32 const nvid_fixed = DP_LINK_CONSTANT_N_VALUE;
	unsigned long parent_rate, num, den;

	/* determine M/N values used for pixel clock, by finding
	 * the parent rate from the link rate and using the same
	 * logic as clk_rcg2_dp_set_rate
	 * note: DP PLL is using differient divisors than downstream
	 * for 5.4gbps and 8.1gbps link rates
	 */
	if (rate == 8100)
		parent_rate = rate * 1000 / 4;
	else
		parent_rate = rate * 1000 / 2;

	rational_best_approximation(parent_rate, dp->mode.clock,
			GENMASK(16 - 1, 0),
			GENMASK(16 - 1, 0), &den, &num);

	/* multiply by 5 why?*/
	mvid = num * 5;
	nvid = den;

	if (nvid < nvid_fixed) {
		u32 temp;

		temp = (nvid_fixed / nvid) * nvid;
		mvid = (nvid_fixed / nvid) * mvid;
		nvid = temp;
	}

	/* why ? */
	if (rate == 810000)
		nvid *= 2;

	dp_write(dp, REG_DP_SOFTWARE_MVID, mvid);
	dp_write(dp, REG_DP_SOFTWARE_NVID, nvid);
	}

	dp_ctrl_setup_tr_unit(dp);

	//dp_ctrl_wait4video_ready(dp);

	//
	}

	dp_write(dp, REG_DP_LOGICAL2PHYSICAL_LANE_MAPPING,
		0 | 1 << 2 | 2 << 4 | 3 << 6); /* 1:1 mapping */

	dp_write(dp, REG_DP_MAINLINK_CTRL, DP_MAINLINK_CTRL_FB_BOUNDARY_SEL);
	dp_write(dp, REG_DP_MAINLINK_CTRL, DP_MAINLINK_CTRL_FB_BOUNDARY_SEL |
					     DP_MAINLINK_CTRL_RESET);
	dp_write(dp, REG_DP_MAINLINK_CTRL, DP_MAINLINK_CTRL_FB_BOUNDARY_SEL);
	dp_write(dp, REG_DP_MAINLINK_CTRL, DP_MAINLINK_CTRL_FB_BOUNDARY_SEL |
					     DP_MAINLINK_CTRL_ENABLE);

	config = DP_CONFIGURATION_CTRL_LSCLK_DIV(2) | /* LSCLK DIV: 1/4 LCLK  */
		 DP_CONFIGURATION_CTRL_BPC(BPC_8) |
		 DP_CONFIGURATION_CTRL_NUM_OF_LANES(dp->phy_opts.dp.lanes - 1) |
		 DP_CONFIGURATION_CTRL_P_INTERLACED | /* progressive video */
		 /* sync clock & static Mvid */
		 DP_CONFIGURATION_CTRL_STATIC_DYNAMIC_CN |
		 DP_CONFIGURATION_CTRL_SYNC_ASYNC_CLK;

	/* Scrambler reset enable */
	if (dp->dpcd[DP_EDP_CONFIGURATION_CAP] & DP_ALTERNATE_SCRAMBLER_RESET_CAP)
		config |= DP_CONFIGURATION_CTRL_ASSR;

	if (drm_dp_enhanced_frame_cap(dp->dpcd))
		config |= DP_CONFIGURATION_CTRL_ENHANCED_FRAMING;

	dp_write(dp, REG_DP_CONFIGURATION_CTRL, config);

	/* exit power saving mode */
	if (dp->dpcd[DP_DPCD_REV] >= 0x11) {
		for (i = 0; i < 3; i++) {
			ret = drm_dp_dpcd_writeb(&dp->aux, DP_SET_POWER, DP_SET_POWER_D0);
			if (ret == 1)
				break;
			msleep(1);
		}
		if (ret != 1)
			DRM_WARN("failed to set power state\n");
	}

	do {
		ret = dp_ctrl_link_train(dp);
		if (!ret)
			break;

		if (--link_train_max_retries == 0) {
			printk("link training failed\n");
			break;
		}
	} while (1);

	dp->ctrl_on = true;

	if (dp->mode.clock) {
		dp_write(dp, REG_DP_STATE_CTRL, DP_STATE_CTRL_SEND_VIDEO);
		//if (!wait_for_completion_timeout(&dp->video_comp, HZ))
		//	DRM_ERROR("Link Train timedout\n");
	}

	/* Poll for mainlink ready status */
	ret = readx_poll_timeout(readl,
				dp->base +
				REG_DP_MAINLINK_READY,
				data, data & DP_MAINLINK_READY_FOR_VIDEO,
				POLLING_SLEEP_US, POLLING_TIMEOUT_US);
	if (ret < 0)
		DRM_WARN("mainlink not ready\n");

	return 0;
}

int dp_ctrl_off(struct msm_dp *dp)
{
	int ret = 0;
	u32 mainlink_ctrl;

	if (dp->mode.clock)
		clk_disable_unprepare(dp->pixel_clk);

	mainlink_ctrl = dp_read(dp, REG_DP_MAINLINK_CTRL);
	mainlink_ctrl &= ~DP_MAINLINK_CTRL_ENABLE;
	dp_write(dp, REG_DP_MAINLINK_CTRL, mainlink_ctrl);

	dp_ctrl_reset(dp);

	dp_disable_ctrl_clock(dp);

	dp->ctrl_on = false;

	DRM_DEBUG_DP("DP off done\n");
	return ret;
}
