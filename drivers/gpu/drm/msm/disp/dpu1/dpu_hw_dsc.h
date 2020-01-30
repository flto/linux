/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2020, Linaro Limited */

#ifndef _DPU_HW_DSC_H
#define _DPU_HW_DSC_H

#include <drm/drm_dsc.h>

#define DSC_MODE_SPLIT_PANEL            BIT(0)
#define DSC_MODE_MULTIPLEX              BIT(1)
#define DSC_MODE_VIDEO                  BIT(2)

struct dpu_hw_dsc;

/**
 * struct dpu_dsc_config - defines dsc configuration
 * @version:                 DSC version.
 * @scr_rev:                 DSC revision.
 * @pic_height:              Picture height in pixels.
 * @pic_width:               Picture width in pixels.
 * @initial_lines:           Number of initial lines stored in encoder.
 * @pkt_per_line:            Number of packets per line.
 * @bytes_in_slice:          Number of bytes in slice.
 * @eol_byte_num:            Valid bytes at the end of line.
 * @pclk_per_line:           Compressed width.
 * @full_frame_slices:       Number of slice per interface.
 * @slice_height:            Slice height in pixels.
 * @slice_width:             Slice width in pixels.
 * @chunk_size:              Chunk size in bytes for slice multiplexing.
 * @slice_last_group_size:   Size of last group in pixels.
 * @bpp:                     Target bits per pixel.
 * @bpc:                     Number of bits per component.
 * @line_buf_depth:          Line buffer bit depth.
 * @block_pred_enable:       Block prediction enabled/disabled.
 * @vbr_enable:              VBR mode.
 * @enable_422:              Indicates if input uses 4:2:2 sampling.
 * @convert_rgb:             DSC color space conversion.
 * @input_10_bits:           10 bit per component input.
 * @slice_per_pkt:           Number of slices per packet.
 * @initial_dec_delay:       Initial decoding delay.
 * @initial_xmit_delay:      Initial transmission delay.
 * @initial_scale_value:     Scale factor value at the beginning of a slice.
 * @scale_decrement_interval: Scale set up at the beginning of a slice.
 * @scale_increment_interval: Scale set up at the end of a slice.
 * @first_line_bpg_offset:   Extra bits allocated on the first line of a slice.
 * @nfl_bpg_offset:          Slice specific settings.
 * @slice_bpg_offset:        Slice specific settings.
 * @initial_offset:          Initial offset at the start of a slice.
 * @final_offset:            Maximum end-of-slice value.
 * @rc_model_size:           Number of bits in RC model.
 * @det_thresh_flatness:     Flatness threshold.
 * @max_qp_flatness:         Maximum QP for flatness adjustment.
 * @min_qp_flatness:         Minimum QP for flatness adjustment.
 * @edge_factor:             Ratio to detect presence of edge.
 * @quant_incr_limit0:       QP threshold.
 * @quant_incr_limit1:       QP threshold.
 * @tgt_offset_hi:           Upper end of variability range.
 * @tgt_offset_lo:           Lower end of variability range.
 * @buf_thresh:              Thresholds in RC model
 * @range_min_qp:            Min QP allowed.
 * @range_max_qp:            Max QP allowed.
 * @range_bpg_offset:        Bits per group adjustment.
 */
struct dpu_dsc_config {
	u8 version;
	u8 scr_rev;

	int pic_height;
	int pic_width;
	int slice_height;
	int slice_width;

	int initial_lines;
	int pkt_per_line;
	int bytes_in_slice;
	int bytes_per_pkt;
	int eol_byte_num;
	int pclk_per_line;
	int full_frame_slices;
	int slice_last_group_size;
	int bpp;
	int bpc;
	int line_buf_depth;

	int slice_per_pkt;
	int chunk_size;
	bool block_pred_enable;
	int vbr_enable;
	int enable_422;
	int convert_rgb;
	int input_10_bits;

	int initial_dec_delay;
	int initial_xmit_delay;
	int initial_scale_value;
	int scale_decrement_interval;
	int scale_increment_interval;
	int first_line_bpg_offset;
	int nfl_bpg_offset;
	int slice_bpg_offset;
	int initial_offset;
	int final_offset;

	int rc_model_size;
	int det_thresh_flatness;
	int max_qp_flatness;
	int min_qp_flatness;
	int edge_factor;
	int quant_incr_limit0;
	int quant_incr_limit1;
	int tgt_offset_hi;
	int tgt_offset_lo;

	u32 *buf_thresh;
	char *range_min_qp;
	char *range_max_qp;
	char *range_bpg_offset;
};

/**
 * struct dpu_hw_dsc_ops - interface to the dsc hardware driver functions
 * Assumption is these functions will be called after clocks are enabled
 */
struct dpu_hw_dsc_ops {
	/**
	 * dsc_disable - disable dsc
	 * @hw_dsc: Pointer to dsc context
	 */
	void (*dsc_disable)(struct dpu_hw_dsc *hw_dsc);

	/**
	 * dsc_config - configures dsc encoder
	 * @hw_dsc: Pointer to dsc context
	 * @dsc: panel dsc parameters
	 * @mode: dsc topology mode to be set
	 * @ich_reset_override: option to reset ich
	 */
	void (*dsc_config)(struct dpu_hw_dsc *hw_dsc,
			   struct dpu_dsc_config *dsc,
			   u32 mode, bool ich_reset_override);

	/**
	 * dsc_config_thresh - programs panel thresholds
	 * @hw_dsc: Pointer to dsc context
	 * @dsc: panel dsc parameters
	 */
	void (*dsc_config_thresh)(struct dpu_hw_dsc *hw_dsc,
				  struct dpu_dsc_config *dsc);
};

struct dpu_hw_dsc {
	struct dpu_hw_blk base;
	struct dpu_hw_blk_reg_map hw;

	/* dsc */
	enum dpu_dsc idx;
	const struct dpu_dsc_cfg *caps;

	/* ops */
	struct dpu_hw_dsc_ops ops;
};

/**
 * dpu_hw_dsc_init - initializes the dsc block for the passed dsc idx.
 * @idx:  DSC index for which driver object is required
 * @addr: Mapped register io address of MDP
 * @m:    Pointer to mdss catalog data
 * Returns: Error code or allocated dpu_hw_dsc context
 */
struct dpu_hw_dsc *dpu_hw_dsc_init(enum dpu_dsc idx, void __iomem *addr,
				   struct dpu_mdss_cfg *m);

/**
 * dpu_hw_dsc_destroy - destroys dsc driver context
 * @dsc:   Pointer to dsc driver context returned by dpu_hw_dsc_init
 */
void dpu_hw_dsc_destroy(struct dpu_hw_dsc *dsc);

#endif /* _DPU_HW_DSC_H */
