// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#include <linux/iopoll.h>

#include "dpu_hwio.h"
#include "dpu_hw_catalog.h"
#include "dpu_hw_intf.h"
#include "dpu_kms.h"

#define INTF_TIMING_ENGINE_EN           0x000
#define INTF_CONFIG                     0x004
#define INTF_HSYNC_CTL                  0x008
#define INTF_VSYNC_PERIOD_F0            0x00C
#define INTF_VSYNC_PERIOD_F1            0x010
#define INTF_VSYNC_PULSE_WIDTH_F0       0x014
#define INTF_VSYNC_PULSE_WIDTH_F1       0x018
#define INTF_DISPLAY_V_START_F0         0x01C
#define INTF_DISPLAY_V_START_F1         0x020
#define INTF_DISPLAY_V_END_F0           0x024
#define INTF_DISPLAY_V_END_F1           0x028
#define INTF_ACTIVE_V_START_F0          0x02C
#define INTF_ACTIVE_V_START_F1          0x030
#define INTF_ACTIVE_V_END_F0            0x034
#define INTF_ACTIVE_V_END_F1            0x038
#define INTF_DISPLAY_HCTL               0x03C
#define INTF_ACTIVE_HCTL                0x040
#define INTF_BORDER_COLOR               0x044
#define INTF_UNDERFLOW_COLOR            0x048
#define INTF_HSYNC_SKEW                 0x04C
#define INTF_POLARITY_CTL               0x050
#define INTF_TEST_CTL                   0x054
#define INTF_TP_COLOR0                  0x058
#define INTF_TP_COLOR1                  0x05C
#define INTF_FRAME_LINE_COUNT_EN        0x0A8
#define INTF_FRAME_COUNT                0x0AC
#define   INTF_LINE_COUNT               0x0B0

#define   INTF_DEFLICKER_CONFIG         0x0F0
#define   INTF_DEFLICKER_STRNG_COEFF    0x0F4
#define   INTF_DEFLICKER_WEAK_COEFF     0x0F8

#define   INTF_DSI_CMD_MODE_TRIGGER_EN  0x084
#define   INTF_PANEL_FORMAT             0x090
#define   INTF_TPG_ENABLE               0x100
#define   INTF_TPG_MAIN_CONTROL         0x104
#define   INTF_TPG_VIDEO_CONFIG         0x108
#define   INTF_TPG_COMPONENT_LIMITS     0x10C
#define   INTF_TPG_RECTANGLE            0x110
#define   INTF_TPG_INITIAL_VALUE        0x114
#define   INTF_TPG_BLK_WHITE_PATTERN_FRAMES   0x118
#define   INTF_TPG_RGB_MAPPING          0x11C
#define   INTF_PROG_FETCH_START         0x170
#define   INTF_PROG_ROT_START           0x174

#define   INTF_FRAME_LINE_COUNT_EN      0x0A8
#define   INTF_FRAME_COUNT              0x0AC
#define   INTF_LINE_COUNT               0x0B0

#define   INTF_MUX                      0x25C

static struct dpu_intf_cfg *_intf_offset(enum dpu_intf intf,
		struct dpu_mdss_cfg *m,
		void __iomem *addr,
		struct dpu_hw_blk_reg_map *b)
{
	int i;

	for (i = 0; i < m->intf_count; i++) {
		if ((intf == m->intf[i].id) &&
		(m->intf[i].type != INTF_NONE)) {
			b->base_off = addr;
			b->blk_off = m->intf[i].base;
			b->length = m->intf[i].len;
			b->hwversion = m->hwversion;
			b->log_mask = DPU_DBG_MASK_INTF;
			return &m->intf[i];
		}
	}

	return ERR_PTR(-EINVAL);
}

