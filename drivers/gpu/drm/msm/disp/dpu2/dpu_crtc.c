// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include "dpu_common.h"

// TODO
// primary vs overlay planes (vkcube doesn't UBWC...)
// UBWC setting in top
// enable async commit
// irq stuff
// dma plane crash
// weston mode change problem
// 4k mode needs larger vporch than expected (not related to dpu)

/* flush bits table for 4 vig and 4 dma config */
static const u8 sspp_flush_tbl[8] = {0, 1, 2, 18, 11, 12, 24, 25};
#define CTL_FLUSH_SSPP(i) BIT(sspp_flush_tbl[i])

static void
intf_timings(struct dpu_kms *dpu, u32 i, const struct drm_display_mode *mode,
	     bool is_dual_dsi, bool is_dp, bool is_dsc)
{
	/* from dsi_host.c */
	u32 h_total = mode->htotal;
	u32 v_total = mode->vtotal;
	u32 hs_end = mode->hsync_end - mode->hsync_start; /* h_pulse_width */
	u32 vs_end = mode->vsync_end - mode->vsync_start; /* v_pulse_width */
	u32 ha_start = h_total - mode->hsync_start;
	u32 ha_end = ha_start + mode->hdisplay;
	u32 va_start = v_total - mode->vsync_start;
	u32 va_end = va_start + mode->vdisplay;
	u32 hdisplay = mode->hdisplay;

	if (is_dual_dsi) {
		h_total /= 2;
		hs_end /= 2;
		ha_start /= 2;
		ha_end /= 2;
		hdisplay /= 2;
	}

	if (is_dsc) {
		h_total -= hdisplay;
		hdisplay /= 3;
		h_total += hdisplay;
		ha_end = ha_start + hdisplay;
	}

	if (is_dp) {
		ha_start = h_total - mode->hdisplay;
		ha_end = h_total;
		va_start = v_total - mode->vdisplay;
		va_end = v_total;
	}

	va_start = va_start * h_total + mode->hskew;
	va_end = va_end * h_total + mode->hskew - 1;
	ha_end = ha_end - 1;

	dpu_write(dpu, INTF_HSYNC_CTL(i), h_total << 16 | hs_end);
	dpu_write(dpu, INTF_VSYNC_PERIOD_F0(i), h_total * v_total);
	dpu_write(dpu, INTF_VSYNC_PULSE_WIDTH_F0(i), h_total * vs_end);
	dpu_write(dpu, INTF_DISPLAY_HCTL(i), ha_end << 16 | ha_start);

	if (is_dp) {
		dpu_write(dpu, INTF_DISPLAY_V_START_F0(i), va_start + ha_start);
		dpu_write(dpu, INTF_DISPLAY_V_END_F0(i), va_end);
		dpu_write(dpu, INTF_ACTIVE_HCTL(i), ha_end << 16 | ha_start);
		dpu_write(dpu, INTF_ACTIVE_V_START_F0(i), va_start);
		dpu_write(dpu, INTF_ACTIVE_V_END_F0(i), va_end);
	} else {
		dpu_write(dpu, INTF_DISPLAY_V_START_F0(i), va_start);
		dpu_write(dpu, INTF_DISPLAY_V_END_F0(i), va_end);
		dpu_write(dpu, INTF_ACTIVE_HCTL(i), 0);
		dpu_write(dpu, INTF_ACTIVE_V_START_F0(i), 0);
		dpu_write(dpu, INTF_ACTIVE_V_END_F0(i), 0);
	}

	dpu_write(dpu, INTF_BORDER_COLOR(i), 0);
	dpu_write(dpu, INTF_UNDERFLOW_COLOR(i), 0xff);
	dpu_write(dpu, INTF_HSYNC_SKEW(i), mode->hskew);
	dpu_write(dpu, INTF_POLARITY_CTL(i),
		  COND(mode->flags & DRM_MODE_FLAG_NHSYNC, 1) |
		  COND(mode->flags & DRM_MODE_FLAG_NVSYNC, 2));
	dpu_write(dpu, INTF_FRAME_LINE_COUNT_EN(i), 0x3);
	dpu_write(dpu, INTF_PANEL_FORMAT(i), 0x213f);

	/* XXX */
	if (!is_dp) {
		dpu_write(dpu, INTF_PROG_FETCH_START(i), 0);//is_dsc ? 0x1A0CC1 : 0x1B2731);
		dpu_write(dpu, INTF_CONFIG(i), 0x00800000);
		dpu_write(dpu, INTF_CONFIG2(i), 0);
	} else {
		dpu_write(dpu, INTF_CONFIG(i), 0x60800000);
		dpu_write(dpu, INTF_CONFIG2(i), 0x10);
		dpu_write(dpu, INTF_DISPLAY_DATA_HCTL(i), ha_end << 16 | ha_start);
	}
}

