// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2019, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm-dp] %s: " fmt, __func__

#include <linux/of_gpio.h>

#include "dp_parser.h"

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
		DRM_ERROR("cell-index not specified, %d\n", rc);
		return rc;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->dp_ahb, "dp_ahb");
	if (rc) {
		DRM_ERROR("unable to remap dp io resources, %d\n", rc);
		goto err;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->dp_aux, "dp_aux");
	if (rc) {
		DRM_ERROR("unable to remap dp io resources, %d\n", rc);
		goto err;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->dp_link, "dp_link");
	if (rc) {
		DRM_ERROR("unable to remap dp io resources, %d\n", rc);
		goto err;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->dp_p0, "dp_p0");
	if (rc) {
		DRM_ERROR("unable to remap dp io resources, %d\n", rc);
		goto err;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->phy_io, "dp_phy");
	if (rc) {
		DRM_ERROR("unable to remap dp PHY resources, %d\n", rc);
		goto err;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->ln_tx0_io, "dp_ln_tx0");
	if (rc) {
		DRM_ERROR("unable to remap dp TX0 resources, %d\n", rc);
		goto err;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->ln_tx1_io, "dp_ln_tx1");
	if (rc) {
		DRM_ERROR("unable to remap dp TX1 resources, %d\n", rc);
		goto err;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->dp_pll_io, "dp_pll");
	if (rc) {
		DRM_ERROR("unable to remap DP PLL resources, %d\n", rc);
		goto err;
	}

	rc = msm_dss_ioremap_byname(pdev, &io->usb3_dp_com, "usb3_dp_com");
	if (rc) {
		DRM_ERROR("unable to remap USB3 DP com resources, %d\n", rc);
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
	for (i = 0; i < PHY_AUX_CFG_MAX; i++) {
		parser->aux_cfg[i] = (const struct dp_aux_cfg){ 0 };
	}
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

	return 0;
}

static int dp_parser_pinctrl(struct dp_parser *parser)
{
	struct dp_pinctrl *pinctrl = &parser->pinctrl;

	pinctrl->pin = devm_pinctrl_get(&parser->pdev->dev);

	if (IS_ERR_OR_NULL(pinctrl->pin)) {
		DRM_ERROR("failed to get pinctrl, rc=%ld\n", PTR_ERR(pinctrl->pin));
		return -EINVAL;
	}

	pinctrl->state_active = pinctrl_lookup_state(pinctrl->pin,
					"mdss_dp_active");
	if (IS_ERR_OR_NULL(pinctrl->state_active)) {
		DRM_ERROR("failed to get pinctrl active state, %ld\n",
			PTR_ERR(pinctrl->state_active));
		return -EINVAL;
	}

	pinctrl->state_suspend = pinctrl_lookup_state(pinctrl->pin,
					"mdss_dp_sleep");
	if (IS_ERR_OR_NULL(pinctrl->state_suspend)) {
		DRM_ERROR("failed to get pinctrl suspend state, %ld\n",
			PTR_ERR(pinctrl->state_suspend));
		return -EINVAL;
	}

	return 0;
}

static int dp_parser_gpio(struct dp_parser *parser)
{
	int i = 0;
	struct device *dev = &parser->pdev->dev;
	struct device_node *of_node = dev->of_node;
	struct dss_module_power *mp = &parser->mp[DP_CORE_PM];
	static const char * const dp_gpios[] = {
		"qcom,aux-en-gpio",
		"qcom,aux-sel-gpio",
		"qcom,usbplug-cc-gpio",
	};

	mp->gpio_config = devm_kzalloc(dev,
		sizeof(struct dss_gpio) * ARRAY_SIZE(dp_gpios), GFP_KERNEL);
	if (!mp->gpio_config)
		return -ENOMEM;

	mp->num_gpio = ARRAY_SIZE(dp_gpios);

	for (i = 0; i < mp->num_gpio; i++) {
		mp->gpio_config[i].gpio = of_get_named_gpio(of_node,
			dp_gpios[i], 0);

		if (!gpio_is_valid(mp->gpio_config[i].gpio)) {
			DRM_ERROR("%s gpio not specified\n", dp_gpios[i]);
			return -EINVAL;
		}

		strlcpy(mp->gpio_config[i].gpio_name, dp_gpios[i],
			sizeof(mp->gpio_config[i].gpio_name));

		mp->gpio_config[i].value = 0;
	}

	return 0;
}

static const char *dp_parser_supply_node_name(enum dp_pm_type module)
{
	switch (module) {
	case DP_CORE_PM:	return "qcom,core-supply-entries";
	case DP_CTRL_PM:	return "qcom,ctrl-supply-entries";
	case DP_PHY_PM:		return "qcom,phy-supply-entries";
	default:		return "???";
	}
}

