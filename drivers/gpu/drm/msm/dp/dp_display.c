// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 */

#define HPD_STRING_SIZE 30

#include <linux/usb/typec_dp.h>

#include "dp_display.h"

static const struct of_device_id dp_dt_match[] = {
	{.compatible = "qcom,sm8150-dp"},
	{.compatible = "qcom,sm8250-dp"},
	{}
};

int msm_dp_modeset_init(struct msm_dp *dp, struct drm_device *dev, struct drm_encoder *encoder)
{
	drm_connector_attach_encoder(dp->connector, encoder);
	return 0;
}

void msm_dp_display_mode_set(struct msm_dp *dp, struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	/* TODO: check dp->connector->display_info.bpc to find bpc */
	dp->mode_bpc = BPC_8;
	drm_mode_copy(&dp->mode, adjusted_mode);

	printk("msm_dp_display_mode_set\n");

	/* retrain and reset ctrl parameters */
	if (!dp->ctrl_on)
		dp_ctrl_on(&dp->ctrl);
	else {
		dp_ctrl_off(&dp->ctrl);
		dp_ctrl_on(&dp->ctrl);
		//dp_ctrl_link_maintenance(&dp->ctrl);
	}
}

static irqreturn_t dp_display_irq(int irq, void *dev_id)
{
	struct msm_dp *dp = dev_id;

	if (!dp) {
		DRM_ERROR("invalid data\n");
		return IRQ_NONE;
	}

	/* DP controller isr */
	dp_ctrl_isr(&dp->ctrl);

	/* DP aux isr */
	dp_aux_isr(dp);

	return IRQ_HANDLED;
}

struct dp_connector {
	struct drm_connector base;
	struct msm_dp *dp_display;
};
#define to_dp_connector(x) container_of(x, struct dp_connector, base)

static enum drm_connector_status
dp_connector_detect(struct drm_connector *connector, bool force)
{
	struct msm_dp *dp = to_dp_connector(connector)->dp_display;

	return (dp->is_connected) ? connector_status_connected :
			connector_status_disconnected;
}

static int
dp_connector_get_modes(struct drm_connector *connector)
{
	struct msm_dp *dp = to_dp_connector(connector)->dp_display;
	int ret;

	if (!dp->is_connected || !dp->edid)
		return 0;

	// XXX shouldn't be here ?
	ret = drm_connector_update_edid_property(dp->connector, dp->edid);
	if (ret)
		return ret;

	return drm_add_edid_modes(dp->connector, dp->edid);
}

static u32 dp_panel_get_supported_bpp(struct msm_dp *dp,
		u32 mode_edid_bpp, u32 mode_pclk_khz)
{
	const u32 max_supported_bpp = 24, min_supported_bpp = 18;
	u32 bpp = 0, data_rate_khz = 0;

	bpp = min_t(u32, mode_edid_bpp, max_supported_bpp);

	data_rate_khz = dp->phy_opts.dp.lanes * dp->phy_opts.dp.link_rate * 100 * 8;

	while (bpp > min_supported_bpp) {
		if (mode_pclk_khz * bpp <= data_rate_khz)
			break;
		bpp -= 6;
	}

	return bpp;
}

static int
dp_display_validate_mode(struct msm_dp *dp, u32 mode_pclk_khz)
{
	const u32 num_components = 3, default_bpp = 24;
	u32 mode_rate_khz = 0, supported_rate_khz = 0, mode_bpp = 0;

	mode_bpp = dp->connector->display_info.bpc * num_components;
	if (!mode_bpp)
		mode_bpp = default_bpp;

	mode_bpp = dp_panel_get_supported_bpp(dp,
			mode_bpp, mode_pclk_khz);

	mode_rate_khz = mode_pclk_khz * mode_bpp;
	supported_rate_khz = dp->phy_opts.dp.lanes * dp->phy_opts.dp.link_rate * 100 * 8;

	if (mode_rate_khz > supported_rate_khz)
		return MODE_BAD;

	return MODE_OK;
}

