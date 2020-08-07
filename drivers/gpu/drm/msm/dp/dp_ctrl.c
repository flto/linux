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
#include "dp_ctrl.h"
#include "dp_display.h"

#define DP_KHZ_TO_HZ 1000
#define IDLE_PATTERN_COMPLETION_TIMEOUT_JIFFIES	(30 * HZ / 1000) /* 30 ms */
#define WAIT_FOR_VIDEO_READY_TIMEOUT_JIFFIES (HZ / 2)

#define DP_CTRL_INTR_READY_FOR_VIDEO     BIT(0)
#define DP_CTRL_INTR_IDLE_PATTERN_SENT  BIT(3)

#define MR_LINK_TRAINING1  0x8
#define MR_LINK_SYMBOL_ERM 0x80
#define MR_LINK_PRBS7 0x100
#define MR_LINK_CUSTOM80 0x200

#define POLLING_SLEEP_US			1000
#define POLLING_TIMEOUT_US			10000

#define REFTIMER_DEFAULT_VALUE			0x20000
#define SCRAMBLER_RESET_COUNT_VALUE		0xFC

static inline u32
dp_read(struct dp_ctrl *ctrl, u32 reg)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
	return readl_relaxed(dp->base + reg);
}

static inline void
dp_write(struct dp_ctrl *ctrl, u32 reg, u32 data)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
	writel(data, dp->base + reg);
}

/* controller related catalog functions */
void dp_ctrl_update_transfer_unit(struct dp_ctrl *ctrl,
				u32 dp_tu, u32 valid_boundary,
				u32 valid_boundary2)
{
	dp_write(ctrl, REG_DP_VALID_BOUNDARY, valid_boundary);
	dp_write(ctrl, REG_DP_TU, dp_tu);
	dp_write(ctrl, REG_DP_VALID_BOUNDARY_2, valid_boundary2);
}

void dp_ctrl_mainlink_ctrl(struct dp_ctrl *ctrl, bool enable)
{
	u32 mainlink_ctrl;

	if (enable) {
		/*
		 * To make sure link reg writes happens before other operation,
		 * dp_write_link() function uses writel()
		 */
		dp_write(ctrl, REG_DP_MAINLINK_CTRL,
				DP_MAINLINK_CTRL_FB_BOUNDARY_SEL);
		dp_write(ctrl, REG_DP_MAINLINK_CTRL,
					DP_MAINLINK_CTRL_FB_BOUNDARY_SEL |
					DP_MAINLINK_CTRL_RESET);
		dp_write(ctrl, REG_DP_MAINLINK_CTRL,
					DP_MAINLINK_CTRL_FB_BOUNDARY_SEL);
		dp_write(ctrl, REG_DP_MAINLINK_CTRL,
					DP_MAINLINK_CTRL_FB_BOUNDARY_SEL |
					DP_MAINLINK_CTRL_ENABLE);
	} else {
		mainlink_ctrl = dp_read(ctrl, REG_DP_MAINLINK_CTRL);
		mainlink_ctrl &= ~DP_MAINLINK_CTRL_ENABLE;
		dp_write(ctrl, REG_DP_MAINLINK_CTRL, mainlink_ctrl);
	}
}

