/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_CATALOG_H_
#define _DP_CATALOG_H_

#include "dp_parser.h"

/* interrupts */
#define DP_INTR_HPD		BIT(0)
#define DP_INTR_AUX_I2C_DONE	BIT(3)
#define DP_INTR_WRONG_ADDR	BIT(6)
#define DP_INTR_TIMEOUT		BIT(9)
#define DP_INTR_NACK_DEFER	BIT(12)
#define DP_INTR_WRONG_DATA_CNT	BIT(15)
#define DP_INTR_I2C_NACK	BIT(18)
#define DP_INTR_I2C_DEFER	BIT(21)
#define DP_INTR_PLL_UNLOCKED	BIT(24)
#define DP_INTR_AUX_ERROR	BIT(27)

#define DP_INTR_READY_FOR_VIDEO		BIT(0)
#define DP_INTR_IDLE_PATTERN_SENT	BIT(3)
#define DP_INTR_FRAME_END		BIT(6)
#define DP_INTR_CRC_UPDATED		BIT(9)

struct dp_catalog_aux {
	u32 data;
	u32 isr;
};

struct dp_catalog_ctrl {
	u32 dp_tu;
	u32 valid_boundary;
	u32 valid_boundary2;
	u32 isr;
};

struct dp_catalog_panel {
	u32 total;
	u32 sync_start;
	u32 width_blanking;
	u32 dp_active;

	/* TPG */
	u32 hsync_period;
	u32 vsync_period;
	u32 display_v_start;
	u32 display_v_end;
	u32 v_sync_width;
	u32 hsync_ctl;
	u32 display_hctl;
};

struct dp_catalog {
	struct dp_catalog_aux aux;
	struct dp_catalog_ctrl ctrl;
	struct dp_catalog_panel panel;
};

/* AUX APIs */
u32 dp_catalog_aux_read_data(struct dp_catalog_aux *aux);
int dp_catalog_aux_write_data(struct dp_catalog_aux *aux);
int dp_catalog_aux_write_trans(struct dp_catalog_aux *aux);
int dp_catalog_aux_clear_trans(struct dp_catalog_aux *aux, bool read);
void dp_catalog_aux_reset(struct dp_catalog_aux *aux);
void dp_catalog_aux_enable(struct dp_catalog_aux *aux, bool enable);
void dp_catalog_aux_update_cfg(struct dp_catalog_aux *aux,
		struct dp_aux_cfg *cfg, enum dp_phy_aux_config_type type);
void dp_catalog_aux_setup(struct dp_catalog_aux *aux,
		struct dp_aux_cfg *aux_cfg);
void dp_catalog_aux_get_irq(struct dp_catalog_aux *aux, bool cmd_busy);

/* DP Controller APIs */
void dp_catalog_ctrl_state_ctrl(struct dp_catalog_ctrl *ctrl, u32 state);
void dp_catalog_ctrl_config_ctrl(struct dp_catalog_ctrl *ctrl, u32 config);
void dp_catalog_ctrl_lane_mapping(struct dp_catalog_ctrl *ctrl);
void dp_catalog_ctrl_mainlink_ctrl(struct dp_catalog_ctrl *ctrl, bool enable);
void dp_catalog_ctrl_config_misc(struct dp_catalog_ctrl *ctrl, u32 cc, u32 tb);
void dp_catalog_ctrl_config_msa(struct dp_catalog_ctrl *ctrl, u32 rate,
				u32 stream_rate_khz, bool fixed_nvid);
int dp_catalog_ctrl_set_pattern(struct dp_catalog_ctrl *ctrl, u32 pattern);
void dp_catalog_ctrl_reset(struct dp_catalog_ctrl *ctrl);
void dp_catalog_ctrl_usb_reset(struct dp_catalog_ctrl *ctrl, bool flip);
bool dp_catalog_ctrl_mainlink_ready(struct dp_catalog_ctrl *ctrl);
void dp_catalog_ctrl_enable_irq(struct dp_catalog_ctrl *ctrl, bool enable);
void dp_catalog_ctrl_hpd_config(struct dp_catalog_ctrl *ctrl, bool enable);
void dp_catalog_ctrl_phy_reset(struct dp_catalog_ctrl *ctrl);
void dp_catalog_ctrl_phy_lane_cfg(struct dp_catalog_ctrl *ctrl, bool flipped,
				u8 lane_cnt);
int dp_catalog_ctrl_update_vx_px(struct dp_catalog_ctrl *ctrl, u8 v_level,
				u8 p_level);
void dp_catalog_ctrl_get_interrupt(struct dp_catalog_ctrl *ctrl);
void dp_catalog_ctrl_update_transfer_unit(struct dp_catalog_ctrl *ctrl);
void dp_catalog_ctrl_send_phy_pattern(struct dp_catalog_ctrl *ctrl,
			u32 pattern);
u32 dp_catalog_ctrl_read_phy_pattern(struct dp_catalog_ctrl *ctrl);

/* DP Panel APIs */
int dp_catalog_panel_timing_cfg(struct dp_catalog_panel *panel);
void dp_catalog_panel_tpg_enable(struct dp_catalog_panel *panel);
void dp_catalog_panel_tpg_disable(struct dp_catalog_panel *panel);

struct dp_catalog *dp_catalog_get(struct device *dev, struct dp_io *io);
void dp_catalog_put(struct dp_catalog *catalog);

#endif /* _DP_CATALOG_H_ */