static enum drm_mode_status
dp_connector_mode_valid(struct drm_connector *connector, struct drm_display_mode *mode)
{
	struct msm_dp *dp_disp;

	dp_disp = to_dp_connector(connector)->dp_display;

	return dp_display_validate_mode(dp_disp, mode->clock);
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

/* connector initialization */
static struct drm_connector *dp_drm_connector_init(struct drm_device *drm, struct msm_dp *dp_display)
{
	struct drm_connector *connector = NULL;
	struct dp_connector *dp_connector;
	int ret;

	dp_connector = devm_kzalloc(drm->dev,
					sizeof(*dp_connector),
					GFP_KERNEL);
	if (!dp_connector)
		return ERR_PTR(-ENOMEM);

	dp_connector->dp_display = dp_display;

	connector = &dp_connector->base;

	ret = drm_connector_init(drm, connector,
			&dp_connector_funcs,
			DRM_MODE_CONNECTOR_DisplayPort);
	if (ret)
		return ERR_PTR(ret);

	drm_connector_helper_add(connector, &dp_connector_helper_funcs);

	/*
	 * Enable HPD to let hpd event is handled when cable is connected.
	 */
	connector->polled = DRM_CONNECTOR_POLL_HPD;

	return connector;
}

/* from intel_dp.c (common helper?) */
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

	dp->edid = drm_get_edid(dp->connector, &dp->aux.ddc);
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
	bool hpd_state, changed;
	bool dp_altmode;

	/* also: state->mode ? */
	dp_altmode =
		(data && state->alt && state->alt->svid == USB_TYPEC_DP_SID && state->alt->active);

	_orientation_hack = typec_altmode_get_orientation(state->alt) == TYPEC_ORIENTATION_REVERSE;

	if (!dp_altmode) {
		if (dp->phy_init) {
			phy_exit(dp->phy);
			dp->phy_init = false;
		}
		return 0;
	}

	if (!dp->phy_init) {
		phy_init(dp->phy);
		dp->phy_init = true;
	}

	hpd_state = (data->status & DP_STATUS_HPD_STATE);

	/* no IRQ and no change to hpd state */
	if (!(data->status & DP_STATUS_IRQ_HPD) && hpd_state == dp->hpd_high)
		return 0;

	changed = (hpd_state != dp->hpd_high);
	dp->hpd_high = hpd_state;

	if (!hpd_state) {
		// connected->disconnected. TODO

		// dp_ctrl_push_idle(&dp->ctrl);
		// dp_ctrl_off(&dp->ctrl);

		return 0;
	}

	ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
	if (ret < DP_LINK_STATUS_SIZE) {
		DRM_ERROR("DP link status read failed\n");
		return ret;
	}

	printk("dp_link_process_hpd 2\n");

	/* TODO: read DP_DEVICE_SERVICE_IRQ_VECTOR for automated tests */

	updated = link_status[DP_LANE_ALIGN_STATUS_UPDATED - DP_LANE0_1_STATUS];

	if (updated & DP_LINK_STATUS_UPDATED) {
		printk("DP_LINK_STATUS_UPDATED\n");

		if (!drm_dp_clock_recovery_ok(link_status, dp->phy_opts.dp.lanes))
			DRM_ERROR("clock recovery NOT ok");
		if (!drm_dp_channel_eq_ok(link_status, dp->phy_opts.dp.lanes))
			DRM_ERROR("channel eq NOT ok");

		//dp_ctrl_link_maintenance(&dp->ctrl);
	}

	if ((updated & DP_DOWNSTREAM_PORT_STATUS_CHANGED) || changed) {
		/* TODO: read DP_SINK_COUNT, which should always be 1 */

		printk("dp_link_process_hpd 3\n");

		// TODO: hpd off if hpd was already high?
		//dp_display_send_hpd_notification(dp, false);

		/* if (dp_display_is_sink_count_zero(dp)) {
			DRM_DEBUG_DP("sink count is zero, nothing to do\n");
			return 0;
		}*/

		dp_panel_read_sink_caps(dp);
		//dp_display_send_hpd_notification(dp, true);

		changed = true;
	}

	if (!changed)
		return 0;


	//dp_ctrl_on(&dp->ctrl);

	dp->is_connected = true;
	drm_helper_hpd_irq_event(dp->connector->dev);

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

	dp_ctrl_get(&dp->ctrl, &dp->aux);

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
	dp->clk_ctrl[2].id = "ctrl_pixel_clk";

	ret = devm_clk_bulk_get(dev, ARRAY_SIZE(dp->clk_ctrl), dp->clk_ctrl);
	if (ret) {
		DRM_ERROR("failed to get DP_CTRL_PM clk. err=%d\n", ret);
		return -ENODEV;
	}

	dp->phy = devm_phy_get(dev, "dp");
	if (IS_ERR(dp->phy)) {
		DRM_DEV_ERROR(dev, "failed to get DP phy\n");
		return PTR_ERR(dp->phy);
	}

#if 0
	ret = phy_init(dp->phy);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to init DP phy\n");
		return ret;
	}
#endif

	dp->connector = dp_drm_connector_init(drm, dp);
	if (IS_ERR(dp->connector)) {
		ret = PTR_ERR(dp->connector);
		DRM_DEV_ERROR(dev, "failed to create dp connector: %d\n", ret);
		dp->connector = NULL;
		return ret;
	}

	priv->connectors[priv->num_connectors++] = dp->connector;

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(dp->clk_core), dp->clk_core);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to enable core clks: %d\n", ret);
		return ret;
	}

	dp_ctrl_host_init(&dp->ctrl);
	dp_aux_register(dp, dev);

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
