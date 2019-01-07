// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm-dp] %s: " fmt, __func__

#include <linux/delay.h>
#include <linux/iopoll.h>
#include <drm/drm_dp_helper.h>

#include "dp_catalog.h"
#include "dp_reg.h"

#define POLLING_SLEEP_US			1000
#define POLLING_TIMEOUT_US			10000

#define REFTIMER_DEFAULT_VALUE			0x20000
#define SCRAMBLER_RESET_COUNT_VALUE		0xFC

#define dp_catalog_get_priv(x) { \
	struct dp_catalog *dp_catalog; \
	dp_catalog = container_of(x, struct dp_catalog, x); \
	catalog = container_of(dp_catalog, struct dp_catalog_private, \
				dp_catalog); \
}

#define DP_INTERRUPT_STATUS1 \
	(DP_INTR_AUX_I2C_DONE| \
	DP_INTR_WRONG_ADDR | DP_INTR_TIMEOUT | \
	DP_INTR_NACK_DEFER | DP_INTR_WRONG_DATA_CNT | \
	DP_INTR_I2C_NACK | DP_INTR_I2C_DEFER | \
	DP_INTR_PLL_UNLOCKED | DP_INTR_AUX_ERROR)

#define DP_INTERRUPT_STATUS1_MASK	(DP_INTERRUPT_STATUS1 << 2)

#define DP_INTERRUPT_STATUS2 \
	(DP_INTR_READY_FOR_VIDEO | DP_INTR_IDLE_PATTERN_SENT | \
	DP_INTR_FRAME_END | DP_INTR_CRC_UPDATED)

#define DP_INTERRUPT_STATUS2_MASK	(DP_INTERRUPT_STATUS2 << 2)

static u8 const vm_pre_emphasis[4][4] = {
	{0x00, 0x0B, 0x12, 0xFF},       /* pe0, 0 db */
	{0x00, 0x0A, 0x12, 0xFF},       /* pe1, 3.5 db */
	{0x00, 0x0C, 0xFF, 0xFF},       /* pe2, 6.0 db */
	{0xFF, 0xFF, 0xFF, 0xFF}        /* pe3, 9.5 db */
};

/* voltage swing, 0.2v and 1.0v are not support */
static u8 const vm_voltage_swing[4][4] = {
	{0x07, 0x0F, 0x14, 0xFF}, /* sw0, 0.4v  */
	{0x11, 0x1D, 0x1F, 0xFF}, /* sw1, 0.6 v */
	{0x18, 0x1F, 0xFF, 0xFF}, /* sw1, 0.8 v */
	{0xFF, 0xFF, 0xFF, 0xFF}  /* sw1, 1.2 v, optional */
};

struct dp_catalog_private {
	struct device *dev;
	struct dp_io *io;
	struct dp_catalog dp_catalog;
};

static inline u32 dp_read_aux(struct dp_catalog_private *catalog, u32 offset)
{
	return readl_relaxed(catalog->io->dp_aux.base + offset);
}

