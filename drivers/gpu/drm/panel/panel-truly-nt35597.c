// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

static const char * const regulator_names[] = {
	"vdda",
	"vdispp",
	"vdispn",
};

static unsigned long const regulator_enable_loads[] = {
	62000,
	100000,
	100000,
};

static unsigned long const regulator_disable_loads[] = {
	80,
	100,
	100,
};

struct cmd_set {
	u8 commands[4];
	u8 size;
};

struct nt35597_config {
	u32 width_mm;
	u32 height_mm;
	const char *panel_name;
	const struct cmd_set *panel_on_cmds;
	u32 num_on_cmds;
	const struct drm_display_mode *dm;
};

struct truly_nt35597 {
	struct device *dev;
	struct drm_panel panel;

	struct regulator_bulk_data supplies[ARRAY_SIZE(regulator_names)];

	struct gpio_desc *reset_gpio;
	struct gpio_desc *mode_gpio;

	struct backlight_device *backlight;

	struct mipi_dsi_device *dsi[2];

	const struct nt35597_config *config;
	bool prepared;
	bool enabled;
};

static inline struct truly_nt35597 *panel_to_ctx(struct drm_panel *panel)
{
	return container_of(panel, struct truly_nt35597, panel);
}

static const struct cmd_set qcom_2k_panel_magic_cmds[] = {
	{ { 0xff, 0x10 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x36, 0x00 }, 2 },
	{ { 0x35, 0x00 }, 2 },
	{ { 0x44, 0x03, 0xe8 }, 3 },
	{ { 0x51, 0xff }, 2 },
	{ { 0x53, 0x2c }, 2 },
	{ { 0x55, 0x01 }, 2 },
	{ { 0x20 }, 1 },
	{ { 0xbb, 0x10 }, 2 },
};

static int truly_dcs_write(struct drm_panel *panel, u32 command)
{
	struct truly_nt35597 *ctx = panel_to_ctx(panel);
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(ctx->dsi); i++) {
		ret = mipi_dsi_dcs_write(ctx->dsi[i], command, NULL, 0);
		if (ret < 0) {
			DRM_DEV_ERROR(ctx->dev,
				"cmd 0x%x failed for dsi = %d\n",
				command, i);
		}
	}

	return ret;
}

static int truly_dcs_write_buf(struct drm_panel *panel,
	u32 size, const u8 *buf)
{
	struct truly_nt35597 *ctx = panel_to_ctx(panel);
	int ret = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(ctx->dsi); i++) {
		ret = mipi_dsi_dcs_write_buffer(ctx->dsi[i], buf, size);
		if (ret < 0) {
			DRM_DEV_ERROR(ctx->dev,
				"failed to tx cmd [%d], err: %d\n", i, ret);
			return ret;
		}
	}

	return ret;
}

static int truly_35597_power_on(struct truly_nt35597 *ctx)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(ctx->supplies); i++) {
		ret = regulator_set_load(ctx->supplies[i].consumer,
					regulator_enable_loads[i]);
		if (ret)
			return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	/*
	 * Reset sequence of truly panel requires the panel to be
	 * out of reset for 10ms, followed by being held in reset
	 * for 10ms and then out again
	 */
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(10000, 20000);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10000, 20000);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(50000, 60000);

	return 0;
}

static int truly_nt35597_power_off(struct truly_nt35597 *ctx)
{
	int ret = 0;
	int i;

	gpiod_set_value(ctx->reset_gpio, 1);

	for (i = 0; i < ARRAY_SIZE(ctx->supplies); i++) {
		ret = regulator_set_load(ctx->supplies[i].consumer,
				regulator_disable_loads[i]);
		if (ret) {
			DRM_DEV_ERROR(ctx->dev,
				"regulator_set_load failed %d\n", ret);
			return ret;
		}
	}

	ret = regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret) {
		DRM_DEV_ERROR(ctx->dev,
			"regulator_bulk_disable failed %d\n", ret);
	}
	return ret;
}

static int truly_nt35597_disable(struct drm_panel *panel)
{
	struct truly_nt35597 *ctx = panel_to_ctx(panel);
	int ret;

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ret = backlight_disable(ctx->backlight);
		if (ret < 0)
			DRM_DEV_ERROR(ctx->dev, "backlight disable failed %d\n",
				ret);
	}

	ctx->enabled = false;
	return 0;
}