void dp_ctrl_config_msa(struct dp_ctrl *ctrl,
					u32 rate, u32 stream_rate_khz,
					bool fixed_nvid)
{
	u32 mvid, nvid;
	u32 const nvid_fixed = DP_LINK_CONSTANT_N_VALUE;
	unsigned long parent_rate, num, den;

	/* determine M/N values used for pixel clock, by finding
	 * the parent rate from the link rate and using the same
	 * logic as clk_rcg2_dp_set_rate
	 * note: DP PLL is using differient divisors than downstream
	 * for 5.4gbps and 8.1gbps link rates
	 */
	if (rate == 810000)
		parent_rate = rate * 10 / 4;
	else
		parent_rate = rate * 10 / 2;

	rational_best_approximation(parent_rate, stream_rate_khz,
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

	dp_write(ctrl, REG_DP_SOFTWARE_MVID, mvid);
	dp_write(ctrl, REG_DP_SOFTWARE_NVID, nvid);
}


int dp_ctrl_set_pattern(struct dp_ctrl *ctrl,
					u32 pattern)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
	int bit, ret;
	u32 data;

	bit = BIT(pattern - 1);
	DRM_DEBUG_DP("hw: bit=%d train=%d\n", bit, pattern);
	dp_write(ctrl, REG_DP_STATE_CTRL, bit);

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

void dp_ctrl_reset(struct dp_ctrl *ctrl)
{
	u32 sw_reset;

	sw_reset = dp_read(ctrl, REG_DP_SW_RESET);

	sw_reset |= DP_SW_RESET_SW_RESET;
	dp_write(ctrl, REG_DP_SW_RESET, sw_reset);
	usleep_range(1000, 1100); /* h/w recommended delay */

	sw_reset &= ~DP_SW_RESET_SW_RESET;
	dp_write(ctrl, REG_DP_SW_RESET, sw_reset);
}

bool dp_ctrl_mainlink_ready(struct dp_ctrl *ctrl)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
	u32 data;
	int ret;

	/* Poll for mainlink ready status */
	ret = readx_poll_timeout(readl,
				dp->base +
				REG_DP_MAINLINK_READY,
				data, data & DP_MAINLINK_READY_FOR_VIDEO,
				POLLING_SLEEP_US, POLLING_TIMEOUT_US);
	if (ret < 0) {
		DRM_ERROR("mainlink not ready\n");
		return false;
	}

	return true;
}

void dp_ctrl_enable_irq(struct dp_ctrl *ctrl, bool enable)
{
	if (enable) {
		dp_write(ctrl, REG_DP_INTR_STATUS,
				DP_INTERRUPT_STATUS1_MASK);
		dp_write(ctrl, REG_DP_INTR_STATUS2,
				DP_INTERRUPT_STATUS2_MASK);
	} else {
		dp_write(ctrl, REG_DP_INTR_STATUS, 0x00);
		dp_write(ctrl, REG_DP_INTR_STATUS2, 0x00);
	}
}

int dp_ctrl_get_interrupt(struct dp_ctrl *ctrl)
{
	u32 intr, intr_ack;

	intr = dp_read(ctrl, REG_DP_INTR_STATUS2);
	intr &= ~DP_INTERRUPT_STATUS2_MASK;
	intr_ack = (intr & DP_INTERRUPT_STATUS2)
			<< DP_INTERRUPT_STATUS_ACK_SHIFT;
	dp_write(ctrl, REG_DP_INTR_STATUS2,
			intr_ack | DP_INTERRUPT_STATUS2_MASK);

	return intr;
}

void dp_ctrl_push_idle(struct dp_ctrl *ctrl)
{
	mutex_lock(&ctrl->push_idle_mutex);

	reinit_completion(&ctrl->idle_comp);
	dp_write(ctrl, REG_DP_STATE_CTRL, DP_STATE_CTRL_PUSH_IDLE);

	if (!wait_for_completion_timeout(&ctrl->idle_comp,
			IDLE_PATTERN_COMPLETION_TIMEOUT_JIFFIES))
		pr_warn("PUSH_IDLE pattern timedout\n");

	mutex_unlock(&ctrl->push_idle_mutex);
	pr_debug("mainlink off done\n");
}