static inline void dp_write_aux(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	/*
	 * To make sure aux reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, catalog->io->dp_aux.base + offset);
}

static inline u32 dp_read_ahb(struct dp_catalog_private *catalog, u32 offset)
{
	return readl_relaxed(catalog->io->dp_ahb.base + offset);
}

static inline void dp_write_ahb(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	/*
	 * To make sure phy reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, catalog->io->dp_ahb.base + offset);
}

static inline u32 dp_read_cc(struct dp_catalog_private *catalog, u32 offset)
{
	return readl_relaxed(catalog->io->dp_cc_io.base + offset);
}

static inline void dp_write_phy(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	/*
	 * To make sure phy reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, catalog->io->phy_io.base + offset);
}

static inline void dp_write_pll(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	writel_relaxed(data, catalog->io->dp_pll_io.base + offset);
}

static inline void dp_write_ln_tx0(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	writel_relaxed(data, catalog->io->ln_tx0_io.base + offset);
}

static inline void dp_write_ln_tx1(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	writel_relaxed(data, catalog->io->ln_tx1_io.base + offset);
}

static inline void dp_write_usb_cm(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	/*
	 * To make sure usb reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, catalog->io->usb3_dp_com.base + offset);
}

static inline void dp_write_p0(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	/*
	 * To make sure interface reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, catalog->io->dp_p0.base + offset);
}

static inline u32 dp_read_link(struct dp_catalog_private *catalog, u32 offset)
{
	return readl_relaxed(catalog->io->dp_link.base + offset);
}

static inline void dp_write_link(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	/*
	 * To make sure link reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, catalog->io->dp_link.base + offset);
}

/* aux related catalog functions */
u32 dp_catalog_aux_read_data(struct dp_catalog_aux *aux)
{
	struct dp_catalog_private *catalog;

	if (!aux) {
		DRM_ERROR("invalid input\n");
		return 0;
	}

	dp_catalog_get_priv(aux);

	return dp_read_aux(catalog, REG_DP_AUX_DATA);
}

int dp_catalog_aux_write_data(struct dp_catalog_aux *aux)
{
	int rc = 0;
	struct dp_catalog_private *catalog;

	if (!aux) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	dp_catalog_get_priv(aux);
	dp_write_aux(catalog, REG_DP_AUX_DATA, aux->data);
	return rc;
}

int dp_catalog_aux_write_trans(struct dp_catalog_aux *aux)
{
	int rc = 0;
	struct dp_catalog_private *catalog;

	if (!aux) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	dp_catalog_get_priv(aux);
	dp_write_aux(catalog, REG_DP_AUX_TRANS_CTRL, aux->data);
	return rc;
}

int dp_catalog_aux_clear_trans(struct dp_catalog_aux *aux, bool read)
{
	int rc = 0;
	u32 data = 0;
	struct dp_catalog_private *catalog;

	if (!aux) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	dp_catalog_get_priv(aux);

	if (read) {
		data = dp_read_aux(catalog, REG_DP_AUX_TRANS_CTRL);
		data &= ~DP_AUX_TRANS_CTRL_GO;
		dp_write_aux(catalog, REG_DP_AUX_TRANS_CTRL, data);
	} else {
		dp_write_aux(catalog, REG_DP_AUX_TRANS_CTRL, 0);
	}
	return rc;
}

void dp_catalog_aux_reset(struct dp_catalog_aux *aux)
{
	u32 aux_ctrl;
	struct dp_catalog_private *catalog;

	if (!aux) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(aux);

	aux_ctrl = dp_read_aux(catalog, REG_DP_AUX_CTRL);

	aux_ctrl |= DP_AUX_CTRL_RESET;
	dp_write_aux(catalog, REG_DP_AUX_CTRL, aux_ctrl);
	usleep_range(1000, 1100); /* h/w recommended delay */

	aux_ctrl &= ~DP_AUX_CTRL_RESET;
	dp_write_aux(catalog, REG_DP_AUX_CTRL, aux_ctrl);
}

void dp_catalog_aux_enable(struct dp_catalog_aux *aux, bool enable)
{
	u32 aux_ctrl;
	struct dp_catalog_private *catalog;

	if (!aux) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(aux);

	aux_ctrl = dp_read_aux(catalog, REG_DP_AUX_CTRL);

	if (enable) {
		dp_write_aux(catalog, REG_DP_TIMEOUT_COUNT, 0xffff);
		dp_write_aux(catalog, REG_DP_AUX_LIMITS, 0xffff);
		aux_ctrl |= DP_AUX_CTRL_ENABLE;
	} else {
		aux_ctrl &= ~DP_AUX_CTRL_ENABLE;
	}

	dp_write_aux(catalog, REG_DP_AUX_CTRL, aux_ctrl);
}

void dp_catalog_aux_update_cfg(struct dp_catalog_aux *aux,
		struct dp_aux_cfg *cfg, enum dp_phy_aux_config_type type)
{
	struct dp_catalog_private *catalog;
	u32 new_index = 0, current_index = 0;

	if (!aux || !cfg || (type >= PHY_AUX_CFG_MAX)) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(aux);

	current_index = cfg[type].current_index;
	new_index = (current_index + 1) % cfg[type].cfg_cnt;
	DRM_DEBUG_DP("Updating %s from 0x%08x to 0x%08x\n",
		dp_phy_aux_config_type_to_string(type),
	cfg[type].lut[current_index], cfg[type].lut[new_index]);

	dp_write_phy(catalog, cfg[type].offset,
			cfg[type].lut[new_index]);
	cfg[type].current_index = new_index;
}

void dp_catalog_aux_setup(struct dp_catalog_aux *aux,
		struct dp_aux_cfg *cfg)
{
	struct dp_catalog_private *catalog;
	int i = 0;

	if (!aux || !cfg) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(aux);

	dp_write_phy(catalog, REG_DP_PHY_PD_CTL, DP_PHY_PD_CTL_PWRDN |
		DP_PHY_PD_CTL_AUX_PWRDN | DP_PHY_PD_CTL_PLL_PWRDN |
		DP_PHY_PD_CTL_DP_CLAMP_EN);

	/* Turn on BIAS current for PHY/PLL */
	dp_write_pll(catalog,
		QSERDES_COM_BIAS_EN_CLKBUFLR_EN, QSERDES_COM_BIAS_EN |
		QSERDES_COM_BIAS_EN_MUX | QSERDES_COM_CLKBUF_L_EN |
		QSERDES_COM_EN_SYSCLK_TX_SEL);

	/* DP AUX CFG register programming */
	for (i = 0; i < PHY_AUX_CFG_MAX; i++) {
		DRM_DEBUG_DP("%s: offset=0x%08x, value=0x%08x\n",
			dp_phy_aux_config_type_to_string(i),
			cfg[i].offset, cfg[i].lut[cfg[i].current_index]);
		dp_write_phy(catalog, cfg[i].offset,
			cfg[i].lut[cfg[i].current_index]);
	}

	dp_write_phy(catalog, REG_DP_PHY_AUX_INTERRUPT_MASK, PHY_AUX_STOP_ERR_MASK |
			PHY_AUX_DEC_ERR_MASK | PHY_AUX_SYNC_ERR_MASK |
			PHY_AUX_ALIGN_ERR_MASK | PHY_AUX_REQ_ERR_MASK);
}

void dp_catalog_aux_get_irq(struct dp_catalog_aux *aux, bool cmd_busy)
{
	u32 ack;
	struct dp_catalog_private *catalog;

	if (!aux) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(aux);

	aux->isr = dp_read_ahb(catalog, REG_DP_INTR_STATUS);
	aux->isr &= ~DP_INTERRUPT_STATUS1_MASK;
	ack = aux->isr & DP_INTERRUPT_STATUS1;
	ack <<= 1;
	ack |= DP_INTERRUPT_STATUS1_MASK;
	dp_write_ahb(catalog, REG_DP_INTR_STATUS, ack);
}

/* controller related catalog functions */
void dp_catalog_ctrl_update_transfer_unit(struct dp_catalog_ctrl *ctrl)
{
	struct dp_catalog_private *catalog;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(ctrl);

	dp_write_link(catalog, REG_DP_VALID_BOUNDARY, ctrl->valid_boundary);
	dp_write_link(catalog, REG_DP_TU, ctrl->dp_tu);
	dp_write_link(catalog, REG_DP_VALID_BOUNDARY_2, ctrl->valid_boundary2);
}

void dp_catalog_ctrl_state_ctrl(struct dp_catalog_ctrl *ctrl, u32 state)
{
	struct dp_catalog_private *catalog;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(ctrl);

	dp_write_link(catalog, REG_DP_STATE_CTRL, state);
}

void dp_catalog_ctrl_config_ctrl(struct dp_catalog_ctrl *ctrl, u32 cfg)
{
	struct dp_catalog_private *catalog;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(ctrl);

	DRM_DEBUG_DP("DP_CONFIGURATION_CTRL=0x%x\n", cfg);

	dp_write_link(catalog, REG_DP_CONFIGURATION_CTRL, cfg);
}

void dp_catalog_ctrl_lane_mapping(struct dp_catalog_ctrl *ctrl)
{
	struct dp_catalog_private *catalog;
	u32 ln_0 = 0, ln_1 = 1, ln_2 = 2, ln_3 = 3; /* One-to-One mapping */
	u32 ln_mapping;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(ctrl);

	ln_mapping = ln_0 << LANE0_MAPPING_SHIFT;
	ln_mapping |= ln_1 << LANE1_MAPPING_SHIFT;
	ln_mapping |= ln_2 << LANE2_MAPPING_SHIFT;
	ln_mapping |= ln_3 << LANE3_MAPPING_SHIFT;

	dp_write_link(catalog, REG_DP_LOGICAL2PHYSICAL_LANE_MAPPING, ln_mapping);
}

void dp_catalog_ctrl_mainlink_ctrl(struct dp_catalog_ctrl *ctrl,
						bool enable)
{
	u32 mainlink_ctrl;
	struct dp_catalog_private *catalog;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(ctrl);

	if (enable) {
		/*
		* To make sure link reg writes happens before any other operation,
		* dp_write_link() function uses writel() instread of writel_relaxed()
		*/
		dp_write_link(catalog, REG_DP_MAINLINK_CTRL, DP_MAINLINK_FB_BOUNDARY_SEL);
		dp_write_link(catalog, REG_DP_MAINLINK_CTRL,
					DP_MAINLINK_FB_BOUNDARY_SEL | DP_MAINLINK_CTRL_RESET);
		dp_write_link(catalog, REG_DP_MAINLINK_CTRL, DP_MAINLINK_FB_BOUNDARY_SEL);
		dp_write_link(catalog, REG_DP_MAINLINK_CTRL,
					DP_MAINLINK_FB_BOUNDARY_SEL | DP_MAINLINK_CTRL_ENABLE);
	} else {
		mainlink_ctrl = dp_read_link(catalog, REG_DP_MAINLINK_CTRL);
		mainlink_ctrl &= ~DP_MAINLINK_CTRL_ENABLE;
		dp_write_link(catalog, REG_DP_MAINLINK_CTRL, mainlink_ctrl);
	}
}

void dp_catalog_ctrl_config_misc(struct dp_catalog_ctrl *ctrl,
					u32 colorimetry_cfg, u32 test_bits_depth)
{
	u32 misc_val;
	struct dp_catalog_private *catalog;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(ctrl);

	misc_val = dp_read_link(catalog, REG_DP_MISC1_MISC0);
	misc_val |= colorimetry_cfg << DP_MISC0_COLORIMETRY_CFG_SHIFT;
	misc_val |= test_bits_depth << DP_MISC0_TEST_BITS_DEPTH_SHIFT;
	/* Configure clock to synchronous mode */
	misc_val |= DP_MISC0_SYNCHRONOUS_CLK;

	DRM_DEBUG_DP("misc settings = 0x%x\n", misc_val);
	dp_write_link(catalog, REG_DP_MISC1_MISC0, misc_val);
}

void dp_catalog_ctrl_config_msa(struct dp_catalog_ctrl *ctrl,
					u32 rate, u32 stream_rate_khz,
					bool fixed_nvid)
{
	u32 pixel_m, pixel_n;
	u32 mvid, nvid;
	u64 mvid_calc;
	u32 const nvid_fixed = 0x8000;
	u32 const link_rate_hbr2 = 540000;
	u32 const link_rate_hbr3 = 810000;
	struct dp_catalog_private *catalog;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(ctrl);
	if (fixed_nvid) {
		DRM_DEBUG_DP("use fixed NVID=0x%x\n", nvid_fixed);
		nvid = nvid_fixed;

		DRM_DEBUG_DP("link rate=%dkbps, stream_rate_khz=%uKhz",
			rate, stream_rate_khz);

		/*
		 * For intermediate results, use 64 bit arithmetic to avoid
		 * loss of precision.
		 */
		mvid_calc = (u64) stream_rate_khz * nvid;
		mvid_calc = div_u64(mvid_calc, rate);

		/*
		 * truncate back to 32 bits as this final divided value will
		 * always be within the range of a 32 bit unsigned int.
		 */
		mvid = (u32) mvid_calc;
	} else {
		pixel_m = dp_read_cc(catalog, MMSS_DP_PIXEL_M);
		pixel_n = dp_read_cc(catalog, MMSS_DP_PIXEL_N);
		DRM_DEBUG_DP("pixel_m=0x%x, pixel_n=0x%x\n", pixel_m, pixel_n);

		mvid = (pixel_m & 0xFFFF) * 5;
		nvid = (0xFFFF & (~pixel_n)) + (pixel_m & 0xFFFF);

		DRM_DEBUG_DP("rate = %d\n", rate);

		if (link_rate_hbr2 == rate)
			nvid *= 2;

		if (link_rate_hbr3 == rate)
			nvid *= 3;
	}

	DRM_DEBUG_DP("mvid=0x%x, nvid=0x%x\n", mvid, nvid);
	dp_write_link(catalog, REG_DP_SOFTWARE_MVID, mvid);
	dp_write_link(catalog, REG_DP_SOFTWARE_NVID, nvid);
}

int dp_catalog_ctrl_set_pattern(struct dp_catalog_ctrl *ctrl,
					u32 pattern)
{
	int bit, ret;
	u32 data;
	struct dp_catalog_private *catalog;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	dp_catalog_get_priv(ctrl);

	bit = BIT(pattern - 1);
	DRM_DEBUG_DP("hw: bit=%d train=%d\n", bit, pattern);
	dp_write_link(catalog, REG_DP_STATE_CTRL, bit);

	bit = BIT(pattern - 1) << DP_MAINLINK_READY_LINK_TRAINING_SHIFT;

	/* Poll for mainlink ready status */
	ret = readx_poll_timeout(readl, catalog->io->dp_link.base + REG_DP_MAINLINK_READY,
				 data, data & bit,
				 POLLING_SLEEP_US, POLLING_TIMEOUT_US);
	if (ret < 0) {
		DRM_ERROR("set pattern for link_train=%d failed\n", pattern);
		return ret;
	}
	return 0;
}

void dp_catalog_ctrl_usb_reset(struct dp_catalog_ctrl *ctrl, bool flip)
{
	struct dp_catalog_private *catalog;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(ctrl);

	dp_write_usb_cm(catalog, REG_USB3_DP_COM_RESET_OVRD_CTRL,
			USB3_DP_COM_OVRD_CTRL_SW_DPPHY_RESET_MUX |
			USB3_DP_COM_OVRD_CTRL_SW_USB3PHY_RESET_MUX);
	dp_write_usb_cm(catalog, REG_USB3_DP_COM_PHY_MODE_CTRL,
						USB3_DP_COM_PHY_MODE_DP);
	dp_write_usb_cm(catalog, REG_USB3_DP_COM_SW_RESET,
						USB3_DP_COM_SW_RESET_SET);

	if (!flip) /* CC1 */
		dp_write_usb_cm(catalog, REG_USB3_DP_COM_TYPEC_CTRL,
					USB3_DP_COM_TYPEC_CTRL_PORTSEL_MUX);
	else /* CC2 */
		dp_write_usb_cm(catalog, REG_USB3_DP_COM_TYPEC_CTRL,
					USB3_DP_COM_TYPEC_CTRL_PORTSEL_MUX |
					USB3_DP_COM_TYPEC_CTRL_PORTSEL);

	dp_write_usb_cm(catalog, REG_USB3_DP_COM_SWI_CTRL, 0x00);
	dp_write_usb_cm(catalog, REG_USB3_DP_COM_SW_RESET, 0x00);

	dp_write_usb_cm(catalog, REG_USB3_DP_COM_POWER_DOWN_CTRL,
						USB3_DP_COM_POWER_DOWN_CTRL_SW_PWRDN);
	dp_write_usb_cm(catalog, REG_USB3_DP_COM_RESET_OVRD_CTRL, 0x00);

}

void dp_catalog_ctrl_reset(struct dp_catalog_ctrl *ctrl)
{
	u32 sw_reset;
	struct dp_catalog_private *catalog;
	void __iomem *base;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(ctrl);
	base = catalog->io->dp_ahb.base;

	sw_reset = dp_read_ahb(catalog, REG_DP_SW_RESET);

	sw_reset |= DP_SW_RESET;
	dp_write_ahb(catalog, REG_DP_SW_RESET, sw_reset);
	usleep_range(1000, 1100); /* h/w recommended delay */

	sw_reset &= ~DP_SW_RESET;
	dp_write_ahb(catalog, REG_DP_SW_RESET, sw_reset);
}

bool dp_catalog_ctrl_mainlink_ready(struct dp_catalog_ctrl *ctrl)
{
	u32 data;
	int ret;
	struct dp_catalog_private *catalog;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return false;
	}

	dp_catalog_get_priv(ctrl);

	/* Poll for mainlink ready status */
	ret = readx_poll_timeout(readl,
				 catalog->io->dp_link.base + REG_DP_MAINLINK_READY,
				 data, data & DP_MAINLINK_READY_FOR_VIDEO,
				 POLLING_SLEEP_US, POLLING_TIMEOUT_US);
	if (ret < 0) {
		DRM_ERROR("mainlink not ready\n");
		return false;
	}

	return true;
}

