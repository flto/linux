/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_INTF_H
#define _DPU_HW_INTF_H

#include "dpu_hw_catalog.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_util.h"
#include "dpu_hw_blk.h"

struct dpu_hw_intf;

/* intf timing settings */
struct intf_timing_params {
	u32 width;		/* active width */
	u32 height;		/* active height */
	u32 xres;		/* Display panel width */
	u32 yres;		/* Display panel height */

	u32 h_back_porch;
	u32 h_front_porch;
	u32 v_back_porch;
	u32 v_front_porch;
	u32 hsync_pulse_width;
	u32 vsync_pulse_width;
	u32 hsync_polarity;
	u32 vsync_polarity;
	u32 border_clr;
	u32 underflow_clr;
	u32 hsync_skew;
};

struct intf_prog_fetch {
	u8 enable;
	/* vsync counter for the front porch pixel line */
	u32 fetch_start;
};

struct intf_status {
	u8 is_en;		/* interface timing engine is enabled or not */
	u32 frame_count;	/* frame count since timing engine enabled */
	u32 line_count;		/* current line count including blanking */
};

struct dpu_hw_tear_check {
	/*
	 * This is ratio of MDP VSYNC clk freq(Hz) to
	 * refresh rate divided by no of lines
	 */
	u32 vsync_count;
	u32 sync_cfg_height;
	u32 vsync_init_val;
	u32 sync_threshold_start;
	u32 sync_threshold_continue;
	u32 start_pos;
	u32 rd_ptr_irq;
	u8 hw_vsync_mode;
};

struct dpu_hw_autorefresh {
	bool  enable;
	u32 frame_count;
};

struct dpu_hw_pp_vsync_info {
	u32 rd_ptr_init_val;	/* value of rd pointer at vsync edge */
	u32 rd_ptr_frame_count;	/* num frames sent since enabling interface */
	u32 rd_ptr_line_count;	/* current line on panel (rd ptr) */
	u32 wr_ptr_line_count;	/* current line within pp fifo (wr ptr) */
};

/**
 * struct dpu_hw_intf_ops : Interface to the interface Hw driver functions
 *  Assumption is these functions will be called after clocks are enabled
 * @ setup_timing_gen : programs the timing engine
 * @ setup_prog_fetch : enables/disables the programmable fetch logic
 * @ enable_timing: enable/disable timing engine
 * @ get_status: returns if timing engine is enabled or not
 * @ get_line_count: reads current vertical line counter
 */
struct dpu_hw_intf_ops {
	void (*setup_timing_gen)(struct dpu_hw_intf *intf,
			const struct intf_timing_params *p,
			const struct dpu_format *fmt);

	void (*setup_prg_fetch)(struct dpu_hw_intf *intf,
			const struct intf_prog_fetch *fetch);

	void (*enable_timing)(struct dpu_hw_intf *intf,
			u8 enable);

	void (*get_status)(struct dpu_hw_intf *intf,
			struct intf_status *status);

	u32 (*get_line_count)(struct dpu_hw_intf *intf);

	/**
	 * enables vysnc generation and sets up init value of
	 * read pointer and programs the tear check cofiguration
	 */
	int (*setup_tearcheck)(struct dpu_hw_intf *intf,
			struct dpu_hw_tear_check *cfg);

	/**
	 * enables tear check block
	 */
	int (*enable_tearcheck)(struct dpu_hw_intf *intf,
			bool enable);

	/**
	 * read, modify, write to either set or clear listening to external TE
	 * @Return: 1 if TE was originally connected, 0 if not, or -ERROR
	 */
	int (*connect_external_te)(struct dpu_hw_intf *intf,
			bool enable_external_te);

	/**
	 * provides the programmed and current
	 * line_count
	 */
	int (*get_vsync_info)(struct dpu_hw_intf *intf,
			struct dpu_hw_pp_vsync_info  *info);

	/**
	 * configure and enable the autorefresh config
	 */
	int (*setup_autorefresh)(struct dpu_hw_intf *intf,
			struct dpu_hw_autorefresh *cfg);

	/**
	 * retrieve autorefresh config from hardware
	 */
	int (*get_autorefresh)(struct dpu_hw_intf *intf,
			struct dpu_hw_autorefresh *cfg);

	/**
	 * poll until write pointer transmission starts
	 * @Return: 0 on success, -ETIMEDOUT on timeout
	 */
	int (*poll_timeout_wr_ptr)(struct dpu_hw_intf *intf, u32 timeout_us);

	void (*bind_pingpong_blk)(struct dpu_hw_intf *intf,
			bool enable,
			const enum dpu_pingpong pp);
};

struct dpu_hw_intf {
	struct dpu_hw_blk base;
	struct dpu_hw_blk_reg_map hw;

	/* intf */
	enum dpu_intf idx;
	const struct dpu_intf_cfg *cap;
	const struct dpu_mdss_cfg *mdss;

	/* ops */
	struct dpu_hw_intf_ops ops;
};

/**
 * dpu_hw_intf_init(): Initializes the intf driver for the passed
 * interface idx.
 * @idx:  interface index for which driver object is required
 * @addr: mapped register io address of MDP
 * @m :   pointer to mdss catalog data
 */
struct dpu_hw_intf *dpu_hw_intf_init(enum dpu_intf idx,
		void __iomem *addr,
		struct dpu_mdss_cfg *m);

/**
 * dpu_hw_intf_destroy(): Destroys INTF driver context
 * @intf:   Pointer to INTF driver context
 */
void dpu_hw_intf_destroy(struct dpu_hw_intf *intf);

#endif /*_DPU_HW_INTF_H */
