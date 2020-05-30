// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include "dpu_common.h"

#include <drm/drm_fourcc.h>
#include <drm/drm_gem_atomic_helper.h>

static struct dpu_kms *
plane_to_dpu_kms(struct drm_plane *plane)
{
	struct msm_drm_private *priv = plane->dev->dev_private;

	return to_dpu_kms(priv->kms);
}

static bool
dpu_format_mod_supported(struct drm_plane *plane, uint32_t format, uint64_t modifier)
{
	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return true;

	if (modifier == DRM_FORMAT_MOD_QCOM_COMPRESSED)
		return true;

	return false;
}

static int
dpu_plane_prepare_fb(struct drm_plane *plane, struct drm_plane_state *state)
{
	struct dpu_kms *dpu = plane_to_dpu_kms(plane);
	int ret;

	if (!state->fb)
		return 0;

	/*
	 * TODO: Need to sort out the msm_framebuffer_prepare() call below so
	 *       we can use msm_atomic_prepare_fb() instead of doing the
	 *       implicit fence and fb prepare by hand here.
	 */
	drm_gem_plane_helper_prepare_fb(plane, state);

	if (dpu->base.aspace) {
		ret = msm_framebuffer_prepare(state->fb, dpu->base.aspace);
		if (ret) {
			DRM_ERROR("failed to prepare framebuffer\n");
			return ret;
		}
	}

	return 0;
}

static void
dpu_plane_cleanup_fb(struct drm_plane *plane, struct drm_plane_state *state)
{
	struct dpu_kms *dpu = plane_to_dpu_kms(plane);

	if (!state || !state->fb)
		return;

	msm_framebuffer_cleanup(state->fb, dpu->base.aspace);
}

static int dpu_plane_atomic_check(struct drm_plane *plane,
				  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state, plane);
	const struct drm_crtc_state *crtc_state = NULL;

	if (new_plane_state->crtc)
		crtc_state = drm_atomic_get_new_crtc_state(state,
							   new_plane_state->crtc);

	return drm_atomic_helper_check_plane_state(new_plane_state, crtc_state, 1 << 16,
						   1 << 16, true, true);

}

static void
sspp_set_framebuffer(struct dpu_kms *dpu, u8 i, struct drm_framebuffer *fb)
{
	u32 addr[4] = {};
	u32 pitch[4] = {};
	u32 meta_height;

	addr[0] = msm_framebuffer_iova(fb, dpu->base.aspace, 0);
	pitch[0] = fb->pitches[0];

	if (fb->modifier == DRM_FORMAT_MOD_QCOM_COMPRESSED) {
		/*
		 * UBWC plane in addr[2]/pitch[2], but comes before in memory
		 *
		 * note: the UBWC pitch should probably be calculated from the
		 * framebuffer pitch and not framebuffer width..
		 */

		addr[2] = addr[0];
		pitch[2] = ALIGN(DIV_ROUND_UP(fb->width, 16), 64);

		meta_height = ALIGN(DIV_ROUND_UP(fb->height, 4), 16);
		addr[0] += ALIGN(pitch[2] * meta_height, 4096);
	}

	dpu_write(dpu, SSPP_SRC0_ADDR(i) + 0,  addr[0]);
	dpu_write(dpu, SSPP_SRC0_ADDR(i) + 4,  addr[1]);
	dpu_write(dpu, SSPP_SRC0_ADDR(i) + 8,  addr[2]);
	dpu_write(dpu, SSPP_SRC0_ADDR(i) + 12, addr[3]);
	dpu_write(dpu, SSPP_SRC_YSTRIDE0(i), pitch[0] | pitch[1] << 16);
	dpu_write(dpu, SSPP_SRC_YSTRIDE1(i), pitch[2] | pitch[3] << 16);
}

