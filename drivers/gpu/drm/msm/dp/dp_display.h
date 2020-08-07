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
#include "dp_ctrl.h"
#include "dp_display.h"

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

#define DP_INTERRUPT_STATUS_ACK_SHIFT	1
#define DP_INTERRUPT_STATUS_MASK_SHIFT	2

#define DP_INTERRUPT_STATUS1 \
	(DP_INTR_AUX_I2C_DONE| \
	DP_INTR_WRONG_ADDR | DP_INTR_TIMEOUT | \
	DP_INTR_NACK_DEFER | DP_INTR_WRONG_DATA_CNT | \
	DP_INTR_I2C_NACK | DP_INTR_I2C_DEFER | \
	DP_INTR_PLL_UNLOCKED | DP_INTR_AUX_ERROR)

#define DP_INTERRUPT_STATUS1_ACK \
	(DP_INTERRUPT_STATUS1 << DP_INTERRUPT_STATUS_ACK_SHIFT)
#define DP_INTERRUPT_STATUS1_MASK \
	(DP_INTERRUPT_STATUS1 << DP_INTERRUPT_STATUS_MASK_SHIFT)

#define DP_INTERRUPT_STATUS2 \
	(DP_INTR_READY_FOR_VIDEO | DP_INTR_IDLE_PATTERN_SENT | \
	DP_INTR_FRAME_END | DP_INTR_CRC_UPDATED)

#define DP_INTERRUPT_STATUS2_ACK \
	(DP_INTERRUPT_STATUS2 << DP_INTERRUPT_STATUS_ACK_SHIFT)
#define DP_INTERRUPT_STATUS2_MASK \
	(DP_INTERRUPT_STATUS2 << DP_INTERRUPT_STATUS_MASK_SHIFT)

enum {
	BPC_6,
	BPC_8,
	BPC_10,
};

struct msm_dp {
	struct platform_device *pdev;
	void __iomem *base;
	int irq;
	struct clk_bulk_data clk_core[3], clk_ctrl[3];

	struct drm_connector *connector;
	bool is_connected;
	bool hpd_high; // XXX duplicate of is_connected

	struct phy *phy;
	union phy_configure_opts phy_opts;
	bool phy_init;

	struct drm_dp_aux aux;

	struct dp_ctrl ctrl;
	bool ctrl_on;

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
void dp_aux_isr(struct msm_dp *dp);

#endif /* _DP_DISPLAY_H_ */