void dp_catalog_ctrl_enable_irq(struct dp_catalog_ctrl *ctrl,
						bool enable)
{
	struct dp_catalog_private *catalog;
	void __iomem *base;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(ctrl);
	base = catalog->io->dp_ahb.base;

	if (enable) {
		dp_write_ahb(catalog, REG_DP_INTR_STATUS, DP_INTERRUPT_STATUS1_MASK);
		dp_write_ahb(catalog, REG_DP_INTR_STATUS2, DP_INTERRUPT_STATUS2_MASK);
	} else {
		dp_write_ahb(catalog, REG_DP_INTR_STATUS, 0x00);
		dp_write_ahb(catalog, REG_DP_INTR_STATUS2, 0x00);
	}
}

void dp_catalog_ctrl_hpd_config(struct dp_catalog_ctrl *ctrl, bool en)
{
	struct dp_catalog_private *catalog;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(ctrl);

	if (en) {
		u32 reftimer = dp_read_aux(catalog, REG_DP_DP_HPD_REFTIMER);

		dp_write_aux(catalog, REG_DP_DP_HPD_INT_ACK,
				DP_DP_HPD_PLUG_INT_ACK | DP_DP_IRQ_HPD_INT_ACK |
				DP_DP_HPD_REPLUG_INT_ACK | DP_DP_HPD_UNPLUG_INT_ACK);
		dp_write_aux(catalog, REG_DP_DP_HPD_INT_MASK,
				DP_DP_HPD_PLUG_INT_MASK | DP_DP_IRQ_HPD_INT_MASK |
				DP_DP_HPD_REPLUG_INT_MASK | DP_DP_HPD_UNPLUG_INT_MASK);

		/* Configure REFTIMER */
		reftimer |= REFTIMER_DEFAULT_VALUE;
		dp_write_aux(catalog, REG_DP_DP_HPD_REFTIMER, reftimer);
		/* Enable HPD */
		dp_write_aux(catalog, REG_DP_DP_HPD_CTRL, DP_DP_HPD_CTRL_HPD_EN);
	} else {
		/* Disable HPD */
		dp_write_aux(catalog, REG_DP_DP_HPD_CTRL, 0x0);
	}
}