static void dpu_hw_intf_setup_timing_engine(struct dpu_hw_intf *ctx,
		const struct intf_timing_params *p,
		const struct dpu_format *fmt)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	u32 hsync_period, vsync_period;
	u32 display_v_start, display_v_end;
	u32 hsync_start_x, hsync_end_x;
	u32 active_h_start, active_h_end;
	u32 active_v_start, active_v_end;
	u32 active_hctl, display_hctl, hsync_ctl;
	u32 polarity_ctl, den_polarity, hsync_polarity, vsync_polarity;
	u32 panel_format;
	u32 intf_cfg;

	/* read interface_cfg */
	intf_cfg = DPU_REG_READ(c, INTF_CONFIG);
	hsync_period = p->hsync_pulse_width + p->h_back_porch + p->width +
	p->h_front_porch;
	vsync_period = p->vsync_pulse_width + p->v_back_porch + p->height +
	p->v_front_porch;

	display_v_start = ((p->vsync_pulse_width + p->v_back_porch) *
	hsync_period) + p->hsync_skew;
	display_v_end = ((vsync_period - p->v_front_porch) * hsync_period) +
	p->hsync_skew - 1;

	hsync_start_x = p->h_back_porch + p->hsync_pulse_width;
	hsync_end_x = hsync_period - p->h_front_porch - 1;

	if (p->width != p->xres) {
		active_h_start = hsync_start_x;
		active_h_end = active_h_start + p->xres - 1;
	} else {
		active_h_start = 0;
		active_h_end = 0;
	}

	if (p->height != p->yres) {
		active_v_start = display_v_start;
		active_v_end = active_v_start + (p->yres * hsync_period) - 1;
	} else {
		active_v_start = 0;
		active_v_end = 0;
	}

	if (active_h_end) {
		active_hctl = (active_h_end << 16) | active_h_start;
		intf_cfg |= BIT(29);	/* ACTIVE_H_ENABLE */
	} else {
		active_hctl = 0;
	}

	if (active_v_end)
		intf_cfg |= BIT(30); /* ACTIVE_V_ENABLE */

	hsync_ctl = (hsync_period << 16) | p->hsync_pulse_width;
	display_hctl = (hsync_end_x << 16) | hsync_start_x;

	if (ctx->cap->type == INTF_DP) {
		printk("ctx->cap->type == INTF_DP\n");
		active_h_start = hsync_start_x;
		active_h_end = active_h_start + p->xres - 1;
		active_v_start = display_v_start;
		active_v_end = active_v_start + (p->yres * hsync_period) - 1;

		display_v_start += p->hsync_pulse_width + p->h_back_porch;

		active_hctl = (active_h_end << 16) | active_h_start;
		display_hctl = active_hctl;
	}

	den_polarity = 0;
	if (ctx->cap->type == INTF_HDMI) {
		hsync_polarity = p->yres >= 720 ? 0 : 1;
		vsync_polarity = p->yres >= 720 ? 0 : 1;
	} else if (ctx->cap->type == INTF_DP) {
		hsync_polarity = p->hsync_polarity;
		vsync_polarity = p->vsync_polarity;
	} else {
		hsync_polarity = 0;
		vsync_polarity = 0;
	}
	polarity_ctl = (den_polarity << 2) | /*  DEN Polarity  */
		(vsync_polarity << 1) | /* VSYNC Polarity */
		(hsync_polarity << 0);  /* HSYNC Polarity */

	if (!DPU_FORMAT_IS_YUV(fmt))
		panel_format = (fmt->bits[C0_G_Y] |
				(fmt->bits[C1_B_Cb] << 2) |
				(fmt->bits[C2_R_Cr] << 4) |
				(0x21 << 8));
	else
		/* Interface treats all the pixel data in RGB888 format */
		panel_format = (COLOR_8BIT |
				(COLOR_8BIT << 2) |
				(COLOR_8BIT << 4) |
				(0x21 << 8));

	DPU_REG_WRITE(c, INTF_HSYNC_CTL, hsync_ctl);
	DPU_REG_WRITE(c, INTF_VSYNC_PERIOD_F0, vsync_period * hsync_period);
	DPU_REG_WRITE(c, INTF_VSYNC_PULSE_WIDTH_F0,
			p->vsync_pulse_width * hsync_period);
	DPU_REG_WRITE(c, INTF_DISPLAY_HCTL, display_hctl);
	DPU_REG_WRITE(c, INTF_DISPLAY_V_START_F0, display_v_start);
	DPU_REG_WRITE(c, INTF_DISPLAY_V_END_F0, display_v_end);
	DPU_REG_WRITE(c, INTF_ACTIVE_HCTL,  active_hctl);
	DPU_REG_WRITE(c, INTF_ACTIVE_V_START_F0, active_v_start);
	DPU_REG_WRITE(c, INTF_ACTIVE_V_END_F0, active_v_end);
	DPU_REG_WRITE(c, INTF_BORDER_COLOR, p->border_clr);
	DPU_REG_WRITE(c, INTF_UNDERFLOW_COLOR, p->underflow_clr);
	DPU_REG_WRITE(c, INTF_HSYNC_SKEW, p->hsync_skew);
	DPU_REG_WRITE(c, INTF_POLARITY_CTL, polarity_ctl);
	DPU_REG_WRITE(c, INTF_FRAME_LINE_COUNT_EN, 0x3);
	DPU_REG_WRITE(c, INTF_CONFIG, intf_cfg);
	DPU_REG_WRITE(c, INTF_PANEL_FORMAT, panel_format);
}