#define DSC_MODE_SPLIT_PANEL            BIT(0)
#define DSC_MODE_MULTIPLEX              BIT(1)
#define DSC_MODE_VIDEO                  BIT(2)

static void
set_dsc_config(struct dpu_kms *dpu, u32 i, struct drm_dsc_config *dsc)
{
	u32 det_thresh_flatness = 2 << (dsc->bits_per_component - 8);
	u32 slice_last_group_size = 2;
	u32 mux_words_size = 48, num_extra_mux_bits;
	u32 bpc = 8, bpp = 8;
	u32 groups_per_line = DIV_ROUND_UP(dsc->slice_width, 3);
	u32 nfl_bpg_offset, slice_bpg_offset;
	u32 chunk_size, slice_bits, final_offset, final_scale;
	u32 scale_increment_interval, scale_decrement_interval;
	u32 j, data;
	u32 initial_dec_delay = 0x20e;//XXX
	u32 initial_scale_value = 0x20; //XXX

	dpu_write(dpu, DSC_COMMON_MODE(i),
		  DSC_MODE_MULTIPLEX | DSC_MODE_SPLIT_PANEL | DSC_MODE_VIDEO);

	// set 3 << 28 for ich_reset??
	dpu_write(dpu, DSC_ENC(i),
		  3 << 20 | /* initial_lines */
		  slice_last_group_size << 18 |
		  dsc->bits_per_pixel << 12 |
		  dsc->block_pred_enable  << 7 |
		  dsc->line_buf_depth  << 3 |
		  dsc->simple_422  << 2 |
		  dsc->convert_rgb << 1 |
		  COND(dsc->bits_per_component == 10, BIT(0)));

	dpu_write(dpu, DSC_PICTURE(i), 2160 << 16 | 2160);//dsc->pic_width << 16 | dsc->pic_height);
	dpu_write(dpu, DSC_SLICE(i), dsc->slice_width << 16 | dsc->slice_height);

	chunk_size = dsc->slice_width * bpp / 8;
	if ((dsc->slice_width * bpp) % 8)
		chunk_size++;
	slice_bits = 8 * chunk_size * dsc->slice_height;


	data = dsc->first_line_bpg_offset * 2048;
	nfl_bpg_offset = DIV_ROUND_UP(data, dsc->slice_height - 1);

	data = 3 * (mux_words_size + (4 * bpc + 4) - 2);
	num_extra_mux_bits = data - (mux_words_size - ((slice_bits - data) % mux_words_size));
	data = dsc->rc_model_size - dsc->initial_offset + num_extra_mux_bits;
	slice_bpg_offset = DIV_ROUND_UP(2048 * data, groups_per_line * dsc->slice_height);

	final_offset = dsc->rc_model_size - dsc->initial_xmit_delay * bpp + num_extra_mux_bits;
	final_scale = 8 * dsc->rc_model_size / (dsc->rc_model_size - final_offset);
	data = (final_scale - 9) * nfl_bpg_offset + slice_bpg_offset;
	scale_increment_interval = (2048 * final_offset) / data;
	scale_decrement_interval = groups_per_line / (initial_scale_value - 8);

	// XXX
	scale_increment_interval = 0x4a6;

	dpu_write(dpu, DSC_CHUNK_SIZE(i), chunk_size << 16);
	dpu_write(dpu, DSC_DELAY(i), initial_dec_delay << 16 | dsc->initial_xmit_delay);
	dpu_write(dpu, DSC_SCALE_INITIAL(i), initial_scale_value);
	dpu_write(dpu, DSC_SCALE_DEC_INTERVAL(i), scale_decrement_interval);//0x00000007);
	dpu_write(dpu, DSC_SCALE_INC_INTERVAL(i), scale_increment_interval);//0x000000BB);
	dpu_write(dpu, DSC_FIRST_LINE_BPG_OFFSET(i), dsc->first_line_bpg_offset);
	// 0x0DB70CB7, 0x180010F0
	dpu_write(dpu, DSC_BPG_OFFSET(i), nfl_bpg_offset << 16 | slice_bpg_offset);
	dpu_write(dpu, DSC_DSC_OFFSET(i), dsc->initial_offset << 16 | final_offset);

	dpu_write(dpu, DSC_FLATNESS(i),
		  det_thresh_flatness << 10 |
		  dsc->flatness_max_qp << 5 |
		  dsc->flatness_min_qp);

	dpu_write(dpu, DSC_RC_MODEL_SIZE(i), dsc->rc_model_size);
	dpu_write(dpu, DSC_RC(i),
		  dsc->rc_tgt_offset_low << 18 |
		  dsc->rc_tgt_offset_high << 14 |
		  dsc->rc_quant_incr_limit1 << 9 |
		  dsc->rc_quant_incr_limit0 << 4 |
		  dsc->rc_edge_factor);


	for (j = 0; j < ARRAY_SIZE(dsc->rc_buf_thresh); j++)
		dpu_write(dpu, DSC_RC_BUF_THRESH(i) + j * 4, dsc->rc_buf_thresh[j]);

	for (j = 0; j < ARRAY_SIZE(dsc->rc_range_params); j++) {
		struct drm_dsc_rc_range_parameters *rc = &dsc->rc_range_params[j];
		dpu_write(dpu, DSC_RANGE_MIN_QP(i) + j * 4, rc->range_min_qp);
		dpu_write(dpu, DSC_RANGE_MAX_QP(i) + j * 4, rc->range_max_qp);
		dpu_write(dpu, DSC_RANGE_BPG_OFFSET(i) + j * 4, rc->range_bpg_offset);
	}

	/* set endian flip */
	dpu_write(dpu, PP_DCE_DATA_OUT_SWAP(i),
		  dpu_read(dpu, PP_DCE_DATA_OUT_SWAP(i)) | BIT(18));
	dpu_write(dpu, PP_DSC_MODE(i), 1);

	dpu_write(dpu, DSC_CTL(i), i); // id of pp, set to 0xf to disable */
}

