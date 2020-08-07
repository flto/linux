// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 */

#define HPD_STRING_SIZE 30

#include <linux/usb/typec_dp.h>

#include "dp_display.h"

int msm_dp_modeset_init(struct msm_dp *dp, struct drm_device *dev, struct drm_encoder *encoder)
{
	drm_connector_attach_encoder(&dp->connector, encoder);
	return 0;
}

void msm_dp_display_mode_set(struct msm_dp *dp, struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	/* TODO: check dp->connector->display_info.bpc to find bpc */
	dp->mode_bpc = BPC_8;

	mutex_lock(&dp->dp_mutex);

	/* defer setting mode to when connected */
	if (!dp->connected) {
		drm_mode_copy(&dp->mode, adjusted_mode);
		mutex_unlock(&dp->dp_mutex);
		return;
	}

	if (dp->ctrl_on) {
		dp_ctrl_off(dp);

		/* let ctrl be off for a bit to "reset" (for index)
		 * .. this is probably papering over a real problem
		 * (index ends up disconnecting and reconnecting)
		 */
		msleep(500);
	}

	drm_mode_copy(&dp->mode, adjusted_mode);

	dp_ctrl_on(dp);

	mutex_unlock(&dp->dp_mutex);
}

/* status1 */
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

#define DP_INTERRUPT_STATUS1 \
	(DP_INTR_AUX_I2C_DONE| \
	DP_INTR_WRONG_ADDR | DP_INTR_TIMEOUT | \
	DP_INTR_NACK_DEFER | DP_INTR_WRONG_DATA_CNT | \
	DP_INTR_I2C_NACK | DP_INTR_I2C_DEFER | \
	DP_INTR_PLL_UNLOCKED | DP_INTR_AUX_ERROR)

/* status2 */
#define DP_INTR_READY_FOR_VIDEO		BIT(0)
#define DP_INTR_IDLE_PATTERN_SENT	BIT(3)
#define DP_INTR_FRAME_END		BIT(6)
#define DP_INTR_CRC_UPDATED		BIT(9)

#define DP_INTERRUPT_STATUS2 \
	(DP_INTR_READY_FOR_VIDEO | DP_INTR_IDLE_PATTERN_SENT | \
	DP_INTR_FRAME_END | DP_INTR_CRC_UPDATED)

static irqreturn_t dp_display_irq(int irq, void *dev_id)
{
	struct msm_dp *dp = dev_id;
	u32 status1, status2;

	/* 3 bits per irq - status, ack, mask - set ack while preserving mask */

	status1 = dp_read(dp, REG_DP_INTR_STATUS);
	dp_write(dp, REG_DP_INTR_STATUS,
		(status1 & DP_INTERRUPT_STATUS1) << 1 | DP_INTERRUPT_STATUS1 << 2);

	status2 = dp_read(dp, REG_DP_INTR_STATUS2);
	dp_write(dp, REG_DP_INTR_STATUS2,
		(status2 & DP_INTERRUPT_STATUS2) << 1 | DP_INTERRUPT_STATUS1 << 2);

	if (!dp->cmd_busy)
		return IRQ_HANDLED;

	if (dp->native) {
		if (status1 & DP_INTR_AUX_I2C_DONE)
			dp->aux_error_num = DP_AUX_ERR_NONE;
		else if (status1 & DP_INTR_WRONG_ADDR)
			dp->aux_error_num = DP_AUX_ERR_ADDR;
		else if (status1 & DP_INTR_TIMEOUT)
			dp->aux_error_num = DP_AUX_ERR_TOUT;
		if (status1 & DP_INTR_NACK_DEFER)
			dp->aux_error_num = DP_AUX_ERR_NACK;

		complete(&dp->comp);
		return IRQ_HANDLED;
	}

	if (status1 & DP_INTR_AUX_I2C_DONE) {
		if (status1 & (DP_INTR_I2C_NACK | DP_INTR_I2C_DEFER))
			dp->aux_error_num = DP_AUX_ERR_NACK;
		else
			dp->aux_error_num = DP_AUX_ERR_NONE;
	} else {
		if (status1 & DP_INTR_WRONG_ADDR)
			dp->aux_error_num = DP_AUX_ERR_ADDR;
		else if (status1 & DP_INTR_TIMEOUT)
			dp->aux_error_num = DP_AUX_ERR_TOUT;
		if (status1 & DP_INTR_NACK_DEFER)
			dp->aux_error_num = DP_AUX_ERR_NACK_DEFER;
		if (status1 & DP_INTR_I2C_NACK)
			dp->aux_error_num = DP_AUX_ERR_NACK;
		if (status1 & DP_INTR_I2C_DEFER)
			dp->aux_error_num = DP_AUX_ERR_DEFER;
	}
	complete(&dp->comp);

	return IRQ_HANDLED;
}