void dp_catalog_ctrl_get_interrupt(struct dp_catalog_ctrl *ctrl)
{
	u32 ack = 0;
	struct dp_catalog_private *catalog;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(ctrl);

	ctrl->isr = dp_read_ahb(catalog, REG_DP_INTR_STATUS2);
	ctrl->isr &= ~DP_INTERRUPT_STATUS2_MASK;
	ack = ctrl->isr & DP_INTERRUPT_STATUS2;
	ack <<= 1;
	ack |= DP_INTERRUPT_STATUS2_MASK;
	dp_write_ahb(catalog, REG_DP_INTR_STATUS2, ack);
}

void dp_catalog_ctrl_phy_reset(struct dp_catalog_ctrl *ctrl)
{
	struct dp_catalog_private *catalog;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(ctrl);

	dp_write_ahb(catalog, REG_DP_PHY_CTRL,
			DP_PHY_CTRL_SW_RESET_PLL | DP_PHY_CTRL_SW_RESET);
	usleep_range(1000, 1100); /* h/w recommended delay */
	dp_write_ahb(catalog, REG_DP_PHY_CTRL, 0x0);
}

void dp_catalog_ctrl_phy_lane_cfg(struct dp_catalog_ctrl *ctrl,
		bool flipped, u8 ln_cnt)
{
	u32 info = 0x0;
	struct dp_catalog_private *catalog;
	u8 orientation = BIT(!!flipped);

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(ctrl);

	info = ln_cnt & DP_PHY_SPARE0_MASK;
	info |= (orientation & DP_PHY_SPARE0_MASK) << DP_PHY_SPARE0_ORIENTATION_INFO_SHIFT;
	DRM_DEBUG_DP("Shared Info = 0x%x\n", info);

	dp_write_phy(catalog, REG_DP_PHY_SPARE0, info);
}