static void
set_dsc_disable(struct dpu_kms *dpu, u32 i)
{
	dpu_write(dpu, PP_DCE_DATA_OUT_SWAP(i),
		  dpu_read(dpu, PP_DCE_DATA_OUT_SWAP(i)) & ~BIT(18));
	dpu_write(dpu, PP_DSC_MODE(i), 0);
	dpu_write(dpu, DSC_CTL(i), 0xf);
}

static void
dpu_crtc_disable(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	drm_crtc_vblank_off(crtc);
}

static void
dpu_crtc_enable(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	drm_crtc_vblank_on(crtc);
}

static int
dpu_crtc_atomic_check(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	/* TODO: actually perform checks */
	return 0;
}

static void
dpu_crtc_atomic_begin(struct drm_crtc *crtc, struct drm_atomic_state *old_state)
{
	/* nothing */
}

static const u8 rt_pri_lvl[] = {3, 3, 4, 4, 5, 5, 6, 6};
static const u8 sspp_xin_id[] = {0, 4, 8, 12, 1, 5, 9, 13};

enum {
	/* alpha source */
	ALPHA_FG_CONST,
	ALPHA_BG_CONST,
	ALPHA_FG_PIXEL,
	ALPHA_BG_PIXEL,
	/* modifiers */
	INV_ALPHA = BIT(2),
	MOD_ALPHA = BIT(3),
	INV_MOD_ALPHA = BIT(4),
	TRANSP_EN = BIT(5),
};

#define CTL_MIXER_BORDER_OUT            BIT(24)

