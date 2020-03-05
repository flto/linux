// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm-dp] %s: " fmt, __func__

#include <linux/slab.h>
#include <linux/device.h>

#include "dp_hpd.h"

/* DP specific VDM commands */
#define DP_USBPD_VDM_STATUS	0x10
#define DP_USBPD_VDM_CONFIGURE	0x11

/* USBPD-TypeC specific Macros */
#define VDM_VERSION		0x0
#define USB_C_DP_SID		0xFF01

struct dp_hpd_private {
	struct device *dev;
	struct dp_usbpd_cb *dp_cb;
	struct dp_usbpd dp_usbpd;
};

static int dp_hpd_connect(struct dp_usbpd *dp_usbpd, bool hpd)
{
	int rc = 0;
	struct dp_hpd_private *hpd_priv;

	hpd_priv = container_of(dp_usbpd, struct dp_hpd_private,
					dp_usbpd);

	dp_usbpd->hpd_high = hpd;

	if (!hpd_priv->dp_cb && !hpd_priv->dp_cb->configure
				&& !hpd_priv->dp_cb->disconnect) {
		pr_err("hpd dp_cb not initialized\n");
		return -EINVAL;
	}
	if (hpd)
		hpd_priv->dp_cb->configure(hpd_priv->dev);
	else
		hpd_priv->dp_cb->disconnect(hpd_priv->dev);

	return rc;
}

struct dp_usbpd *dp_hpd_get(struct device *dev, struct dp_usbpd_cb *cb)
{
	int rc = 0;
	struct dp_hpd_private *dp_hpd;
	struct dp_usbpd *dp_usbpd;

	if (!cb) {
		pr_err("invalid cb data\n");
		rc = -EINVAL;
		return ERR_PTR(rc);
	}

	dp_hpd = devm_kzalloc(dev, sizeof(*dp_hpd), GFP_KERNEL);
	if (!dp_hpd) {
		rc = -ENOMEM;
		return ERR_PTR(rc);
	}

	dp_hpd->dev = dev;
	dp_hpd->dp_cb = cb;

	dp_hpd->dp_usbpd.connect = dp_hpd_connect;
	dp_usbpd = &dp_hpd->dp_usbpd;

	return dp_usbpd;
}

void dp_hpd_put(struct dp_usbpd *dp_usbpd)
{
	struct dp_hpd_private *hpd;

	if (!dp_usbpd)
		return;

	hpd = container_of(dp_usbpd, struct dp_hpd_private, dp_usbpd);

	devm_kfree(hpd->dev, hpd);
}
