// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2019, The Linux Foundation. All rights reserved.
 */

#include <linux/slab.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/err.h>

#include "dp_hpd.h"
#include "dp_usbpd.h"

struct dp_hpd *dp_hpd_get(struct device *dev, struct dp_hpd_cb *cb)
{
	struct dp_hpd *dp_hpd;

	dp_hpd = dp_usbpd_get(dev, cb);
	if (IS_ERR(dp_hpd)) {
		DP_ERR("failed to get usbpd\n");
		return dp_hpd;
	}
	dp_hpd->type = DP_HPD_USBPD;

	return dp_hpd;
}

void dp_hpd_put(struct dp_hpd *dp_hpd)
{
	if (!dp_hpd)
		return;

	switch (dp_hpd->type) {
	case DP_HPD_USBPD:
		dp_usbpd_put(dp_hpd);
		break;
	default:
		DP_ERR("unknown hpd type %d\n", dp_hpd->type);
		break;
	}
}