static void
mixer_set_blendfunc(struct dpu_kms *dpu, u32 i, u32 stage)
{
	u32 alpha_const = 0xff << 16 | 0; /* fg_const = 0xff, bg_const = 0 */
	u32 fg_func = ALPHA_FG_CONST; /* solid fg */
	u32 bg_func = ALPHA_BG_CONST; /* invisible bg */

	/* TODO: blending for format with alpha channel */
	if (0) {
		fg_func = ALPHA_FG_PIXEL;
		bg_func = ALPHA_FG_PIXEL | INV_ALPHA;
	}

	dpu_write(dpu, LM_BLEND0_OP(i, stage), fg_func | bg_func << 8);
	dpu_write(dpu, LM_BLEND0_CONST_ALPHA(i, stage), alpha_const);
}

struct mixercfg {
	u32 cfg[4];
};

static void
mixercfg_set_sspp_stage(struct mixercfg *cfg, u32 sspp, u32 stage)
{
	/* assumes sspp index is 0-3 VIG, 4-7 DMA */
	static const u8 stage_bits_lo[8] = {0, 3, 6, 26, 18, 21, 0, 4};
	static const u8 stage_bits_hi[6] = {0, 2, 4,  6, 16, 18};

	/* offset to skip (0 = no stage, 1 = base stage) */
	stage += 2;

	if (sspp < 6) {
		cfg->cfg[0] |= (stage & 7) << stage_bits_lo[sspp];
		cfg->cfg[1] |= (stage >= 8) << stage_bits_hi[sspp];
	} else {
		cfg->cfg[2] |= stage << stage_bits_lo[sspp];
	}
}

static void
ctl_set_mixercfg(struct dpu_kms *dpu, u32 ctl, u32 i, struct mixercfg *cfg)
{
	dpu_write(dpu, CTL_LAYER(ctl, i), cfg->cfg[0] | CTL_MIXER_BORDER_OUT);
	dpu_write(dpu, CTL_LAYER_EXT(ctl, i), cfg->cfg[1]);
	dpu_write(dpu, CTL_LAYER_EXT2(ctl, i), cfg->cfg[2]);
	dpu_write(dpu, CTL_LAYER_EXT3(ctl, i), cfg->cfg[3]);
}