int dp_catalog_ctrl_update_vx_px(struct dp_catalog_ctrl *ctrl,
		u8 v_level, u8 p_level)
{
	struct dp_catalog_private *catalog;
	u8 value0, value1;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	dp_catalog_get_priv(ctrl);

	DRM_DEBUG_DP("hw: v=%d p=%d\n", v_level, p_level);

	value0 = vm_voltage_swing[v_level][p_level];
	value1 = vm_pre_emphasis[v_level][p_level];

	if (value0 == 0xFF && value1 == 0xFF) {
		DRM_ERROR("invalid vx (0x%x=0x%x), px (0x%x=0x%x\n",
			v_level, value0, p_level, value1);
		return -EINVAL;
	}
	/* program default setting first */
	dp_write_ln_tx0(catalog, TXn_TX_DRV_LVL, 0x2A);
	dp_write_ln_tx1(catalog, TXn_TX_DRV_LVL, 0x2A);
	dp_write_ln_tx0(catalog, TXn_TX_EMP_POST1_LVL, 0x20);
	dp_write_ln_tx1(catalog, TXn_TX_EMP_POST1_LVL, 0x20);

	/* Enable MUX to use Cursor values from these registers */
	value0 |= BIT(5);
	value1 |= BIT(5);

	/* Configure host and panel only if both values are allowed */
	dp_write_ln_tx0(catalog, TXn_TX_DRV_LVL, value0);
	dp_write_ln_tx1(catalog, TXn_TX_DRV_LVL, value0);
	dp_write_ln_tx0(catalog, TXn_TX_EMP_POST1_LVL, value1);
	dp_write_ln_tx1(catalog, TXn_TX_EMP_POST1_LVL, value1);
	DRM_DEBUG_DP("hw: vx_value=0x%x px_value=0x%x\n",
			value0, value1);

	return 0;
}