static void dp_ctrl_config_ctrl(struct dp_ctrl *ctrl)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
	u32 config = 0;

	/* Default-> LSCLK DIV: 1/4 LCLK  */
	config |= DP_CONFIGURATION_CTRL_LSCLK_DIV(2);;

	/* Scrambler reset enable */
	if (dp->dpcd[DP_EDP_CONFIGURATION_CAP] & DP_ALTERNATE_SCRAMBLER_RESET_CAP)
		config |= DP_CONFIGURATION_CTRL_ASSR;

	config |= DP_CONFIGURATION_CTRL_BPC(dp->mode_bpc);

	/* Num of Lanes */
	config |= DP_CONFIGURATION_CTRL_NUM_OF_LANES(dp->phy_opts.dp.lanes - 1);

	if (drm_dp_enhanced_frame_cap(dp->dpcd))
		config |= DP_CONFIGURATION_CTRL_ENHANCED_FRAMING;

	config |= DP_CONFIGURATION_CTRL_P_INTERLACED; /* progressive video */

	/* sync clock & static Mvid */
	config |= DP_CONFIGURATION_CTRL_STATIC_DYNAMIC_CN;
	config |= DP_CONFIGURATION_CTRL_SYNC_ASYNC_CLK;

	dp_write(ctrl, REG_DP_CONFIGURATION_CTRL, config);
}

static void dp_ctrl_configure_source_params(struct dp_ctrl *ctrl)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
	const struct drm_display_mode *mode = &dp->mode;
	u32 data;

	dp_write(ctrl, REG_DP_LOGICAL2PHYSICAL_LANE_MAPPING,
		0 | 1 << 2 | 2 << 4 | 3 << 6); /* 1:1 mapping */

	dp_ctrl_mainlink_ctrl(ctrl, true);
	dp_ctrl_config_ctrl(ctrl);

	dp_write(ctrl, REG_DP_MISC1_MISC0,
		DP_MISC1_MISC0_COLORIMETRY_CFG(DP_TEST_DYNAMIC_RANGE_VESA) |
		DP_MISC1_MISC0_TEST_BITS_DEPTH(dp->mode_bpc) |
		DP_MISC1_MISC0_SYNCHRONOUS_CLK);

	dp_write(ctrl, REG_DP_TOTAL_HOR_VER, mode->vtotal << 16 | mode->htotal);
	dp_write(ctrl, REG_DP_ACTIVE_HOR_VER, mode->vdisplay << 16 | mode->hdisplay);

	dp_write(ctrl, REG_DP_START_HOR_VER_FROM_SYNC,
		(mode->vtotal - mode->vsync_start) << 16 |
		(mode->htotal - mode->hsync_start));

	data = (mode->vsync_end - mode->vsync_start) << 16 |
	       (mode->hsync_end - mode->hsync_start);
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		data |= 1 << 31;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		data |= 1 << 15;

	dp_write(ctrl, REG_DP_HSYNC_VSYNC_WIDTH_POLARITY, data);

	/* XXX sm8150 has different p0 base */
	if (of_device_is_compatible(dp->pdev->dev.of_node, "qcom,sm8150-dp"))
		dp_write(ctrl, REG_DP_MMSS_DSC_DTO - 0x600, 0);
	else
		dp_write(ctrl, REG_DP_MMSS_DSC_DTO, 0);
}

#include "dp_calc.h"

static void dp_ctrl_calc_tu_parameters(struct dp_ctrl *ctrl,
		struct dp_vc_tu_mapping_table *tu_table)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
	struct dp_tu_calc_input in;

	in.lclk = dp->phy_opts.dp.link_rate / 10;
	in.pclk_khz = dp->mode.clock;
	in.hactive = dp->mode.hdisplay;
	in.hporch = dp->mode.htotal - dp->mode.hdisplay;
	in.nlanes = dp->phy_opts.dp.lanes;
	in.bpp = 24; /* XXX: use dp->mode_bpc */
	in.pixel_enc = 444;
	in.dsc_en = 0;
	in.async_en = 0;
	in.fec_en = 0;
	in.num_of_dsc_slices = 0;
	in.compress_ratio = 100;

	_dp_ctrl_calc_tu(&in, tu_table);
}