static void
sspp_set_coords(struct dpu_kms *dpu, u8 i, struct drm_rect *dst, struct drm_rect *src)
{
	u32 src_size = (drm_rect_height(src) & 0xffff0000) | drm_rect_width(src) >> 16;

	dpu_write(dpu, SSPP_SRC_SIZE(i), src_size);
	dpu_write(dpu, SSPP_SRC_XY(i), (src->y1 & 0xffff0000) | src->x1 >> 16);
	dpu_write(dpu, SSPP_OUT_SIZE(i),
		  drm_rect_height(dst) << 16 | drm_rect_width(dst));
	dpu_write(dpu, SSPP_OUT_XY(i), dst->y1 << 16 | dst->x1);
	dpu_write(dpu, SSPP_SW_PIX_EXT_C0_LR(i), 0);
	dpu_write(dpu, SSPP_SW_PIX_EXT_C0_TB(i), 0);
	dpu_write(dpu, SSPP_SW_PIX_EXT_C0_REQ_PIXELS(i), src_size);
	dpu_write(dpu, SSPP_SW_PIX_EXT_C1C2_LR(i), 0);
	dpu_write(dpu, SSPP_SW_PIX_EXT_C1C2_TB(i), 0);
	dpu_write(dpu, SSPP_SW_PIX_EXT_C1C2_REQ_PIXELS(i), src_size);
	dpu_write(dpu, SSPP_SW_PIX_EXT_C3_LR(i), 0);
	dpu_write(dpu, SSPP_SW_PIX_EXT_C3_TB(i), 0);
	dpu_write(dpu, SSPP_SW_PIX_EXT_C3_REQ_PIXELS(i), src_size);
}

#define SRC_FORMAT(b, g, r, a) (BIT(17) | (a ? 3 : 2) << 12 | ((b + g + r + a) / 8 - 1) << 9 |  \
	COLOR_##g##BIT | COLOR_##b##BIT << 2 | COLOR_##r##BIT << 4 | COLOR_ALPHA_##a##BIT << 6)

enum {
	COLOR_4BIT,
	COLOR_5BIT,
	COLOR_6BIT,
	COLOR_8BIT,
};

enum {
	COLOR_ALPHA_1BIT,
	COLOR_ALPHA_4BIT,
	COLOR_ALPHA_6BIT,
	COLOR_ALPHA_8BIT,
	COLOR_ALPHA_0BIT = 0, /* for SRC_FORMAT macro only */
};

static void
sspp_set_format(struct dpu_kms *dpu, u8 i, const struct drm_format_info *fmt,
		uint64_t modifier)
{
	u32 src_format, unpack_pattern, op_mode, ubwc_static_ctrl;
	u32 cdp_cntl, danger_lut;
	u64 creq_lut;

	switch (fmt->format >> 16) {
	case DRM_FORMAT_ABGR8888 >> 16:
	default:
		src_format = SRC_FORMAT(8, 8, 8, 8);
		if (fmt->cpp[0] == 3)
			src_format = SRC_FORMAT(8, 8, 8, 0);
		break;
	case DRM_FORMAT_BGR565 >> 16:
		src_format = SRC_FORMAT(5, 6, 5, 0);
		break;
	case DRM_FORMAT_ABGR1555 >> 16:
		src_format = SRC_FORMAT(5, 5, 5, 1);
		break;
	case DRM_FORMAT_ABGR4444 >> 16:
		src_format = SRC_FORMAT(4, 4, 4, 4);
		break;
	}

	switch (fmt->format & 0xffff) {
	case DRM_FORMAT_ABGR8888 & 0xffff:
	case DRM_FORMAT_XBGR8888 & 0xffff:
	case DRM_FORMAT_BGR888   & 0xffff:
	default:
		unpack_pattern = 0x03010002;
		break;
	case DRM_FORMAT_ARGB8888 & 0xffff:
	case DRM_FORMAT_XRGB8888 & 0xffff:
	case DRM_FORMAT_RGB888   & 0xffff:
		unpack_pattern = 0x03020001;
		break;
	case DRM_FORMAT_RGBX8888 & 0xffff:
	case DRM_FORMAT_RGBA8888 & 0xffff:
		unpack_pattern = 0x01000203;
		break;
	case DRM_FORMAT_BGRX8888 & 0xffff:
	case DRM_FORMAT_BGRA8888 & 0xffff:
		unpack_pattern = 0x02000103;
		break;
	}

	op_mode = MDSS_MDP_OP_PE_OVERRIDE;
	cdp_cntl = BIT(0) | BIT(3); /* enable | preload_ahead */
	creq_lut = dpu->creq_lut_linear;
	danger_lut = 0xf;

	if (modifier == DRM_FORMAT_MOD_QCOM_COMPRESSED) {
		op_mode |= MDSS_MDP_OP_BWC_EN;
		src_format |= 2 << 30; /* UBWC fetch */
		unpack_pattern = 0x03010002; /* no swap for UBWC formats */
		cdp_cntl |= BIT(1) | BIT(2); /* ubwc_meta | tile_amortize */
		creq_lut = dpu->creq_lut_macrotile;
		danger_lut = 0xffff;

		if (dpu->ubwc_version == 4)
			ubwc_static_ctrl = BIT(30);
		else if (dpu->ubwc_version == 3)
			ubwc_static_ctrl = BIT(30) | dpu->hbb << 4;
		else
			ubwc_static_ctrl = COND(fmt->has_alpha, BIT(31)) | dpu->hbb << 4;

		dpu_write(dpu, SSPP_FETCH_CONFIG(i), 0x87 | dpu->hbb << 18);
		dpu_write(dpu, SSPP_UBWC_STATIC_CTRL(i), ubwc_static_ctrl);
	}

	dpu_write(dpu, SSPP_SRC_FORMAT(i), src_format);
	dpu_write(dpu, SSPP_SRC_UNPACK_PATTERN(i), unpack_pattern);
	dpu_write(dpu, SSPP_SRC_OP_MODE(i), op_mode);
	dpu_write(dpu, SSPP_CDP_CNTL(i), cdp_cntl);
	dpu_write(dpu, SSPP_CREQ_LUT_0(i), creq_lut);
	dpu_write(dpu, SSPP_CREQ_LUT_1(i), creq_lut >> 32);
	dpu_write(dpu, SSPP_DANGER_LUT(i), danger_lut);
	dpu_write(dpu, SSPP_SAFE_LUT(i), 0);
}