void dp_catalog_ctrl_send_phy_pattern(struct dp_catalog_ctrl *ctrl,
			u32 pattern)
{
	struct dp_catalog_private *catalog;
	u32 value = 0x0;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(ctrl);

	/* Make sure to clear the current pattern before starting a new one */
	dp_write_link(catalog, REG_DP_STATE_CTRL, 0x0);

	switch (pattern) {
	case DP_LINK_QUAL_PATTERN_D10_2:
		dp_write_link(catalog, REG_DP_STATE_CTRL, DP_STATE_CTRL_LINK_TRAINING_PATTERN1);
		break;
	case DP_LINK_QUAL_PATTERN_ERROR_RATE:
		value &= ~DP_HBR2_ERM_PATTERN;
		dp_write_link(catalog, REG_DP_HBR2_COMPLIANCE_SCRAMBLER_RESET, value);
		value |= SCRAMBLER_RESET_COUNT_VALUE;
		dp_write_link(catalog, REG_DP_HBR2_COMPLIANCE_SCRAMBLER_RESET, value);
		dp_write_link(catalog, REG_DP_MAINLINK_LEVELS, DP_MAINLINK_SAFE_TO_EXIT_LEVEL_2);
		dp_write_link(catalog, REG_DP_STATE_CTRL, DP_STATE_CTRL_LINK_SYMBOL_ERR_MEASURE);
		break;
	case DP_LINK_QUAL_PATTERN_PRBS7:
		dp_write_link(catalog, REG_DP_STATE_CTRL, DP_STATE_CTRL_LINK_PRBS7);
		break;
	case DP_LINK_QUAL_PATTERN_80BIT_CUSTOM:
		dp_write_link(catalog, REG_DP_STATE_CTRL, DP_STATE_CTRL_LINK_TEST_CUSTOM_PATTERN);
		/* 00111110000011111000001111100000 */
		dp_write_link(catalog, REG_DP_TEST_80BIT_CUSTOM_PATTERN_REG0, 0x3E0F83E0);
		/* 00001111100000111110000011111000 */
		dp_write_link(catalog, REG_DP_TEST_80BIT_CUSTOM_PATTERN_REG1, 0x0F83E0F8);
		/* 1111100000111110 */
		dp_write_link(catalog, REG_DP_TEST_80BIT_CUSTOM_PATTERN_REG2, 0x0000F83E);
		break;
	case DP_LINK_QUAL_PATTERN_HBR2_EYE:
		value = DP_HBR2_ERM_PATTERN;
		dp_write_link(catalog, REG_DP_HBR2_COMPLIANCE_SCRAMBLER_RESET, value);
		value |= SCRAMBLER_RESET_COUNT_VALUE;
		dp_write_link(catalog, REG_DP_HBR2_COMPLIANCE_SCRAMBLER_RESET, value);
		dp_write_link(catalog, REG_DP_MAINLINK_LEVELS, DP_MAINLINK_SAFE_TO_EXIT_LEVEL_2);
		dp_write_link(catalog, REG_DP_STATE_CTRL, DP_STATE_CTRL_LINK_SYMBOL_ERR_MEASURE);
		break;
	default:
		DRM_DEBUG_DP("No valid test pattern requested: 0x%x\n", pattern);
		return;
	}
}