static void dp_ctrl_setup_tr_unit(struct dp_ctrl *ctrl)
{
	u32 dp_tu = 0x0;
	u32 valid_boundary = 0x0;
	u32 valid_boundary2 = 0x0;
	struct dp_vc_tu_mapping_table tu_calc_table;

	dp_ctrl_calc_tu_parameters(ctrl, &tu_calc_table);

	dp_tu |= tu_calc_table.tu_size_minus1;
	valid_boundary |= tu_calc_table.valid_boundary_link;
	valid_boundary |= (tu_calc_table.delay_start_link << 16);

	valid_boundary2 |= (tu_calc_table.valid_lower_boundary_link << 1);
	valid_boundary2 |= (tu_calc_table.upper_boundary_count << 16);
	valid_boundary2 |= (tu_calc_table.lower_boundary_count << 20);

	if (tu_calc_table.boundary_moderation_en)
		valid_boundary2 |= BIT(0);

	pr_debug("dp_tu=0x%x, valid_boundary=0x%x, valid_boundary2=0x%x\n",
			dp_tu, valid_boundary, valid_boundary2);

	dp_ctrl_update_transfer_unit(ctrl,
				dp_tu, valid_boundary, valid_boundary2);
}

static int dp_ctrl_wait4video_ready(struct dp_ctrl *ctrl)
{
	int ret = 0;

	if (!wait_for_completion_timeout(&ctrl->video_comp,
				WAIT_FOR_VIDEO_READY_TIMEOUT_JIFFIES)) {
		DRM_ERROR("Link Train timedout\n");
		ret = -ETIMEDOUT;
	}
	return ret;
}

static int dp_ctrl_update_vx_px(struct dp_ctrl *ctrl)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
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

	ret = drm_dp_dpcd_write(ctrl->aux, DP_TRAINING_LANE0_SET,
					buf,  dp->phy_opts.dp.lanes);
	if (ret == dp->phy_opts.dp.lanes)
		ret = 0;

	return ret;
}

static bool dp_ctrl_train_pattern_set(struct dp_ctrl *ctrl,
		u8 pattern)
{
	u8 buf;
	int ret = 0;

	DRM_DEBUG_DP("sink: pattern=%x\n", pattern);

	buf = pattern;
	ret = drm_dp_dpcd_writeb(ctrl->aux,
					DP_TRAINING_PATTERN_SET, buf);
	return ret == 1;
}

static int dp_ctrl_read_link_status(struct dp_ctrl *ctrl,
				    u8 *link_status)
{
	int len = 0;
	u32 const offset = DP_LANE_ALIGN_STATUS_UPDATED - DP_LANE0_1_STATUS;
	u32 link_status_read_max_retries = 100;

	while (--link_status_read_max_retries) {
		len = drm_dp_dpcd_read_link_status(ctrl->aux,
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

static int dp_link_adjust_levels(struct dp_ctrl *ctrl, u8 *link_status)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
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

static int dp_ctrl_link_train_1(struct dp_ctrl *ctrl)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
	int tries, old_v_level, ret = 0;
	u8 link_status[DP_LINK_STATUS_SIZE];
	int const maximum_retries = 5;

	dp_write(ctrl, REG_DP_STATE_CTRL, 0);

	ret = dp_ctrl_set_pattern(ctrl, DP_TRAINING_PATTERN_1);
	if (ret)
		return ret;
	dp_ctrl_train_pattern_set(ctrl, DP_TRAINING_PATTERN_1 |
		DP_LINK_SCRAMBLING_DISABLE);
	ret = dp_ctrl_update_vx_px(ctrl);
	if (ret)
		return ret;

	tries = 0;
	old_v_level = dp->phy_opts.dp.voltage[0];
	for (tries = 0; tries < maximum_retries; tries++) {
		drm_dp_link_train_clock_recovery_delay(dp->dpcd);

		ret = dp_ctrl_read_link_status(ctrl, link_status);
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

		dp_link_adjust_levels(ctrl, link_status);
		ret = dp_ctrl_update_vx_px(ctrl);
		if (ret)
			return ret;
	}

	DRM_ERROR("max tries reached\n");
	return -ETIMEDOUT;
}

static int dp_ctrl_link_rate_down_shift(struct dp_ctrl *ctrl)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
	int ret = 0;

	switch (dp->phy_opts.dp.link_rate) {
	case 8100:
		dp->phy_opts.dp.link_rate = 5400;
		break;
	case 540000:
		dp->phy_opts.dp.link_rate = 2700;
		break;
	case 2700:
	case 1620:
	default:
		dp->phy_opts.dp.link_rate = 1620;
		break;
	};

	DRM_DEBUG_DP("new rate=0x%x\n", dp->phy_opts.dp.link_rate);

	return ret;
}

static void dp_ctrl_clear_training_pattern(struct dp_ctrl *ctrl)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
	dp_ctrl_train_pattern_set(ctrl, DP_TRAINING_PATTERN_DISABLE);
	drm_dp_link_train_channel_eq_delay(dp->dpcd);
}