static void
dpu_crtc_atomic_flush(struct drm_crtc *crtc, struct drm_atomic_state *old_state)
{
	struct drm_crtc_state *state = crtc->state;
	struct drm_plane *plane;
	const struct drm_display_mode *mode = &state->mode;
	struct msm_drm_private *priv = crtc->dev->dev_private;
	struct dpu_kms *dpu = to_dpu_kms(priv->kms);
	struct dpu_crtc *c = to_dpu_crtc(crtc);
	unsigned long flags;
	bool is_dsc = priv->dsc.dsc_version_major && !c->is_dp;

	struct mixercfg mixer_cfg[4] = {};
	struct drm_rect mixer_rect[4];
	u32 intf_active = 0;
	u32 merge3d_active = 0;
	u32 ctl_flush = CTL_FLUSH_MASK_CTL;
	u32 mixer_op_mode = 0;
	u32 fetch_pipe_active = 0;

	unsigned i, num_mixers, num_mixercfg;

	/*
	 * DSC has a max width of 2048, so for dual 2160x2160 panels with DSC,
	 * 4 mixers are required even though mixer max width is 2560.
	 * this is a "QUADPIPE_DSCMERGE" topology
	 *
	 * for this configuration, we need to use 2 HW planes per plane because
	 * 1) plane max width is 4096, lower than 4320
	 * 2) a single plane can only be used by one mixer pair
	 * 3) plane can only read 1 pixel/clock, but max DPU clock is 460Mhz
	 *
	 * 3) is also a problem with DP (2 mixer merge3d configuration),
	 * because for example 3840x2160@60 needs more than 460Mhz DPU clock
	 * so 2 HW plane mechanism is used with DP too to avoid overclocking
	 *
	 * this 2 plane thing might result in slightly incorrect scaling when
	 * the plane is split across two HW planes, but this driver doesn't
	 * support scaling..
	 */

	if (c->is_dual_dsi && is_dsc)
		num_mixers = 4;
	else if (c->is_dp || c->is_dual_dsi)
		num_mixers = 2;
	else
		num_mixers = 1;
	num_mixercfg = num_mixers;

	if (num_mixercfg == 4) {
		mixer_rect[0] = (struct drm_rect) {
			0, 0, 1080, mode->vdisplay,
		};
		mixer_rect[1] = (struct drm_rect) {
			1080, 0, 2160, mode->vdisplay,
		};
		mixer_rect[2] = (struct drm_rect) {
			2160, 0, 3240, mode->vdisplay,
		};
		mixer_rect[3] = (struct drm_rect) {
			3240, 0, 4320, mode->vdisplay,
		};
	} else if (num_mixercfg == 2) {
		mixer_rect[0] = (struct drm_rect) {
			0, 0, mode->hdisplay / 2, mode->vdisplay,
		};
		mixer_rect[1] = (struct drm_rect) {
			mode->hdisplay / 2, 0, mode->hdisplay, mode->vdisplay,
		};
	} else {
		mixer_rect[0] = (struct drm_rect) {
			0, 0, mode->hdisplay, mode->vdisplay,
		};
	}

	if (!state->enable) {
		/*
		 * timing engine is disabled so vblank irq will never come
		 * complete the vblank event immediately
		 */
		dpu_complete_flip(dpu, c);
		/*
		 * skip crtc state update because it will be updated with new state
		 * before the timing engine is enabled again
		 */
		return;
	}

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		u32 stage = plane->state->normalized_zpos;

		// TODO: should be per mixer for dual HW planes
		mixer_op_mode |= 1 << (stage + 1); /* + 1 to skip base stage */

		for (i = c->lm; i < c->lm + num_mixers; i++)
			mixer_set_blendfunc(dpu, i, stage);

		for (i = 0; i < num_mixercfg; i++) {
			u32 id = !to_dpu_plane(plane)->id * 4 + i;
			struct drm_plane_state s = *plane->state;
			if (!drm_rect_clip_scaled(&s.src, &s.dst, &mixer_rect[i]))
				continue;

			s.dst.x1 -= mixer_rect[i].x1;
			s.dst.x2 -= mixer_rect[i].x1;

			/* mapping sspp to mixer stages */
			mixercfg_set_sspp_stage(&mixer_cfg[i], id, stage);

			/* sspp state */
			dpu_plane_update(dpu, id, &s);
			ctl_flush |= CTL_FLUSH_SSPP(id);

			if (id >= 4)
				fetch_pipe_active |= BIT(id - 4);
			else
				fetch_pipe_active |= BIT(id + 16);
		}
	}

	for (i = c->lm; i < c->lm + num_mixers; i++) {
		u32 cfg_index = i - c->lm;//num_mixercfg > 1 ? (i >= c->lm + num_mixers/2) : 0;
		u32 op_mode = mixer_op_mode;

		/* right mixer bit for paired mixers */
		if ((i & 1) && (num_mixers != num_mixercfg))
			op_mode |= BIT(31);

		dpu_write(dpu, LM_OUT_SIZE(i), mode->vdisplay << 16 | (mode->hdisplay / num_mixers));
		dpu_write(dpu, LM_OP_MODE(i), op_mode);

		ctl_set_mixercfg(dpu, c->ctl, i, &mixer_cfg[cfg_index]);

		ctl_flush |= CTL_FLUSH_LM(i);
	}

	if (num_mixers == 2 && !c->is_dp) {
		struct mixercfg cfg = {};
		ctl_set_mixercfg(dpu, c->ctl, c->lm + 2, &cfg);
		ctl_set_mixercfg(dpu, c->ctl, c->lm + 3, &cfg);

		dpu_write(dpu, LM_OUT_SIZE(c->lm + 2), 0);
		dpu_write(dpu, LM_OUT_SIZE(c->lm + 3), 0);
		dpu_write(dpu, LM_OP_MODE(c->lm + 2), 0);
		dpu_write(dpu, LM_OP_MODE(c->lm + 3), 0);

		ctl_flush |= CTL_FLUSH_LM(c->lm + 2);
		ctl_flush |= CTL_FLUSH_LM(c->lm + 3);

	}

	if (state->mode_changed) {
		intf_timings(dpu, c->intf, mode, c->is_dual_dsi, c->is_dp, is_dsc);
		/* low 4 bits idx of pp block, or 0xf disable */
		dpu_write(dpu, INTF_MUX(c->intf), 0xF0000 | c->lm);
		intf_active = BIT(c->intf);
		if (c->is_dual_dsi) {
			intf_timings(dpu, c->intf + 1, mode, c->is_dual_dsi, c->is_dp, is_dsc);
			dpu_write(dpu, INTF_MUX(c->intf + 1), 0xF0000 | (c->lm + num_mixers/2)); // XXX
			intf_active |= BIT(c->intf + 1);
		}
		dpu_write(dpu, CTL_INTF_ACTIVE(c->ctl), intf_active);
		dpu_write(dpu, CTL_INTF_MASTER(c->ctl), BIT(c->intf));

		dpu_write(dpu, CTL_INTF_FLUSH(c->ctl), 0xf); /* bitmask of INTF modified */

		ctl_flush |= CTL_FLUSH_INTF;

		if (is_dsc) {
			set_dsc_config(dpu, 0, &priv->dsc);
			set_dsc_config(dpu, 1, &priv->dsc);
			set_dsc_config(dpu, 2, &priv->dsc);
			set_dsc_config(dpu, 3, &priv->dsc);

			dpu_write(dpu, CTL_DSC_ACTIVE(c->ctl), 0xf); /* bitmask of DSC active */
			dpu_write(dpu, CTL_DSC_FLUSH(c->ctl), 0xf); /* bitmask of DSC modified */
			ctl_flush |= CTL_FLUSH_DSC;
		} else if (!c->is_dp) {
			set_dsc_disable(dpu, 0);
			set_dsc_disable(dpu, 1);
			set_dsc_disable(dpu, 2);
			set_dsc_disable(dpu, 3);
			dpu_write(dpu, CTL_DSC_ACTIVE(c->ctl), 0);
			dpu_write(dpu, CTL_DSC_FLUSH(c->ctl), 0xf); /* bitmask of DSC modified */
			ctl_flush |= CTL_FLUSH_DSC;
		}

		/* always use merge3d for DP */
		if (c->is_dp) {
			dpu_write(dpu, MERGE_3D_MODE(c->lm / 2), num_mixers > 1 ? 3 : 0);
			dpu_write(dpu, MERGE_3D_MUX(c->lm / 2), 0);
			merge3d_active |= BIT(c->lm / 2);

			ctl_flush |= CTL_FLUSH_MERGE_3D;
		}
		dpu_write(dpu, CTL_MERGE_3D_ACTIVE(c->ctl), merge3d_active);
		dpu_write(dpu, CTL_MERGE_3D_FLUSH(c->ctl), 0x7); /* bitmask of MERGE3D flushed */
	}

	dpu_write(dpu, CTL_FETCH_PIPE_ACTIVE(c->ctl), fetch_pipe_active);
	dpu_write(dpu, CTL_TOP(c->ctl), 0xf0000000); /* BIT(17) for command mode */

	dpu_toggle_start_irq(dpu, c->ctl, true);
	dpu_write(dpu, CTL_FLUSH(c->ctl), ctl_flush);


	// XXX race condition with irq??
	WARN_ON(c->event);

	spin_lock_irqsave(&dpu->dev->event_lock, flags);
	c->event = state->event;
	state->event = NULL;
	spin_unlock_irqrestore(&dpu->dev->event_lock, flags);
}

static const struct drm_crtc_funcs dpu_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = msm_crtc_enable_vblank,
	.disable_vblank = msm_crtc_disable_vblank,
};

static const struct drm_crtc_helper_funcs dpu_crtc_helper_funcs = {
	.atomic_disable = dpu_crtc_disable,
	.atomic_enable = dpu_crtc_enable,
	.atomic_check = dpu_crtc_atomic_check,
	.atomic_begin = dpu_crtc_atomic_begin,
	.atomic_flush = dpu_crtc_atomic_flush,
};

int dpu_crtc_init(struct dpu_kms *dpu, struct drm_crtc *crtc, struct drm_plane *plane)
{
	drm_crtc_init_with_planes(dpu->dev, crtc, plane, NULL, &dpu_crtc_funcs, NULL);
	drm_crtc_helper_add(crtc, &dpu_crtc_helper_funcs);

	return 0;
}