u32 dp_catalog_ctrl_read_phy_pattern(struct dp_catalog_ctrl *ctrl)
{
	struct dp_catalog_private *catalog;

	if (!ctrl) {
		DRM_ERROR("invalid input\n");
		return 0;
	}

	dp_catalog_get_priv(ctrl);

	return dp_read_link(catalog, REG_DP_MAINLINK_READY);
}

/* panel related catalog functions */
int dp_catalog_panel_timing_cfg(struct dp_catalog_panel *panel)
{
	struct dp_catalog_private *catalog;

	if (!panel) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	dp_catalog_get_priv(panel);

	dp_write_link(catalog, REG_DP_TOTAL_HOR_VER, panel->total);
	dp_write_link(catalog, REG_DP_START_HOR_VER_FROM_SYNC, panel->sync_start);
	dp_write_link(catalog, REG_DP_HSYNC_VSYNC_WIDTH_POLARITY, panel->width_blanking);
	dp_write_link(catalog, REG_DP_ACTIVE_HOR_VER, panel->dp_active);
	return 0;
}

void dp_catalog_panel_tpg_enable(struct dp_catalog_panel *panel)
{
	struct dp_catalog_private *catalog;

	if (!panel) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(panel);

	dp_write_p0(catalog, MMSS_DP_INTF_CONFIG, 0x0);
	dp_write_p0(catalog, MMSS_DP_INTF_HSYNC_CTL, panel->hsync_ctl);
	dp_write_p0(catalog, MMSS_DP_INTF_VSYNC_PERIOD_F0, panel->vsync_period *
			panel->hsync_period);
	dp_write_p0(catalog, MMSS_DP_INTF_VSYNC_PULSE_WIDTH_F0, panel->v_sync_width *
			panel->hsync_period);
	dp_write_p0(catalog, MMSS_DP_INTF_VSYNC_PERIOD_F1, 0);
	dp_write_p0(catalog, MMSS_DP_INTF_VSYNC_PULSE_WIDTH_F1, 0);
	dp_write_p0(catalog, MMSS_DP_INTF_DISPLAY_HCTL, panel->display_hctl);
	dp_write_p0(catalog, MMSS_DP_INTF_ACTIVE_HCTL, 0);
	dp_write_p0(catalog, MMSS_INTF_DISPLAY_V_START_F0, panel->display_v_start);
	dp_write_p0(catalog, MMSS_DP_INTF_DISPLAY_V_END_F0, panel->display_v_end);
	dp_write_p0(catalog, MMSS_INTF_DISPLAY_V_START_F1, 0);
	dp_write_p0(catalog, MMSS_DP_INTF_DISPLAY_V_END_F1, 0);
	dp_write_p0(catalog, MMSS_DP_INTF_ACTIVE_V_START_F0, 0);
	dp_write_p0(catalog, MMSS_DP_INTF_ACTIVE_V_END_F0, 0);
	dp_write_p0(catalog, MMSS_DP_INTF_ACTIVE_V_START_F1, 0);
	dp_write_p0(catalog, MMSS_DP_INTF_ACTIVE_V_END_F1, 0);
	dp_write_p0(catalog, MMSS_DP_INTF_POLARITY_CTL, 0);

	dp_write_p0(catalog, MMSS_DP_TPG_MAIN_CONTROL, DP_TPG_CHECKERED_RECT_PATTERN);
	dp_write_p0(catalog, MMSS_DP_TPG_VIDEO_CONFIG,
					DP_TPG_VIDEO_CONFIG_BPP_8BIT |
					DP_TPG_VIDEO_CONFIG_RGB);
	dp_write_p0(catalog, MMSS_DP_BIST_ENABLE, DP_BIST_ENABLE_DPBIST_EN);
	dp_write_p0(catalog, MMSS_DP_TIMING_ENGINE_EN, DP_TIMING_ENGINE_EN_EN);
	DRM_DEBUG_DP("%s: enabled tpg\n", __func__);
}