static int dp_ctrl_link_training_2(struct dp_ctrl *ctrl)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
	int tries = 0, ret = 0;
	char pattern;
	int const maximum_retries = 5;
	u8 link_status[DP_LINK_STATUS_SIZE];

	dp_write(ctrl, REG_DP_STATE_CTRL, 0);

	if (drm_dp_tps3_supported(dp->dpcd))
		pattern = DP_TRAINING_PATTERN_3;
	else
		pattern = DP_TRAINING_PATTERN_2;

	ret = dp_ctrl_update_vx_px(ctrl);
	if (ret)
		return ret;

	ret = dp_ctrl_set_pattern(ctrl, pattern);
	if (ret)
		return ret;

	dp_ctrl_train_pattern_set(ctrl, pattern | DP_RECOVERED_CLOCK_OUT_EN);

	for (tries = 0; tries <= maximum_retries; tries++) {
		drm_dp_link_train_channel_eq_delay(dp->dpcd);

		ret = dp_ctrl_read_link_status(ctrl, link_status);
		if (ret)
			return ret;

		if (drm_dp_channel_eq_ok(link_status, dp->phy_opts.dp.lanes))
			return ret;

		dp_link_adjust_levels(ctrl, link_status);
		ret = dp_ctrl_update_vx_px(ctrl);
		if (ret)
			return ret;

	}

	return -ETIMEDOUT;
}

static int dp_ctrl_link_train(struct dp_ctrl *ctrl)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
	int ret = 0;
	u8 encoding = DP_SET_ANSI_8B10B;

	dp->phy_opts.dp.pre[0] = 0;
	dp->phy_opts.dp.voltage[0] = 0;

	dp_ctrl_config_ctrl(ctrl);

	{
		u8 values[2];
		int err;

		values[0] = drm_dp_link_rate_to_bw_code(dp->phy_opts.dp.link_rate * 100);
		values[1] = dp->phy_opts.dp.lanes;

		if (true)
			values[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

		err = drm_dp_dpcd_write(ctrl->aux, DP_LINK_BW_SET, values, sizeof(values));
	}

	drm_dp_dpcd_write(ctrl->aux, DP_MAIN_LINK_CHANNEL_CODING_SET,
				&encoding, 1);

	ret = dp_ctrl_link_train_1(ctrl);
	if (ret) {
		DRM_ERROR("link training #1 failed. ret=%d\n", ret);
		goto end;
	}

	/* print success info as this is a result of user initiated action */
	DRM_DEBUG_DP("link training #1 successful\n");

	ret = dp_ctrl_link_training_2(ctrl);
	if (ret) {
		DRM_ERROR("link training #2 failed. ret=%d\n", ret);
		goto end;
	}

	/* print success info as this is a result of user initiated action */
	DRM_DEBUG_DP("link training #2 successful\n");

end:
	dp_write(ctrl, REG_DP_STATE_CTRL, 0);

	dp_ctrl_clear_training_pattern(ctrl);
	return ret;
}