static void dpu_hw_intf_enable_timing_engine(
		struct dpu_hw_intf *intf,
		u8 enable)
{
	struct dpu_hw_blk_reg_map *c = &intf->hw;
	/* Note: Display interface select is handled in top block hw layer */
	DPU_REG_WRITE(c, INTF_TIMING_ENGINE_EN, enable != 0);
}

static void dpu_hw_intf_setup_prg_fetch(
		struct dpu_hw_intf *intf,
		const struct intf_prog_fetch *fetch)
{
	struct dpu_hw_blk_reg_map *c = &intf->hw;
	int fetch_enable;

	/*
	 * Fetch should always be outside the active lines. If the fetching
	 * is programmed within active region, hardware behavior is unknown.
	 */

	fetch_enable = DPU_REG_READ(c, INTF_CONFIG);
	if (fetch->enable) {
		fetch_enable |= BIT(31);
		DPU_REG_WRITE(c, INTF_PROG_FETCH_START,
				fetch->fetch_start);
	} else {
		fetch_enable &= ~BIT(31);
	}

	DPU_REG_WRITE(c, INTF_CONFIG, fetch_enable);
}

static void dpu_hw_intf_get_status(
		struct dpu_hw_intf *intf,
		struct intf_status *s)
{
	struct dpu_hw_blk_reg_map *c = &intf->hw;

	s->is_en = DPU_REG_READ(c, INTF_TIMING_ENGINE_EN);
	if (s->is_en) {
		s->frame_count = DPU_REG_READ(c, INTF_FRAME_COUNT);
		s->line_count = DPU_REG_READ(c, INTF_LINE_COUNT);
	} else {
		s->line_count = 0;
		s->frame_count = 0;
	}
}

static u32 dpu_hw_intf_get_line_count(struct dpu_hw_intf *intf)
{
	struct dpu_hw_blk_reg_map *c;

	if (!intf)
		return 0;

	c = &intf->hw;

	return DPU_REG_READ(c, INTF_LINE_COUNT);
}

#define INTF_TEAR_MDP_VSYNC_SEL         0x280
#define INTF_TEAR_TEAR_CHECK_EN         0x284
#define INTF_TEAR_SYNC_CONFIG_VSYNC     0x288
#define INTF_TEAR_SYNC_CONFIG_HEIGHT    0x28C
#define INTF_TEAR_SYNC_WRCOUNT          0x290
#define INTF_TEAR_VSYNC_INIT_VAL        0x294
#define INTF_TEAR_INT_COUNT_VAL         0x298
#define INTF_TEAR_SYNC_THRESH           0x29C
#define INTF_TEAR_START_POS             0x2A0
#define INTF_TEAR_RD_PTR_IRQ            0x2A4
#define INTF_TEAR_WR_PTR_IRQ            0x2A8
#define INTF_TEAR_OUT_LINE_COUNT        0x2AC
#define INTF_TEAR_LINE_COUNT            0x2B0
#define INTF_TEAR_AUTOREFRESH_CONFIG    0x2B4
#define INTF_TEAR_TEAR_DETECT_CTRL      0x2B8

static int dpu_hw_intf_setup_te_config(struct dpu_hw_intf *intf,
		struct dpu_hw_tear_check *te)
{
	struct dpu_hw_blk_reg_map *c;
	int cfg;

	if (!intf)
		return -EINVAL;

	c = &intf->hw;

	cfg = BIT(19); /* VSYNC_COUNTER_EN */
	if (te->hw_vsync_mode)
		cfg |= BIT(20);