static int dp_parser_get_vreg(struct dp_parser *parser,
		enum dp_pm_type module)
{
	int i = 0, rc = 0;
	u32 tmp = 0;
	const char *pm_supply_name = NULL;
	struct device_node *supply_node = NULL;
	struct device_node *of_node = parser->pdev->dev.of_node;
	struct device_node *supply_root_node = NULL;
	struct dss_module_power *mp = &parser->mp[module];

	mp->num_vreg = 0;
	pm_supply_name = dp_parser_supply_node_name(module);
	supply_root_node = of_get_child_by_name(of_node, pm_supply_name);
	if (!supply_root_node) {
		DRM_ERROR("no supply entry present: %s\n", pm_supply_name);
		return 0;
	}

	mp->num_vreg = of_get_available_child_count(supply_root_node);

	if (mp->num_vreg == 0) {
		DRM_DEBUG_DP("no vreg\n");
		return 0;
	}

	DRM_DEBUG_DP("vreg found. count=%d\n", mp->num_vreg);

	mp->vreg_config = devm_kzalloc(&parser->pdev->dev,
		sizeof(struct dss_vreg) * mp->num_vreg, GFP_KERNEL);
	if (!mp->vreg_config) {
		mp->num_vreg = 0;
		return -ENOMEM;
	}

	for_each_child_of_node(supply_root_node, supply_node) {
		const char *st = NULL;
		/* vreg-name */
		rc = of_property_read_string(supply_node,
			"qcom,supply-name", &st);
		if (rc) {
			DRM_ERROR("error reading name. rc=%d\n", rc);
			mp->num_vreg = 0;
			return rc;
		}
		snprintf(mp->vreg_config[i].vreg_name,
			ARRAY_SIZE((mp->vreg_config[i].vreg_name)), "%s", st);
		/* vreg-min-voltage */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-min-voltage", &tmp);
		if (rc) {
			DRM_ERROR("error reading min volt. rc=%d\n", rc);
			mp->num_vreg = 0;
			return rc;
		}
		mp->vreg_config[i].min_voltage = tmp;

		/* vreg-max-voltage */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-max-voltage", &tmp);
		if (rc) {
			DRM_ERROR("error reading max volt. rc=%d\n", rc);
			mp->num_vreg = 0;
			return rc;
		}
		mp->vreg_config[i].max_voltage = tmp;

		/* enable-load */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-enable-load", &tmp);
		if (rc) {
			DRM_ERROR("error reading enable load. rc=%d\n", rc);
			mp->num_vreg = 0;
			return rc;
		}
		mp->vreg_config[i].enable_load = tmp;

		/* disable-load */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-disable-load", &tmp);
		if (rc) {
			DRM_ERROR("error reading disable load. rc=%d\n", rc);
			 mp->num_vreg = 0;
			return rc;
		}
		mp->vreg_config[i].disable_load = tmp;

		DRM_DEBUG_DP("%s min=%d, max=%d, enable=%d, disable=%d\n",
			mp->vreg_config[i].vreg_name,
			mp->vreg_config[i].min_voltage,
			mp->vreg_config[i].max_voltage,
			mp->vreg_config[i].enable_load,
			mp->vreg_config[i].disable_load
			);
		++i;
	}

	return 0;
}

static int dp_parser_regulator(struct dp_parser *parser)
{
	int i, rc = 0;

	/* Parse the regulator information */
	for (i = DP_CORE_PM; i < DP_MAX_PM; i++) {
		rc = dp_parser_get_vreg(parser, i);
		if (rc) {
			DRM_ERROR("get_dt_vreg_data failed for %s. rc=%d\n",
				dp_parser_pm_name(i), rc);
			i--;
			return rc;
		}
	}

	return 0;
}

static inline bool dp_parser_check_prefix(const char *clk_prefix, const char *clk_name)
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
	if (core_clk_count <= 0) {
		DRM_ERROR("no core clocks are defined\n");
		return -EINVAL;
	}

	core_power->num_clk = core_clk_count;
	core_power->clk_config = devm_kzalloc(dev,
			sizeof(struct dss_clk) * core_power->num_clk,
			GFP_KERNEL);
	if (!core_power->clk_config) {
		return -EINVAL;
	}

	/* Initialize the CTRL power module */
	if (ctrl_clk_count <= 0) {
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

			if (!strncmp(clk_name, "ctrl_link_clk", strlen("ctrl_link_clk")) ||
			  !strncmp(clk_name, "ctrl_pixel_clk", strlen("ctrl_pixel_clk")))
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

	rc = dp_parser_regulator(parser);
	if (rc)
		return rc;

	rc = dp_parser_gpio(parser);
	if (rc) {
		DRM_ERROR("unable to parse GPIOs\n");
		return rc;
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