static enum drm_connector_status
dp_connector_detect(struct drm_connector *connector, bool force)
{
	return container_of(connector, struct msm_dp, connector)->connected ?
		connector_status_connected : connector_status_disconnected;
}

static int
dp_connector_get_modes(struct drm_connector *connector)
{
	struct msm_dp *dp = container_of(connector, struct msm_dp, connector);
	int ret;

	if (!dp->connected || !dp->edid)
		return 0;

	// XXX shouldn't be here ?
	ret = drm_connector_update_edid_property(&dp->connector, dp->edid);
	if (ret)
		return ret;

	return drm_add_edid_modes(&dp->connector, dp->edid);
}

static enum drm_mode_status
dp_connector_mode_valid(struct drm_connector *connector, struct drm_display_mode *mode)
{
	return MODE_OK; /* TODO */
}

static const struct drm_connector_funcs dp_connector_funcs = {
	.detect = dp_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs dp_connector_helper_funcs = {
	.get_modes = dp_connector_get_modes,
	.mode_valid = dp_connector_mode_valid,
};

/* from intel_dp.c: */
static void
msm_dp_extended_receiver_capabilities(struct msm_dp *dp)
{
	u8 dpcd_ext[6];

	/*
	 * Prior to DP1.3 the bit represented by
	 * DP_EXTENDED_RECEIVER_CAP_FIELD_PRESENT was reserved.
	 * if it is set DP_DPCD_REV at 0000h could be at a value less than
	 * the true capability of the panel. The only way to check is to
	 * then compare 0000h and 2200h.
	 */
	if (!(dp->dpcd[DP_TRAINING_AUX_RD_INTERVAL] & DP_EXTENDED_RECEIVER_CAP_FIELD_PRESENT))
		return;

	if (drm_dp_dpcd_read(&dp->aux, DP_DP13_DPCD_REV,
			     &dpcd_ext, sizeof(dpcd_ext)) != sizeof(dpcd_ext)) {
		DRM_ERROR("DPCD failed read at extended capabilities\n");
		return;
	}

	if (dp->dpcd[DP_DPCD_REV] > dpcd_ext[DP_DPCD_REV]) {
		DRM_DEBUG_DP("DPCD extended DPCD rev less than base DPCD rev\n");
		return;
	}

	if (!memcmp(dp->dpcd, dpcd_ext, sizeof(dpcd_ext)))
		return;

	DRM_DEBUG_DP("Base DPCD: %*ph\n", (int)sizeof(dp->dpcd), dp->dpcd);

	memcpy(dp->dpcd, dpcd_ext, sizeof(dpcd_ext));
}

static bool
msm_dp_read_dpcd(struct msm_dp *dp)
{
	if (drm_dp_dpcd_read(&dp->aux, 0x000, dp->dpcd, sizeof(dp->dpcd)) < 0)
		return false; /* aux transfer failed */

	msm_dp_extended_receiver_capabilities(dp);

	DRM_DEBUG_DP("DPCD: %*ph\n", (int)sizeof(dp->dpcd), dp->dpcd);

	return dp->dpcd[DP_DPCD_REV] != 0;
}

static int
dp_panel_read_sink_caps(struct msm_dp *dp)
{
	int rc = 0;

	if (!msm_dp_read_dpcd(dp)) {
		DRM_ERROR("read dpcd failed\n");
		return rc;
	}

	dp->phy_opts.dp.link_rate = drm_dp_bw_code_to_link_rate(dp->dpcd[DP_MAX_LINK_RATE]) / 100;
	dp->phy_opts.dp.lanes = dp->dpcd[DP_MAX_LANE_COUNT] & DP_MAX_LANE_COUNT_MASK;

	/* Limit support upto HBR2 until HBR3 support is added */
	if (dp->phy_opts.dp.link_rate >= 5400)
		dp->phy_opts.dp.link_rate = 5400;

	rc = drm_dp_read_desc(&dp->aux, &dp->desc, drm_dp_is_branch(dp->dpcd));
	if (rc) {
		DRM_ERROR("read sink/branch descriptor failed %d\n", rc);
		return rc;
	}

	kfree(dp->edid);
	dp->edid = NULL;

	dp->edid = drm_get_edid(&dp->connector, &dp->aux.ddc);
	if (!dp->edid) {
		DRM_ERROR("panel edid read failed\n");
		return -EINVAL;
	}

	// dp->audio_supported = drm_detect_monitor_audio(dp->edid);

	return 0;
}

extern int _orientation_hack;

static int
dp_typec_mux_set(struct typec_mux *mux, struct typec_mux_state *state)
{
	struct msm_dp *dp = typec_mux_get_drvdata(mux);
	struct typec_displayport_data *data = state->data;
	u8 link_status[DP_LINK_STATUS_SIZE], updated;
	int ret;
	bool dp_enabled = false, dp_connected = false;

	/* also: state->mode ? */
	if (data && state->alt && state->alt->svid == USB_TYPEC_DP_SID && state->alt->active) {
		dp_enabled = true;
		dp_connected = (data->status & DP_STATUS_HPD_STATE);
		_orientation_hack =
			typec_altmode_get_orientation(state->alt) == TYPEC_ORIENTATION_REVERSE;
	}

	//printk("dp_typec_mux_set: %d %d %ld\n", dp_enabled, dp_connected,
	//       (data ? (data->status & DP_STATUS_IRQ_HPD) : -1));

	mutex_lock(&dp->dp_mutex);

	/* disconnecting, disable link clocks */
	if (!dp_connected && dp->connected) {
		if (dp->ctrl_on)
			dp_ctrl_off(dp);
	}

	/* init/exit phy based on presence of alt mode */
	if (dp_enabled != dp->enabled) {
		if (dp_enabled)
			phy_init(dp->phy);
		else
			phy_exit(dp->phy);

		dp->enabled = dp_enabled;
	}

	// TODO: (data->status & DP_STATUS_IRQ_HPD)

	bool hpd_irq = (data && (data->status & DP_STATUS_IRQ_HPD));
	if (dp->connected == dp_connected && !hpd_irq)
		goto unlock;

	dp->connected = dp_connected;
	if (!dp->connected) {
		drm_helper_hpd_irq_event(dp->connector.dev);
		goto unlock;
	}

	ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
	if (ret < DP_LINK_STATUS_SIZE) {
		DRM_ERROR("DP link status read failed\n");
		goto unlock;
	}

	/* TODO: read DP_DEVICE_SERVICE_IRQ_VECTOR for automated tests */

	updated = link_status[DP_LANE_ALIGN_STATUS_UPDATED - DP_LANE0_1_STATUS];

	bool need_retrain = false;

	if (!drm_dp_clock_recovery_ok(link_status, dp->phy_opts.dp.lanes) ||
	    !drm_dp_channel_eq_ok(link_status, dp->phy_opts.dp.lanes) ||
	    (updated & DP_LINK_STATUS_UPDATED))
		need_retrain = true;

	if ((updated & DP_DOWNSTREAM_PORT_STATUS_CHANGED) || !hpd_irq) {
		/* TODO: read DP_SINK_COUNT, which should always be 1 */

		dp_panel_read_sink_caps(dp);
		drm_helper_hpd_irq_event(dp->connector.dev);

		need_retrain = true;
	}

	if (need_retrain) {
		if (dp->ctrl_on)
			dp_ctrl_off(dp);

		if (dp->mode.clock)
			dp_ctrl_on(dp);
	}
unlock:
	mutex_unlock(&dp->dp_mutex);

	return 0;
}

static int dp_display_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = dev_get_drvdata(master);
	struct msm_drm_private *priv = drm->dev_private;
	struct platform_device *pdev = to_platform_device(dev);
	struct typec_mux_desc mux_desc = { };
	struct resource *res;
	struct msm_dp *dp;
	int ret;

	dp = devm_kzalloc(dev, sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;

	mux_desc.fwnode = dev->fwnode;
	mux_desc.drvdata = dp;
	mux_desc.set = dp_typec_mux_set;
	dp->typec_mux = typec_mux_register(dev, &mux_desc);
	if (IS_ERR(dp->typec_mux))
		return PTR_ERR(dp->typec_mux);

	dp->pdev = pdev;

	platform_set_drvdata(pdev, dp);

	priv->dp = dp;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	dp->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!dp->base)
		return -EIO;

	dp->clk_core[0].id = "core_aux_clk";
	dp->clk_core[1].id = "core_ref_clk_src";
	dp->clk_core[2].id = "bus_clk";

	ret = devm_clk_bulk_get(dev, ARRAY_SIZE(dp->clk_core), dp->clk_core);
	if (ret) {
		DRM_ERROR("failed to get DP_CORE_PM clk. err=%d\n", ret);
		return ret;
	}

	dp->clk_ctrl[0].id = "ctrl_link_clk";
	dp->clk_ctrl[1].id = "ctrl_link_iface_clk";

	ret = devm_clk_bulk_get(dev, ARRAY_SIZE(dp->clk_ctrl), dp->clk_ctrl);
	if (ret) {
		DRM_ERROR("failed to get DP_CTRL_PM clk. err=%d\n", ret);
		return -ENODEV;
	}

	dp->pixel_clk = devm_clk_get(dev, "ctrl_pixel_clk");
	if (IS_ERR(dp->pixel_clk)) {
		DRM_DEV_ERROR(dev, "failed to get pixel clk\n");
		return PTR_ERR(dp->pixel_clk);
	}

	dp->phy = devm_phy_get(dev, "dp");
	if (IS_ERR(dp->phy)) {
		DRM_DEV_ERROR(dev, "failed to get DP phy\n");
		return PTR_ERR(dp->phy);
	}

	ret = drm_connector_init(drm, &dp->connector,
			&dp_connector_funcs,
			DRM_MODE_CONNECTOR_DisplayPort);
	if (ret) {
		DRM_DEV_ERROR(dev, "drm_connector_init failed\n");
		return ret;
	}

	drm_connector_helper_add(&dp->connector, &dp_connector_helper_funcs);
	dp->connector.polled = DRM_CONNECTOR_POLL_HPD;

	priv->connectors[priv->num_connectors++] = &dp->connector;

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(dp->clk_core), dp->clk_core);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to enable core clks: %d\n", ret);
		return ret;
	}

	/* enable ctrl irqs */
	dp_write(dp, REG_DP_INTR_STATUS, DP_INTERRUPT_STATUS1 << 2);
	dp_write(dp, REG_DP_INTR_STATUS2, DP_INTERRUPT_STATUS2 << 2);
	dp_aux_register(dp, dev);

	mutex_init(&dp->dp_mutex);

	dp->irq = irq_of_parse_and_map(dev->of_node, 0);
	if (dp->irq < 0) {
		ret = dp->irq;
		DRM_ERROR("failed to get irq: %d\n", ret);
		return ret;
	}

	ret = devm_request_irq(dev, dp->irq, dp_display_irq,
		IRQF_TRIGGER_HIGH, "dp_display_isr", dp);
	if (ret < 0) {
		DRM_ERROR("failed to request IRQ%u: %d\n",
				dp->irq, ret);
		return ret;
	}

	dp_audio_init(dp);

	return ret;
}