void dp_catalog_panel_tpg_disable(struct dp_catalog_panel *panel)
{
	struct dp_catalog_private *catalog;

	if (!panel) {
		DRM_ERROR("invalid input\n");
		return;
	}

	dp_catalog_get_priv(panel);

	dp_write_p0(catalog, MMSS_DP_TPG_MAIN_CONTROL, 0x0);
	dp_write_p0(catalog, MMSS_DP_BIST_ENABLE, 0x0);
	dp_write_p0(catalog, MMSS_DP_TIMING_ENGINE_EN, 0x0);
	return;
}

struct dp_catalog *dp_catalog_get(struct device *dev, struct dp_io *io)
{
	int rc = 0;
	struct dp_catalog *dp_catalog;
	struct dp_catalog_private *catalog;

	if (!io) {
		DRM_ERROR("invalid input\n");
		rc = -EINVAL;
		goto error;
	}

	catalog  = devm_kzalloc(dev, sizeof(*catalog), GFP_KERNEL);
	if (!catalog) {
		rc = -ENOMEM;
		goto error;
	}

	catalog->dev = dev;
	catalog->io = io;

	dp_catalog = &catalog->dp_catalog;

	return dp_catalog;
error:
	return ERR_PTR(rc);
}

void dp_catalog_put(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog;

	if (!dp_catalog)
		return;

	catalog = container_of(dp_catalog, struct dp_catalog_private,
				dp_catalog);

	devm_kfree(catalog->dev, catalog);
}
