/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_CTRL_H_
#define _DP_CTRL_H_

#include <linux/clk.h>

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

struct dp_ctrl {
	struct completion idle_comp;
	struct mutex push_idle_mutex;
	struct completion video_comp;

	struct drm_dp_aux *aux;
};

int dp_ctrl_host_init(struct dp_ctrl *dp_ctrl);
void dp_ctrl_host_deinit(struct dp_ctrl *dp_ctrl);
int dp_ctrl_on(struct dp_ctrl *dp_ctrl);
int dp_ctrl_off(struct dp_ctrl *dp_ctrl);
void dp_ctrl_push_idle(struct dp_ctrl *dp_ctrl);
void dp_ctrl_isr(struct dp_ctrl *dp_ctrl);
int dp_ctrl_link_maintenance(struct dp_ctrl *ctrl);

static inline void
dp_ctrl_get(struct dp_ctrl *ctrl, struct drm_dp_aux *aux)
{
	init_completion(&ctrl->idle_comp);
	init_completion(&ctrl->video_comp);
	mutex_init(&ctrl->push_idle_mutex);
	ctrl->aux = aux;
}

#endif /* _DP_CTRL_H_ */
