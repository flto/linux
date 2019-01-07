// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2019, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm-dp] %s: " fmt, __func__

#include <linux/slab.h>
#include <linux/device.h>
#include <linux/extcon.h>
#include <drm/drm_print.h>

#include "dp_extcon.h"

/* DP specific VDM commands */
#define DP_USBPD_VDM_STATUS	0x10
#define DP_USBPD_VDM_CONFIGURE	0x11

/* USBPD-TypeC specific Macros */
#define VDM_VERSION		0x0
#define USB_C_DP_SID		0xFF01

struct dp_extcon_private {
	u32 vdo;
	struct device *dev;
	struct notifier_block extcon_nb;
	struct extcon_dev *extcon;
	struct workqueue_struct *extcon_wq;
	struct work_struct event_work;
	struct usbpd *pd;
	struct dp_usbpd_cb *dp_cb;
	struct dp_usbpd dp_usbpd;
};

static int dp_extcon_connect(struct dp_usbpd *dp_usbpd, bool hpd)
{
	int rc = 0;
	struct dp_extcon_private *extcon;

	extcon = container_of(dp_usbpd, struct dp_extcon_private, dp_usbpd);

	dp_usbpd->hpd_high = hpd;
	dp_usbpd->forced_disconnect = !hpd;

	if (hpd)
		extcon->dp_cb->configure(extcon->dev);
	else
		extcon->dp_cb->disconnect(extcon->dev);

	return rc;
}

static int dp_extcon_get_lanes(struct dp_extcon_private *extcon_priv)
{
	union extcon_property_value property;

	extcon_get_property(extcon_priv->extcon,
					EXTCON_DISP_DP,
					EXTCON_PROP_USB_SS,
					&property);
	return ((property.intval) ? 2 : 4);
}


static void dp_extcon_event_work(struct work_struct *work)
{
	struct dp_extcon_private *extcon_priv;
	int dp_state, ret;
	int lanes;
	union extcon_property_value property;

	extcon_priv = container_of(work,
			struct dp_extcon_private, event_work);

	if (!extcon_priv || !extcon_priv->extcon) {
		DRM_ERROR("Invalid extcon device handler\n");
		return;
	}

	dp_state = extcon_get_state(extcon_priv->extcon, EXTCON_DISP_DP);

	if (dp_state > 0) {
		ret = extcon_get_property(extcon_priv->extcon,
					EXTCON_DISP_DP,
					EXTCON_PROP_USB_TYPEC_POLARITY,
					&property);
		if (ret) {
			DRM_ERROR("Get Polarity property failed\n");
			return;
		}
		extcon_priv->dp_usbpd.orientation =
			(property.intval) ? ORIENTATION_CC2 : ORIENTATION_CC1;

		lanes = dp_extcon_get_lanes(extcon_priv);
		if (!lanes)
			return;
		extcon_priv->dp_usbpd.multi_func =
					((lanes == 2) ? false : true);

		if (extcon_priv->dp_cb && extcon_priv->dp_cb->configure) {
			ret = dp_extcon_connect(&extcon_priv->dp_usbpd, true);
			if (ret) {
				DRM_ERROR("extcon_connect->true failed\n");
				return;
			}
		}
	} else {
		if (extcon_priv->dp_cb && extcon_priv->dp_cb->disconnect) {
			ret = dp_extcon_connect(&extcon_priv->dp_usbpd, false);
			if (ret) {
				DRM_ERROR("extcon_connect->false failed\n");
				return;
			}
		}
	}
}

static int dp_extcon_event_notify(struct notifier_block *nb,
				  unsigned long event, void *priv)
{
	struct dp_extcon_private *extcon_priv;

	extcon_priv = container_of(nb, struct dp_extcon_private,
						extcon_nb);

	queue_work(extcon_priv->extcon_wq, &extcon_priv->event_work);
	return NOTIFY_DONE;
}

int dp_extcon_register(struct dp_usbpd *dp_usbpd)
{
	struct dp_extcon_private *extcon_priv;
	int ret = 0;

	if (!dp_usbpd)
		return -EINVAL;

	extcon_priv = container_of(dp_usbpd, struct dp_extcon_private, dp_usbpd);

	extcon_priv->extcon_nb.notifier_call = dp_extcon_event_notify;
	ret = devm_extcon_register_notifier(extcon_priv->dev, extcon_priv->extcon,
					    EXTCON_DISP_DP,
					    &extcon_priv->extcon_nb);
	if (ret) {
		DRM_DEV_ERROR(extcon_priv->dev,
			"register EXTCON_DISP_DP notifier err\n");
		ret = -EINVAL;
		return ret;
	}

	extcon_priv->extcon_wq = create_singlethread_workqueue("drm_dp_extcon");
	if (IS_ERR_OR_NULL(extcon_priv->extcon_wq)) {
		DRM_ERROR("Failed to create workqueue\n");
		dp_extcon_unregister(dp_usbpd);
		return -EPERM;
	}

	INIT_WORK(&extcon_priv->event_work, dp_extcon_event_work);
	return ret;
}

void dp_extcon_unregister(struct dp_usbpd *dp_usbpd)
{
	struct dp_extcon_private *extcon_priv;

	if (!dp_usbpd) {
		DRM_ERROR("Invalid input\n");
		return;
	}

	extcon_priv = container_of(dp_usbpd, struct dp_extcon_private, dp_usbpd);

	devm_extcon_unregister_notifier(extcon_priv->dev, extcon_priv->extcon,
					    EXTCON_DISP_DP,
					    &extcon_priv->extcon_nb);

	if (extcon_priv->extcon_wq)
		destroy_workqueue(extcon_priv->extcon_wq);

	return;
}

struct dp_usbpd *dp_extcon_get(struct device *dev, struct dp_usbpd_cb *cb)
{
	int rc = 0;
	struct dp_extcon_private *dp_extcon;
	struct dp_usbpd *dp_usbpd;

	if (!cb) {
		DRM_ERROR("invalid cb data\n");
		rc = -EINVAL;
		return ERR_PTR(rc);
	}

	dp_extcon = devm_kzalloc(dev, sizeof(*dp_extcon), GFP_KERNEL);
	if (!dp_extcon) {
		rc = -ENOMEM;
		return ERR_PTR(rc);
	}

	dp_extcon->extcon = extcon_get_edev_by_phandle(dev, 0);
	if (!dp_extcon->extcon) {
		DRM_ERROR("invalid extcon data\n");
		rc = -EINVAL;
		devm_kfree(dev, dp_extcon);
		return ERR_PTR(rc);
        }

	dp_extcon->dev = dev;
	dp_extcon->dp_cb = cb;

	dp_extcon->dp_usbpd.connect = dp_extcon_connect;
	dp_usbpd = &dp_extcon->dp_usbpd;

	return dp_usbpd;
}

void dp_extcon_put(struct dp_usbpd *dp_usbpd)
{
	struct dp_extcon_private *extcon;

	if (!dp_usbpd)
		return;

	extcon = container_of(dp_usbpd, struct dp_extcon_private, dp_usbpd);

	devm_kfree(extcon->dev, extcon);
}
