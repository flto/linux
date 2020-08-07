/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_DISPLAY_H_
#define _DP_DISPLAY_H_

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/component.h>
#include <linux/of_irq.h>
#include <linux/usb/typec_mux.h>

#include <linux/phy/phy.h>
#include <linux/phy/phy-dp.h>

#include "msm_drv.h"
#include "msm_kms.h"
#include "dp.xml.h"
#include "dp_display.h"

enum {
	BPC_6,
	BPC_8,
	BPC_10,
};

struct msm_dp {
	struct platform_device *pdev, *audio_pdev;
	void __iomem *base;
	int irq;
	struct clk_bulk_data clk_core[3], clk_ctrl[2];
	struct clk *pixel_clk;

	struct drm_connector connector;
	bool enabled, connected;
	bool ctrl_on;

	struct mutex dp_mutex;

	struct phy *phy;
	union phy_configure_opts phy_opts;

	struct drm_dp_aux aux;

	/* "panel" */
	u8 dpcd[DP_RECEIVER_CAP_SIZE + 1];
	struct drm_dp_desc desc;
	struct drm_display_mode mode;
	unsigned mode_bpc;
	struct edid *edid;

	struct typec_mux *typec_mux;

	/* "aux */
	struct mutex mutex;
	struct completion comp;

	u32 aux_error_num;
	u32 retry_cnt;
	bool cmd_busy;
	bool native;
	bool read;
	bool no_send_addr;
	bool no_send_stop;
	u32 offset;
	u32 segment;
};

#define DP_AUX_ERR_NONE		0
#define DP_AUX_ERR_ADDR		-1
#define DP_AUX_ERR_TOUT		-2
#define DP_AUX_ERR_NACK		-3
#define DP_AUX_ERR_DEFER	-4
#define DP_AUX_ERR_NACK_DEFER	-5

#define DP_AUX_CFG_MAX_VALUE_CNT 3
#define PHY_AUX_CFG_MAX 10

int dp_aux_register(struct msm_dp *dp, struct device *dev);
void dp_aux_unregister(struct msm_dp *dp);

int dp_ctrl_on(struct msm_dp *dp);
int dp_ctrl_off(struct msm_dp *dp);

static inline u32
dp_read(struct msm_dp *dp, u32 reg)
{
	return readl_relaxed(dp->base + reg);
}

static inline void
dp_write(struct msm_dp *dp, u32 reg, u32 data)
{
	writel(data, dp->base + reg);
}

void dp_audio_init(struct msm_dp *dp);
void dp_audio_setup(struct msm_dp *dp);

#endif /* _DP_DISPLAY_H_ */