	cfg |= te->vsync_count;

	DPU_REG_WRITE(c, INTF_TEAR_SYNC_CONFIG_VSYNC, cfg);
	DPU_REG_WRITE(c, INTF_TEAR_SYNC_CONFIG_HEIGHT, te->sync_cfg_height);
	DPU_REG_WRITE(c, INTF_TEAR_VSYNC_INIT_VAL, te->vsync_init_val);
	DPU_REG_WRITE(c, INTF_TEAR_RD_PTR_IRQ, te->rd_ptr_irq);
	DPU_REG_WRITE(c, INTF_TEAR_START_POS, te->start_pos);
	DPU_REG_WRITE(c, INTF_TEAR_SYNC_THRESH,
			((te->sync_threshold_continue << 16) |
			 te->sync_threshold_start));
	DPU_REG_WRITE(c, INTF_TEAR_SYNC_WRCOUNT,
			(te->start_pos + te->sync_threshold_start + 1));

	return 0;
}

static int dpu_hw_intf_setup_autorefresh_config(struct dpu_hw_intf *intf,
		struct dpu_hw_autorefresh *cfg)
{
	struct dpu_hw_blk_reg_map *c;
	u32 refresh_cfg;

	if (!intf)
		return -EINVAL;

	c = &intf->hw;

	if (cfg->enable)
		refresh_cfg = BIT(31) | cfg->frame_count;
	else
		refresh_cfg = 0;

	DPU_REG_WRITE(c, INTF_TEAR_AUTOREFRESH_CONFIG, refresh_cfg);

	return 0;
}

static int dpu_hw_intf_get_autorefresh_config(struct dpu_hw_intf *intf,
		struct dpu_hw_autorefresh *cfg)
{
	struct dpu_hw_blk_reg_map *c;
	u32 val;

	if (!intf)
		return -EINVAL;

	c = &intf->hw;
	val = DPU_REG_READ(c, INTF_TEAR_AUTOREFRESH_CONFIG);
	cfg->enable = (val & BIT(31)) >> 31;
	cfg->frame_count = val & 0xffff;

	return 0;
}

static int dpu_hw_intf_poll_timeout_wr_ptr(struct dpu_hw_intf *intf,
		u32 timeout_us)
{
	struct dpu_hw_blk_reg_map *c;
	u32 val;
	int rc;

	if (!intf)
		return -EINVAL;

	c = &intf->hw;
	rc = readl_poll_timeout(c->base_off + c->blk_off + INTF_TEAR_LINE_COUNT,
			val, (val & 0xffff) >= 1, 10, timeout_us);

	return rc;
}

static int dpu_hw_intf_enable_te(struct dpu_hw_intf *intf, bool enable)
{
	struct dpu_hw_blk_reg_map *c;

	if (!intf)
		return -EINVAL;

	c = &intf->hw;
	DPU_REG_WRITE(c, INTF_TEAR_TEAR_CHECK_EN, enable);
	return 0;
}

#if 0
static void dpu_hw_intf_update_te(struct dpu_hw_intf *intf,
		struct dpu_hw_tear_check *te)
{
	struct dpu_hw_blk_reg_map *c;
	int cfg;

	if (!intf || !te)
		return;

	c = &intf->hw;
	cfg = dpu_REG_READ(c, INTF_TEAR_SYNC_THRESH);
	cfg &= ~0xFFFF;
	cfg |= te->sync_threshold_start;
	DPU_REG_WRITE(c, INTF_TEAR_SYNC_THRESH, cfg);
}
#endif

static int dpu_hw_intf_connect_external_te(struct dpu_hw_intf *intf,
		bool enable_external_te)
{
	struct dpu_hw_blk_reg_map *c = &intf->hw;
	u32 cfg;
	int orig;

	if (!intf)
		return -EINVAL;

	c = &intf->hw;
	cfg = DPU_REG_READ(c, INTF_TEAR_SYNC_CONFIG_VSYNC);
	orig = (bool)(cfg & BIT(20));
	if (enable_external_te)
		cfg |= BIT(20);
	else
		cfg &= ~BIT(20);
	DPU_REG_WRITE(c, INTF_TEAR_SYNC_CONFIG_VSYNC, cfg);

	return orig;
}