static int truly_nt35597_unprepare(struct drm_panel *panel)
{
	struct truly_nt35597 *ctx = panel_to_ctx(panel);
	int ret = 0;

	if (!ctx->prepared)
		return 0;

	ctx->dsi[0]->mode_flags = 0;
	ctx->dsi[1]->mode_flags = 0;

	ret = truly_dcs_write(panel, MIPI_DCS_SET_DISPLAY_OFF);
	if (ret < 0) {
		DRM_DEV_ERROR(ctx->dev,
			"set_display_off cmd failed ret = %d\n",
			ret);
	}

	/* 120ms delay required here as per DCS spec */
	msleep(120);

	ret = truly_dcs_write(panel, MIPI_DCS_ENTER_SLEEP_MODE);
	if (ret < 0) {
		DRM_DEV_ERROR(ctx->dev,
			"enter_sleep cmd failed ret = %d\n", ret);
	}

	ret = truly_nt35597_power_off(ctx);
	if (ret < 0)
		DRM_DEV_ERROR(ctx->dev, "power_off failed ret = %d\n", ret);

	ctx->prepared = false;
	return ret;
}

static int truly_nt35597_prepare(struct drm_panel *panel)
{
	struct truly_nt35597 *ctx = panel_to_ctx(panel);
	int ret;
	int i;
	const struct cmd_set *panel_on_cmds;
	const struct nt35597_config *config;
	u32 num_cmds;

	if (ctx->prepared)
		return 0;

	ret = truly_35597_power_on(ctx);
	if (ret < 0)
		return ret;

	ctx->dsi[0]->mode_flags |= MIPI_DSI_MODE_LPM;
	ctx->dsi[1]->mode_flags |= MIPI_DSI_MODE_LPM;

	config = ctx->config;
	panel_on_cmds = config->panel_on_cmds;
	num_cmds = config->num_on_cmds;

	for (i = 0; i < num_cmds; i++) {
		ret = truly_dcs_write_buf(panel,
				panel_on_cmds[i].size,
					panel_on_cmds[i].commands);
		if (ret < 0) {
			DRM_DEV_ERROR(ctx->dev,
				"cmd set tx failed i = %d ret = %d\n",
					i, ret);
			goto power_off;
		}
	}

	ret = truly_dcs_write(panel, MIPI_DCS_EXIT_SLEEP_MODE);
	if (ret < 0) {
		DRM_DEV_ERROR(ctx->dev,
			"exit_sleep_mode cmd failed ret = %d\n",
			ret);
		goto power_off;
	}

	/* Per DSI spec wait 120ms after sending exit sleep DCS command */
	msleep(120);

	ret = truly_dcs_write(panel, MIPI_DCS_SET_DISPLAY_ON);
	if (ret < 0) {
		DRM_DEV_ERROR(ctx->dev,
			"set_display_on cmd failed ret = %d\n", ret);
		goto power_off;
	}

	/* Per DSI spec wait 120ms after sending set_display_on DCS command */
	msleep(120);

	ctx->prepared = true;

	return 0;

power_off:
	if (truly_nt35597_power_off(ctx))
		DRM_DEV_ERROR(ctx->dev, "power_off failed\n");
	return ret;
}

static int truly_nt35597_enable(struct drm_panel *panel)
{
	struct truly_nt35597 *ctx = panel_to_ctx(panel);
	int ret;

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ret = backlight_enable(ctx->backlight);
		if (ret < 0)
			DRM_DEV_ERROR(ctx->dev, "backlight enable failed %d\n",
						  ret);
	}

	ctx->enabled = true;

	return 0;
}

static int truly_nt35597_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct truly_nt35597 *ctx = panel_to_ctx(panel);
	struct drm_display_mode *mode;
	const struct nt35597_config *config;

	config = ctx->config;
	mode = drm_mode_create(connector->dev);
	if (!mode) {
		DRM_DEV_ERROR(ctx->dev,
			"failed to create a new display mode\n");
		return 0;
	}

	connector->display_info.width_mm = config->width_mm;
	connector->display_info.height_mm = config->height_mm;
	drm_mode_copy(mode, config->dm);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs truly_nt35597_drm_funcs = {
	.disable = truly_nt35597_disable,
	.unprepare = truly_nt35597_unprepare,
	.prepare = truly_nt35597_prepare,
	.enable = truly_nt35597_enable,
	.get_modes = truly_nt35597_get_modes,
};

static int truly_nt35597_panel_add(struct truly_nt35597 *ctx)
{
	struct device *dev = ctx->dev;
	int ret, i;
	const struct nt35597_config *config;

	config = ctx->config;
	for (i = 0; i < ARRAY_SIZE(ctx->supplies); i++)
		ctx->supplies[i].supply = regulator_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		DRM_DEV_ERROR(dev, "cannot get reset gpio %ld\n",
			PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}

	ctx->mode_gpio = devm_gpiod_get_optional(dev, "mode", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->mode_gpio)) {
		DRM_DEV_ERROR(dev, "cannot get mode gpio %ld\n",
			PTR_ERR(ctx->mode_gpio));
		return PTR_ERR(ctx->mode_gpio);
	}

	/* dual port */
	gpiod_set_value(ctx->mode_gpio, 0);

	drm_panel_init(&ctx->panel, dev, &truly_nt35597_drm_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	drm_panel_add(&ctx->panel);

	return 0;
}