static int dp_aux_link_power_up(struct drm_dp_aux *aux)
{
	u8 value;
	int err;

	err = drm_dp_dpcd_readb(aux, DP_SET_POWER, &value);
	if (err < 0)
		return err;

	value &= ~DP_SET_POWER_MASK;
	value |= DP_SET_POWER_D0;

	err = drm_dp_dpcd_writeb(aux, DP_SET_POWER, value);
	if (err < 0)
		return err;

	usleep_range(1000, 2000);

	return 0;
}

static int dp_aux_link_power_down(struct drm_dp_aux *aux)
{
	u8 value;
	int err;

	err = drm_dp_dpcd_readb(aux, DP_SET_POWER, &value);
	if (err < 0)
		return err;

	value &= ~DP_SET_POWER_MASK;
	value |= DP_SET_POWER_D3;

	err = drm_dp_dpcd_writeb(aux, DP_SET_POWER, value);
	if (err < 0)
		return err;

	return 0;
}

static int dp_ctrl_setup_main_link(struct dp_ctrl *ctrl, bool train)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
	bool mainlink_ready = false;
	int ret = 0;

	dp_ctrl_mainlink_ctrl(ctrl, true);

	/* exit power saving mode */
	if (dp->dpcd[DP_DPCD_REV] >= 0x11) {
		ret = dp_aux_link_power_up(&dp->aux);
		if (ret)
			return ret;
	}

	if (train) {
		/*
		 * As part of previous calls, DP controller state might have
		 * transitioned to PUSH_IDLE. In order to start transmitting
		 * a link training pattern, we have to first do soft reset.
		 */
		dp_ctrl_reset(ctrl);

		ret = dp_ctrl_link_train(ctrl);
		if (ret)
			return ret;
	}

	/*
	 * Set up transfer unit values and set controller state to send
	 * video.
	 */
	dp_ctrl_setup_tr_unit(ctrl);
	dp_write(ctrl, REG_DP_STATE_CTRL, DP_STATE_CTRL_SEND_VIDEO);

	ret = dp_ctrl_wait4video_ready(ctrl);
	if (ret)
		return ret;

	mainlink_ready = dp_ctrl_mainlink_ready(ctrl);
	DRM_DEBUG_DP("mainlink %s\n", mainlink_ready ? "READY" : "NOT READY");
	return ret;
}

int dp_ctrl_host_init(struct dp_ctrl *ctrl)
{
	dp_ctrl_enable_irq(ctrl, true);
	return 0;
}

/**
 * dp_ctrl_host_deinit() - Uninitialize DP controller
 * @dp_ctrl: Display Port Driver data
 *
 * Perform required steps to uninitialize DP controller
 * and its resources.
 */
void dp_ctrl_host_deinit(struct dp_ctrl *ctrl)
{
	dp_ctrl_enable_irq(ctrl, false);

	DRM_DEBUG_DP("Host deinitialized successfully\n");
}

static bool dp_ctrl_use_fixed_nvid(struct dp_ctrl *ctrl)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
	u32 edid_quirks = 0;

	edid_quirks = drm_dp_get_edid_quirks(dp->edid);
	/*
	 * For better interop experience, used a fixed NVID=0x8000
	 * whenever connected to a VGA dongle downstream.
	 */
	if (drm_dp_is_branch(dp->dpcd))
		return (drm_dp_has_quirk(&dp->desc, edid_quirks,
				DP_DPCD_QUIRK_CONSTANT_N));

	return false;
}