void
dpu_plane_update(struct dpu_kms *dpu, u32 i, struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = state->fb;
	if (!fb)
		return;

	if (i >= 4) i += 12; // DMA sspp starts at 0x20000 offset instead of 0x8000

	sspp_set_framebuffer(dpu, i, fb);
	sspp_set_coords(dpu, i, &state->dst, &state->src);
	sspp_set_format(dpu, i, fb->format, fb->modifier);

	dpu_write(dpu, SSPP_MULTIRECT_OPMODE(i), 0);
	if (i < 4) { /* only VIG planes */
		dpu_write(dpu, QSEED3_OP_MODE(i), BIT(14));
		dpu_write(dpu, SSPP_VIG_CSC_10_OP_MODE(i), 0);
	}
	/* clear previous UBWC error */
	dpu_write(dpu, SSPP_UBWC_ERROR_STATUS(i), BIT(31));
	dpu_write(dpu, SSPP_QOS_CTRL(i), 0); /* set to 0 when plane is off ? */
}

static void
dpu_plane_atomic_update(struct drm_plane *plane,
			struct drm_atomic_state *old_state)
{
	/* there isn't a 1:1 mapping to HW planes, update in crtc update */
}

static const struct drm_plane_funcs dpu_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
	.format_mod_supported   = dpu_format_mod_supported,
};

static const struct drm_plane_helper_funcs dpu_plane_helper_funcs = {
	.prepare_fb = dpu_plane_prepare_fb,
	.cleanup_fb = dpu_plane_cleanup_fb,
	.atomic_check = dpu_plane_atomic_check,
	.atomic_update = dpu_plane_atomic_update,
};

static const uint32_t plane_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_BGRA5551,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_BGRX5551,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ABGR4444,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_BGRA4444,
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_XBGR4444,
	DRM_FORMAT_RGBX4444,
	DRM_FORMAT_BGRX4444,
};

static const uint64_t supported_modifiers[] = {
	DRM_FORMAT_MOD_QCOM_COMPRESSED,
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

int dpu_plane_init(struct dpu_kms *dpu, struct drm_plane *plane)
{
	int ret;

	ret = drm_universal_plane_init(dpu->dev, plane, 0xff, &dpu_plane_funcs,
			plane_formats, ARRAY_SIZE(plane_formats),
			supported_modifiers, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;

	drm_plane_create_zpos_property(plane, 0, 0, dpu->num_blendstages - 1);
	drm_plane_create_rotation_property(plane, DRM_MODE_ROTATE_0, DRM_MODE_ROTATE_0);
	drm_plane_helper_add(plane, &dpu_plane_helper_funcs);

	return 0;
}