static void dp_display_unbind(struct device *dev, struct device *master,
			      void *data)
{
	struct msm_dp *dp;
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = dev_get_drvdata(master);
	struct msm_drm_private *priv = drm->dev_private;

	dp = platform_get_drvdata(pdev);
	if (!dp) {
		DRM_ERROR("Invalid DP driver data\n");
		return;
	}

	clk_bulk_disable_unprepare(ARRAY_SIZE(dp->clk_core), dp->clk_core);

	dp_aux_unregister(dp);

	/* disable ctrl irqs */
	dp_write(dp, REG_DP_INTR_STATUS, 0);
	dp_write(dp, REG_DP_INTR_STATUS2, 0);

	priv->dp = NULL;
}

static const struct component_ops dp_display_comp_ops = {
	.bind = dp_display_bind,
	.unbind = dp_display_unbind,
};

static int dp_display_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &dp_display_comp_ops);
}

static int dp_display_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dp_display_comp_ops);
	return 0;
}

static const struct of_device_id dp_dt_match[] = {
	{.compatible = "qcom,sm8150-dp"},
	{.compatible = "qcom,sm8250-dp"},
	{}
};

static struct platform_driver dp_display_driver = {
	.probe  = dp_display_probe,
	.remove = dp_display_remove,
	.driver = {
		.name = "msm-dp-display",
		.of_match_table = dp_dt_match,
	},
};

int __init msm_dp_register(void)
{
	int ret;

	ret = platform_driver_register(&dp_display_driver);
	if (ret) {
		DRM_ERROR("driver register failed");
		return ret;
	}

	return ret;
}

void __exit msm_dp_unregister(void)
{
	platform_driver_unregister(&dp_display_driver);
}