static int dp_ctrl_reinitialize_mainlink(struct dp_ctrl *ctrl)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
	int ret = 0;

	dp_ctrl_mainlink_ctrl(ctrl, false);

	clk_bulk_disable_unprepare(ARRAY_SIZE(dp->clk_ctrl), dp->clk_ctrl);
	/* hw recommended delay before re-enabling clocks */
	phy_power_off(dp->phy);
	msleep(20);

	phy_configure(dp->phy, &dp->phy_opts);
	phy_power_on(dp->phy);

	clk_set_rate(dp->clk_ctrl[2].clk, dp->mode.clock * 1000);
	ret = clk_bulk_prepare_enable(ARRAY_SIZE(dp->clk_ctrl), dp->clk_ctrl);
	if (ret) {
		DRM_ERROR("Failed to enable mainlink clks. ret=%d\n", ret);
		return ret;
	}

	dp_ctrl_configure_source_params(ctrl);
	dp_ctrl_config_msa(ctrl, dp->phy_opts.dp.link_rate * 100, dp->mode.clock, dp_ctrl_use_fixed_nvid(ctrl));
	reinit_completion(&ctrl->idle_comp);

	return ret;
}

int dp_ctrl_link_maintenance(struct dp_ctrl *ctrl)
{
	int ret = 0;
	int tries, max_tries = 10;

	dp_ctrl_reset(ctrl);

	for (tries = 0; tries < max_tries; tries++) {
		ret = dp_ctrl_reinitialize_mainlink(ctrl);
		if (ret) {
			DRM_ERROR("Failed to reinitialize mainlink. ret=%d\n",
					ret);
			break;
		}

		ret = dp_ctrl_setup_main_link(ctrl, true);
		if (ret == -EAGAIN) /* try with lower link rate */
			dp_ctrl_link_rate_down_shift(ctrl);
	}
	return ret;
}

int dp_ctrl_on(struct dp_ctrl *ctrl)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
	int rc = 0;
	u32 link_train_max_retries = 10;

	phy_configure(dp->phy, &dp->phy_opts);
	phy_power_on(dp->phy);

	clk_set_rate(dp->clk_ctrl[2].clk, dp->mode.clock * 1000);
	rc = clk_bulk_prepare_enable(ARRAY_SIZE(dp->clk_ctrl), dp->clk_ctrl);
	if (rc) {
		DRM_ERROR("CTRL clock enable failed\n");
		return rc;
	}

	dp_write(ctrl, REG_DP_HPD_CTRL, 0x0);

	while (--link_train_max_retries) {
		rc = dp_ctrl_reinitialize_mainlink(ctrl);
		if (rc) {
			DRM_ERROR("Failed to reinitialize mainlink. rc=%d\n",
					rc);
			break;
		}
		rc = dp_ctrl_setup_main_link(ctrl, true);
		if (!rc)
			break;
		/* try with lower link rate */
		dp_ctrl_link_rate_down_shift(ctrl);
	}

	dp->ctrl_on = true;

	return rc;
}

int dp_ctrl_off(struct dp_ctrl *ctrl)
{
	struct msm_dp *dp = container_of(ctrl, struct msm_dp, ctrl);
	int ret = 0;

	dp_ctrl_mainlink_ctrl(ctrl, false);
	clk_bulk_disable_unprepare(ARRAY_SIZE(dp->clk_ctrl), dp->clk_ctrl);
	dp_ctrl_reset(ctrl);

	dp->ctrl_on = false;

	DRM_DEBUG_DP("DP off done\n");
	return ret;
}

void dp_ctrl_isr(struct dp_ctrl *ctrl)
{
	u32 isr;

	isr = dp_ctrl_get_interrupt(ctrl);

	if (isr & DP_CTRL_INTR_READY_FOR_VIDEO) {
		DRM_DEBUG_DP("dp_video_ready\n");
		complete(&ctrl->video_comp);
	}

	if (isr & DP_CTRL_INTR_IDLE_PATTERN_SENT) {
		DRM_DEBUG_DP("idle_patterns_sent\n");
		complete(&ctrl->idle_comp);
	}
}