static int dpu_hw_intf_get_vsync_info(struct dpu_hw_intf *intf,
		struct dpu_hw_pp_vsync_info *info)
{
	struct dpu_hw_blk_reg_map *c = &intf->hw;
	u32 val;

	if (!intf || !info)
		return -EINVAL;

	c = &intf->hw;

	val = DPU_REG_READ(c, INTF_TEAR_VSYNC_INIT_VAL);
	info->rd_ptr_init_val = val & 0xffff;

	val = DPU_REG_READ(c, INTF_TEAR_INT_COUNT_VAL);
	info->rd_ptr_frame_count = (val & 0xffff0000) >> 16;
	info->rd_ptr_line_count = val & 0xffff;

	val = DPU_REG_READ(c, INTF_TEAR_LINE_COUNT);
	info->wr_ptr_line_count = val & 0xffff;

	return 0;
}

#if 0
static void dpu_hw_intf_vsync_sel(struct dpu_hw_intf *intf,
		u32 vsync_source)
{
	struct dpu_hw_blk_reg_map *c;

	if (!intf)
		return;

	c = &intf->hw;

	DPU_REG_WRITE(c, INTF_TEAR_MDP_VSYNC_SEL, (vsync_source & 0xf));
}
#endif

static void dpu_hw_intf_bind_pingpong_blk(
		struct dpu_hw_intf *intf,
		bool enable,
		const enum dpu_pingpong pp)
{
	struct dpu_hw_blk_reg_map *c;
	u32 mux_cfg;

	if (!intf)
		return;

	c = &intf->hw;

	mux_cfg = DPU_REG_READ(c, INTF_MUX);
	mux_cfg &= ~0xf;

	if (enable)
		mux_cfg |= (pp - PINGPONG_0) & 0x7;
	else
		mux_cfg |= 0xf;

	DPU_REG_WRITE(c, INTF_MUX, mux_cfg);
}

static void _setup_intf_ops(struct dpu_hw_intf_ops *ops,
		unsigned long cap)
{
	ops->setup_timing_gen = dpu_hw_intf_setup_timing_engine;
	ops->setup_prg_fetch  = dpu_hw_intf_setup_prg_fetch;
	ops->get_status = dpu_hw_intf_get_status;
	ops->enable_timing = dpu_hw_intf_enable_timing_engine;
	ops->get_line_count = dpu_hw_intf_get_line_count;

	ops->setup_tearcheck = dpu_hw_intf_setup_te_config;
	ops->enable_tearcheck = dpu_hw_intf_enable_te;
	ops->connect_external_te = dpu_hw_intf_connect_external_te;
	ops->get_vsync_info = dpu_hw_intf_get_vsync_info;
	ops->setup_autorefresh = dpu_hw_intf_setup_autorefresh_config;
	ops->get_autorefresh = dpu_hw_intf_get_autorefresh_config;
	ops->poll_timeout_wr_ptr = dpu_hw_intf_poll_timeout_wr_ptr;
	if (cap & BIT(DPU_CTL_ACTIVE_CFG))
		ops->bind_pingpong_blk = dpu_hw_intf_bind_pingpong_blk;
}

static struct dpu_hw_blk_ops dpu_hw_ops;

struct dpu_hw_intf *dpu_hw_intf_init(enum dpu_intf idx,
		void __iomem *addr,
		struct dpu_mdss_cfg *m)
{
	struct dpu_hw_intf *c;
	struct dpu_intf_cfg *cfg;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return ERR_PTR(-ENOMEM);

	cfg = _intf_offset(idx, m, addr, &c->hw);
	if (IS_ERR_OR_NULL(cfg)) {
		kfree(c);
		pr_err("failed to create dpu_hw_intf %d\n", idx);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * Assign ops
	 */
	c->idx = idx;
	c->cap = cfg;
	c->mdss = m;
	_setup_intf_ops(&c->ops, c->cap->features);

	dpu_hw_blk_init(&c->base, DPU_HW_BLK_INTF, idx, &dpu_hw_ops);

	return c;
}

void dpu_hw_intf_destroy(struct dpu_hw_intf *intf)
{
	if (intf)
		dpu_hw_blk_destroy(&intf->base);
	kfree(intf);
}