static const struct drm_display_mode qcom_sdm845_mtp_2k_mode = {
	.name = "1440x2560",
	.clock = 268316,
	.hdisplay = 1440,
	.hsync_start = 1440 + 200,
	.hsync_end = 1440 + 200 + 32,
	.htotal = 1440 + 200 + 32 + 64,
	.vdisplay = 2560,
	.vsync_start = 2560 + 8,
	.vsync_end = 2560 + 8 + 1,
	.vtotal = 2560 + 8 + 1 + 7,
	.vrefresh = 60,
	.flags = 0,
};

static const struct nt35597_config nt35597_dir = {
	.width_mm = 74,
	.height_mm = 131,
	.panel_name = "qcom_sdm845_mtp_2k_panel",
	.dm = &qcom_sdm845_mtp_2k_mode,
	.panel_on_cmds = qcom_2k_panel_magic_cmds,
	.num_on_cmds = ARRAY_SIZE(qcom_2k_panel_magic_cmds),
};

static int truly_nt35597_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct truly_nt35597 *ctx;
	struct mipi_dsi_device *dsi1_device;
	struct device_node *dsi1;
	struct mipi_dsi_host *dsi1_host;
	struct mipi_dsi_device *dsi_dev;
	int ret = 0;
	int i;

	const struct mipi_dsi_device_info info = {
		.type = "trulynt35597",
		.channel = 0,
		.node = NULL,
	};

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);

	if (!ctx)
		return -ENOMEM;

	/*
	 * This device represents itself as one with two input ports which are
	 * fed by the output ports of the two DSI controllers . The DSI0 is
	 * the master controller and has most of the panel related info in its
	 * child node.
	 */

	ctx->config = of_device_get_match_data(dev);

	if (!ctx->config) {
		dev_err(dev, "missing device configuration\n");
		return -ENODEV;
	}

	dsi1 = of_graph_get_remote_node(dsi->dev.of_node, 1, -1);
	if (!dsi1) {
		DRM_DEV_ERROR(dev,
			"failed to get remote node for dsi1_device\n");
		return -ENODEV;
	}

	dsi1_host = of_find_mipi_dsi_host_by_node(dsi1);
	of_node_put(dsi1);
	if (!dsi1_host) {
		DRM_DEV_ERROR(dev, "failed to find dsi host\n");
		return -EPROBE_DEFER;
	}

	/* register the second DSI device */
	dsi1_device = mipi_dsi_device_register_full(dsi1_host, &info);
	if (IS_ERR(dsi1_device)) {
		DRM_DEV_ERROR(dev, "failed to create dsi device\n");
		return PTR_ERR(dsi1_device);
	}

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	ctx->dsi[0] = dsi;
	ctx->dsi[1] = dsi1_device;

	ret = truly_nt35597_panel_add(ctx);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to add panel\n");
		goto err_panel_add;
	}

	for (i = 0; i < ARRAY_SIZE(ctx->dsi); i++) {
		dsi_dev = ctx->dsi[i];
		dsi_dev->lanes = 4;
		dsi_dev->format = MIPI_DSI_FMT_RGB888;
		dsi_dev->mode_flags = MIPI_DSI_MODE_LPM |
			MIPI_DSI_CLOCK_NON_CONTINUOUS;
		ret = mipi_dsi_attach(dsi_dev);
		if (ret < 0) {
			DRM_DEV_ERROR(dev,
				"dsi attach failed i = %d\n", i);
			goto err_dsi_attach;
		}
	}

	return 0;

err_dsi_attach:
	drm_panel_remove(&ctx->panel);
err_panel_add:
	mipi_dsi_device_unregister(dsi1_device);
	return ret;
}

static int truly_nt35597_remove(struct mipi_dsi_device *dsi)
{
	struct truly_nt35597 *ctx = mipi_dsi_get_drvdata(dsi);

	if (ctx->dsi[0])
		mipi_dsi_detach(ctx->dsi[0]);
	if (ctx->dsi[1]) {
		mipi_dsi_detach(ctx->dsi[1]);
		mipi_dsi_device_unregister(ctx->dsi[1]);
	}

	drm_panel_remove(&ctx->panel);
	return 0;
}

static const struct of_device_id truly_nt35597_of_match[] = {
	{
		.compatible = "truly,nt35597-2K-display",
		.data = &nt35597_dir,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, truly_nt35597_of_match);

static struct mipi_dsi_driver truly_nt35597_driver = {
	.driver = {
		.name = "panel-truly-nt35597",
		.of_match_table = truly_nt35597_of_match,
	},
	.probe = truly_nt35597_probe,
	.remove = truly_nt35597_remove,
};
module_mipi_dsi_driver(truly_nt35597_driver);

MODULE_DESCRIPTION("Truly NT35597 DSI Panel Driver");
MODULE_LICENSE("GPL v2");
