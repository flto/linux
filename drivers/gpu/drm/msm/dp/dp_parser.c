// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm-dp] %s: " fmt, __func__

#include <linux/of_gpio.h>

#include "dp_parser.h"

static const struct dp_regulator_cfg sdm845_dp_reg_cfg = {
	.num = 2,
	.regs = {
		{"vdda-1p2", 21800, 4 },	/* 1.2 V */
		{"vdda-0p9", 36000, 32 },	/* 0.9 V */
	},
};

static int msm_dss_ioremap_byname(struct platform_device *pdev,
			   struct dss_io_data *io_data, const char *name)
{
	struct resource *res = NULL;

	if (!io_data) {
		DRM_ERROR("%pS->%s: invalid input\n",
			__builtin_return_address(0), __func__);
		return -EINVAL;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	if (!res) {
		DRM_ERROR("%pS->%s: '%s' msm_dss_get_res_byname failed\n",
			__builtin_return_address(0), __func__, name);
		return -ENODEV;
	}

	io_data->len = (u32)resource_size(res);
	io_data->base = ioremap(res->start, io_data->len);
	if (!io_data->base) {
		DRM_ERROR("%pS->%s: '%s' ioremap failed\n",
			__builtin_return_address(0), __func__, name);
		return -EIO;
	}

	return 0;
}

static void msm_dss_iounmap(struct dss_io_data *io_data)
{
	if (!io_data) {
		DRM_ERROR("%pS->%s: invalid input\n",
			__builtin_return_address(0), __func__);
		return;
	}

	if (io_data->base) {
		iounmap(io_data->base);
		io_data->base = NULL;
	}
	io_data->len = 0;
}

static void dp_parser_unmap_io_resources(struct dp_parser *parser)
{
	struct dp_io *io = &parser->io;

	msm_dss_iounmap(&io->dp_ahb);
	msm_dss_iounmap(&io->dp_aux);
	msm_dss_iounmap(&io->dp_link);
	msm_dss_iounmap(&io->dp_p0);
	msm_dss_iounmap(&io->phy_io);
	msm_dss_iounmap(&io->ln_tx0_io);
	msm_dss_iounmap(&io->ln_tx0_io);
	msm_dss_iounmap(&io->dp_pll_io);
	msm_dss_iounmap(&io->dp_cc_io);
	msm_dss_iounmap(&io->usb3_dp_com);
	msm_dss_iounmap(&io->qfprom_io);
}

static int dp_parser_ctrl_res(struct dp_parser *parser)
{
	int rc = 0;
	u32 index;
	struct platform_device *pdev = parser->pdev;
	struct device_node *of_node = parser->pdev->dev.of_node;
	struct dp_io *io = &parser->io;

	rc = of_property_read_u32(of_node, "cell-index", &index);
	if (rc) {
		DRM_ERROR("cell-index not specified, rc=%d\n", rc);
		return rc;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->dp_ahb, "dp_ahb");
	if (rc) {
		DRM_ERROR("unable to remap dp io resources, rc=%d\n", rc);
		goto err;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->dp_aux, "dp_aux");
	if (rc) {
		DRM_ERROR("unable to remap dp io resources, rc=%d\n", rc);
		goto err;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->dp_link, "dp_link");
	if (rc) {
		DRM_ERROR("unable to remap dp io resources, rc=%d\n", rc);
		goto err;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->dp_p0, "dp_p0");
	if (rc) {
		DRM_ERROR("unable to remap dp io resources, rc=%d\n", rc);
		goto err;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->phy_io, "dp_phy");
	if (rc) {
		DRM_ERROR("unable to remap dp PHY resources, rc=%d\n", rc);
		goto err;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->ln_tx0_io, "dp_ln_tx0");
	if (rc) {
		DRM_ERROR("unable to remap dp TX0 resources, rc=%d\n", rc);
		goto err;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->ln_tx1_io, "dp_ln_tx1");
	if (rc) {
		DRM_ERROR("unable to remap dp TX1 resources, rc=%d\n", rc);
		goto err;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->dp_pll_io, "dp_pll");
	if (rc) {
		DRM_ERROR("unable to remap DP PLL resources, rc=%d\n", rc);
		goto err;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->usb3_dp_com, "usb3_dp_com");
	if (rc) {
		DRM_ERROR("unable to remap USB3 DP com resources, rc=%d\n", rc);
		goto err;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->dp_cc_io, "dp_mmss_cc");
	if (rc) {
		DRM_ERROR("unable to remap dp MMSS_CC resources\n");
		goto err;
	}

	if (msm_dss_ioremap_byname(pdev, &io->qfprom_io, "qfprom_physical"))
		pr_warn("unable to remap dp qfprom resources\n");

	return 0;
err:
	dp_parser_unmap_io_resources(parser);
	return rc;
}

static const char *dp_get_phy_aux_config_property(u32 cfg_type)
{
	switch (cfg_type) {
	case PHY_AUX_CFG0:
		return "qcom,aux-cfg0-settings";
	case PHY_AUX_CFG1:
		return "qcom,aux-cfg1-settings";
	case PHY_AUX_CFG2:
		return "qcom,aux-cfg2-settings";
	case PHY_AUX_CFG3:
		return "qcom,aux-cfg3-settings";
	case PHY_AUX_CFG4:
		return "qcom,aux-cfg4-settings";
	case PHY_AUX_CFG5:
		return "qcom,aux-cfg5-settings";
	case PHY_AUX_CFG6:
		return "qcom,aux-cfg6-settings";
	case PHY_AUX_CFG7:
		return "qcom,aux-cfg7-settings";
	case PHY_AUX_CFG8:
		return "qcom,aux-cfg8-settings";
	case PHY_AUX_CFG9:
		return "qcom,aux-cfg9-settings";
	default:
		return "unknown";
	}
}

static int dp_parser_aux(struct dp_parser *parser)
{
	struct device_node *of_node = parser->pdev->dev.of_node;
	int len = 0, i = 0, j = 0, config_count = 0;
	const char *data;

	for (i = 0; i < PHY_AUX_CFG_MAX; i++) {
		const char *property = dp_get_phy_aux_config_property(i);

		data = of_get_property(of_node, property, &len);
		if (!data) {
			DRM_ERROR("Unable to read %s\n", property);
			goto error;
		}

		config_count = len - 1;
		if (config_count < 1 || /* minimum config count = 1 */
			config_count > DP_AUX_CFG_MAX_VALUE_CNT) {
			DRM_ERROR("Invalid config count (%d) configs for %s\n",
					config_count, property);
			goto error;
		}

		parser->aux_cfg[i].offset = data[0];
		parser->aux_cfg[i].cfg_cnt = config_count;
		DRM_DEBUG_DP("%s offset=0x%x, cfg_cnt=%d\n",
				property,
				parser->aux_cfg[i].offset,
				parser->aux_cfg[i].cfg_cnt);
		for (j = 1; j < len; j++) {
			parser->aux_cfg[i].lut[j - 1] = data[j];
			DRM_DEBUG_DP("%s lut[%d]=0x%x\n",
					property,
					i,
					parser->aux_cfg[i].lut[j - 1]);
		}
	}
		return 0;

error:
	for (i = 0; i < PHY_AUX_CFG_MAX; i++)
		parser->aux_cfg[i] = (const struct dp_aux_cfg){ 0 };

	return -EINVAL;
}

static int dp_parser_misc(struct dp_parser *parser)
{
	int rc = 0;
	struct device_node *of_node = parser->pdev->dev.of_node;

	rc = of_property_read_u32(of_node,
		"qcom,max-pclk-frequency-khz", &parser->max_pclk_khz);
	if (rc)
		parser->max_pclk_khz = DP_MAX_PIXEL_CLK_KHZ;

	rc = of_property_read_u32(of_node,
		"qcom,max-lanes-for-dp", &parser->max_dp_lanes);

	if (rc)
		parser->max_dp_lanes = DP_MAX_NUM_DP_LANES;

	return 0;
}

static int dp_parser_pinctrl(struct dp_parser *parser)
{
#if 0
	struct dp_pinctrl *pinctrl = &parser->pinctrl;

	pinctrl->pin = devm_pinctrl_get(&parser->pdev->dev);

	if (IS_ERR_OR_NULL(pinctrl->pin)) {
		DRM_ERROR("failed to get pinctrl, rc=%d\n",
					PTR_ERR_OR_ZERO(pinctrl->pin));
		return -EINVAL;
	}

	pinctrl->state_active = pinctrl_lookup_state(pinctrl->pin,
					"mdss_dp_active");
	if (IS_ERR_OR_NULL(pinctrl->state_active)) {
		DRM_ERROR("failed to get pinctrl active state, %d\n",
			PTR_ERR_OR_ZERO(pinctrl->state_active));
		return -EINVAL;
	}

	pinctrl->state_suspend = pinctrl_lookup_state(pinctrl->pin,
					"mdss_dp_sleep");
	if (IS_ERR_OR_NULL(pinctrl->state_suspend)) {
		DRM_ERROR("failed to get pinctrl suspend state, %d\n",
			PTR_ERR_OR_ZERO(pinctrl->state_suspend));
		return -EINVAL;
	}

#endif
	return 0;
}

static int dp_parser_gpio(struct dp_parser *parser)
{
#if 0
	struct device *dev = &parser->pdev->dev;
	struct device_node *of_node = dev->of_node;

	parser->usbplug_cc_gpio = of_get_named_gpio(of_node,
					"qcom,usbplug-cc-gpio", 0);
	if (!gpio_is_valid(parser->usbplug_cc_gpio)) {
		DRM_ERROR("usbplug-cc-gpio not specified\n");
		return -EINVAL;
	}

	parser->aux_en_gpio = of_get_named_gpio(of_node,
					"qcom,aux-en-gpio", 0);
	if (!gpio_is_valid(parser->aux_en_gpio)) {
		DRM_ERROR("aux-en-gpio not specified\n");
		return -EINVAL;
	}

	parser->aux_sel_gpio = of_get_named_gpio(of_node,
					"qcom,aux-sel-gpio", 0);
	if (!gpio_is_valid(parser->aux_sel_gpio)) {
		DRM_ERROR("aux-sel-gpio not specified\n");
		return -EINVAL;
	}

#endif
	return 0;
}

static inline bool dp_parser_check_prefix(const char *clk_prefix,
						const char *clk_name)
{
	return !strncmp(clk_prefix, clk_name, strlen(clk_prefix));
}

static int dp_parser_init_clk_data(struct dp_parser *parser)
{
	int num_clk = 0, i = 0, rc = 0;
	int core_clk_count = 0, ctrl_clk_count = 0;
	const char *clk_name;
	struct device *dev = &parser->pdev->dev;
	struct dss_module_power *core_power = &parser->mp[DP_CORE_PM];
	struct dss_module_power *ctrl_power = &parser->mp[DP_CTRL_PM];

	num_clk = of_property_count_strings(dev->of_node, "clock-names");
	if (num_clk <= 0) {
		DRM_ERROR("no clocks are defined\n");
		return -EINVAL;
	}

	for (i = 0; i < num_clk; i++) {
		rc = of_property_read_string_index(dev->of_node,
				"clock-names", i, &clk_name);
		if (rc) {
			DRM_ERROR("error reading clock-names %d\n", rc);
			return rc;
		}

		if (dp_parser_check_prefix("core", clk_name))
			core_clk_count++;

		if (dp_parser_check_prefix("ctrl", clk_name))
			ctrl_clk_count++;
	}

	/* Initialize the CORE power module */
	if (core_clk_count == 0) {
		DRM_ERROR("no core clocks are defined\n");
		return -EINVAL;
	}

	core_power->num_clk = core_clk_count;
	core_power->clk_config = devm_kzalloc(dev,
			sizeof(struct dss_clk) * core_power->num_clk,
			GFP_KERNEL);
	if (!core_power->clk_config)
		return -EINVAL;

	/* Initialize the CTRL power module */
	if (ctrl_clk_count == 0) {
		DRM_ERROR("no ctrl clocks are defined\n");
		return -EINVAL;
	}

	ctrl_power->num_clk = ctrl_clk_count;
	ctrl_power->clk_config = devm_kzalloc(dev,
			sizeof(struct dss_clk) * ctrl_power->num_clk,
			GFP_KERNEL);
	if (!ctrl_power->clk_config) {
		ctrl_power->num_clk = 0;
		return -EINVAL;
	}

	return 0;
}

static int dp_parser_clock(struct dp_parser *parser)
{
	int rc = 0, i = 0;
	int num_clk = 0;
	int core_clk_index = 0, ctrl_clk_index = 0;
	int core_clk_count = 0, ctrl_clk_count = 0;
	const char *clk_name;
	struct device *dev = &parser->pdev->dev;
	struct dss_module_power *core_power = &parser->mp[DP_CORE_PM];
	struct dss_module_power *ctrl_power = &parser->mp[DP_CTRL_PM];

	core_power = &parser->mp[DP_CORE_PM];
	ctrl_power = &parser->mp[DP_CTRL_PM];

	rc =  dp_parser_init_clk_data(parser);
	if (rc) {
		DRM_ERROR("failed to initialize power data %d\n", rc);
		return -EINVAL;
	}

	core_clk_count = core_power->num_clk;
	ctrl_clk_count = ctrl_power->num_clk;

	num_clk = core_clk_count + ctrl_clk_count;

	for (i = 0; i < num_clk; i++) {
		rc = of_property_read_string_index(dev->of_node, "clock-names",
				i, &clk_name);
		if (rc) {
			DRM_ERROR("error reading clock-names %d\n", rc);
			return rc;
		}
		if (dp_parser_check_prefix("core", clk_name) &&
				core_clk_index < core_clk_count) {
			struct dss_clk *clk =
				&core_power->clk_config[core_clk_index];
			strlcpy(clk->clk_name, clk_name, sizeof(clk->clk_name));
			clk->type = DSS_CLK_AHB;
			core_clk_index++;
		} else if (dp_parser_check_prefix("ctrl", clk_name) &&
			   ctrl_clk_index < ctrl_clk_count) {
			struct dss_clk *clk =
				&ctrl_power->clk_config[ctrl_clk_index];
			strlcpy(clk->clk_name, clk_name, sizeof(clk->clk_name));
			ctrl_clk_index++;

			if (!strncmp(clk_name, "ctrl_link_clk",
					strlen("ctrl_link_clk")) ||
					!strncmp(clk_name, "ctrl_pixel_clk",
					strlen("ctrl_pixel_clk")))
				clk->type = DSS_CLK_PCLK;
			else
				clk->type = DSS_CLK_AHB;
		}
	}

	DRM_DEBUG_DP("clock parsing successful\n");

	return 0;
}

static int dp_parser_parse(struct dp_parser *parser)
{
	int rc = 0;

	if (!parser) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	/* Default: We assume that USB and DP share PHY(Combo phy) */
	parser->combo_phy_en = true;

	rc = dp_parser_ctrl_res(parser);
	if (rc)
		return rc;

	rc = dp_parser_aux(parser);
	if (rc)
		return rc;

	rc = dp_parser_misc(parser);
	if (rc)
		return rc;

	rc = dp_parser_clock(parser);
	if (rc)
		return rc;

	/* Map the corresponding regulator information according to
	 * version. Currently, since we only have one supported platform,
	 * mapping the regulator directly.
	 */
	parser->regulator_cfg = &sdm845_dp_reg_cfg;

	rc = dp_parser_gpio(parser);
	if (rc) {
		DRM_ERROR("Parsing GPIOs failed.Assuming ComboPhy disabled\n");
		parser->combo_phy_en = false;
		return 0;
	}

	rc = dp_parser_pinctrl(parser);
	return rc;
}

struct dp_parser *dp_parser_get(struct platform_device *pdev)
{
	struct dp_parser *parser;

	parser = devm_kzalloc(&pdev->dev, sizeof(*parser), GFP_KERNEL);
	if (!parser)
		return ERR_PTR(-ENOMEM);

	parser->parse = dp_parser_parse;
	parser->pdev = pdev;

	return parser;
}

void dp_parser_put(struct dp_parser *parser)
{

}
