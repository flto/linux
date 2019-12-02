// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2019, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm-dp] %s: " fmt, __func__

#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include "dp_power.h"

#define DP_CLIENT_NAME_SIZE	20

struct dp_power_private {
	struct dp_parser *parser;
	struct platform_device *pdev;
	struct clk *pixel_clk_rcg;
	struct clk *link_clk_src;
	struct clk *pixel_provider;
	struct clk *link_provider;

	struct dp_power dp_power;
};

static int msm_dss_init_vreg(struct device *dev, struct dss_vreg *in_vreg,
			int num_vreg)
{
	int i = 0, rc = 0;
	struct dss_vreg *curr_vreg = NULL;
	enum dss_vreg_type type;

	if (!in_vreg || !num_vreg)
		return rc;

	for (i = 0; i < num_vreg; i++) {
		curr_vreg = &in_vreg[i];
		curr_vreg->vreg = devm_regulator_get(dev,
			curr_vreg->vreg_name);
		rc = PTR_RET(curr_vreg->vreg);
		if (rc) {
			DRM_DEV_ERROR(dev, "%pS->%s: %s get failed. rc=%d\n",
				 __builtin_return_address(0), __func__,
				 curr_vreg->vreg_name, rc);
			curr_vreg->vreg = NULL;
			goto vreg_get_fail;
		}
		type = (regulator_count_voltages(curr_vreg->vreg) > 0)
				? DSS_REG_LDO : DSS_REG_VS;
		if (type == DSS_REG_LDO) {
			rc = regulator_set_voltage(
				curr_vreg->vreg,
				curr_vreg->min_voltage,
				curr_vreg->max_voltage);
			if (rc < 0) {
				DRM_DEV_ERROR(dev, "%pS->%s:%s set vltg fail\n",
					__builtin_return_address(0),
					__func__,
					curr_vreg->vreg_name);
				goto vreg_set_voltage_fail;
			}
		}
	}
	return 0;

vreg_unconfig:
if (type == DSS_REG_LDO)
	regulator_set_load(curr_vreg->vreg, 0);

vreg_set_voltage_fail:
	devm_regulator_put(curr_vreg->vreg);
	curr_vreg->vreg = NULL;

vreg_get_fail:
	for (i--; i >= 0; i--) {
		curr_vreg = &in_vreg[i];
		type = (regulator_count_voltages(curr_vreg->vreg) > 0)
			? DSS_REG_LDO : DSS_REG_VS;
		goto vreg_unconfig;
	}
	return rc;
}

static int msm_dss_deinit_vreg(struct device *dev, struct dss_vreg *in_vreg,
			int num_vreg)
{
	int i = 0, rc = 0;
	struct dss_vreg *curr_vreg = NULL;
	enum dss_vreg_type type;

	if (!in_vreg || !num_vreg)
		return rc;

	for (i = num_vreg-1; i >= 0; i--) {
		curr_vreg = &in_vreg[i];
		if (curr_vreg->vreg) {
			type = (regulator_count_voltages(
				curr_vreg->vreg) > 0)
				? DSS_REG_LDO : DSS_REG_VS;
			if (type == DSS_REG_LDO) {
				regulator_set_voltage(curr_vreg->vreg,
					0, curr_vreg->max_voltage);
			}
			devm_regulator_put(curr_vreg->vreg);
			curr_vreg->vreg = NULL;
		}
	}
	return 0;
}

static int msm_dss_enable_vreg(struct dss_vreg *in_vreg, int num_vreg, int enable)
{
	int i = 0, rc = 0;
	bool need_sleep;

	if (enable) {
		for (i = 0; i < num_vreg; i++) {
			if (IS_ERR_OR_NULL(in_vreg[i].vreg)) {
				rc = PTR_ERR(in_vreg[i].vreg);
				DRM_ERROR("%pS->%s: %s vreg error. rc=%d\n",
					__builtin_return_address(0), __func__,
					in_vreg[i].vreg_name, rc);
				goto vreg_set_opt_mode_fail;
			}
			need_sleep = !regulator_is_enabled(in_vreg[i].vreg);
			if (in_vreg[i].pre_on_sleep && need_sleep)
				usleep_range(in_vreg[i].pre_on_sleep * 1000,
					in_vreg[i].pre_on_sleep * 1000);
			rc = regulator_set_load(in_vreg[i].vreg,
				in_vreg[i].enable_load);
			if (rc < 0) {
				DRM_ERROR("%pS->%s: %s set opt m fail\n",
					__builtin_return_address(0), __func__,
					in_vreg[i].vreg_name);
				goto vreg_set_opt_mode_fail;
			}
			rc = regulator_enable(in_vreg[i].vreg);
			if (in_vreg[i].post_on_sleep && need_sleep)
				usleep_range(in_vreg[i].post_on_sleep * 1000,
					in_vreg[i].post_on_sleep * 1000);
			if (rc < 0) {
				DRM_ERROR("%pS->%s: %s enable failed\n",
					__builtin_return_address(0), __func__,
					in_vreg[i].vreg_name);
				goto disable_vreg;
			}
		}
	} else {
		for (i = num_vreg-1; i >= 0; i--) {
			if (in_vreg[i].pre_off_sleep)
				usleep_range(in_vreg[i].pre_off_sleep * 1000,
					in_vreg[i].pre_off_sleep * 1000);
			regulator_set_load(in_vreg[i].vreg,
				in_vreg[i].disable_load);
			regulator_disable(in_vreg[i].vreg);
			if (in_vreg[i].post_off_sleep)
				usleep_range(in_vreg[i].post_off_sleep * 1000,
					in_vreg[i].post_off_sleep * 1000);
		}
	}
	return rc;

disable_vreg:
	regulator_set_load(in_vreg[i].vreg, in_vreg[i].disable_load);

vreg_set_opt_mode_fail:
	for (i--; i >= 0; i--) {
		if (in_vreg[i].pre_off_sleep)
			usleep_range(in_vreg[i].pre_off_sleep * 1000,
				in_vreg[i].pre_off_sleep * 1000);
		regulator_set_load(in_vreg[i].vreg,
			in_vreg[i].disable_load);
		regulator_disable(in_vreg[i].vreg);
		if (in_vreg[i].post_off_sleep)
			usleep_range(in_vreg[i].post_off_sleep * 1000,
				in_vreg[i].post_off_sleep * 1000);
	}

	return rc;
} /* msm_dss_enable_vreg */

static int dp_power_regulator_init(struct dp_power_private *power)
{
	int rc = 0, i = 0, j = 0;
	struct platform_device *pdev;
	struct dp_parser *parser;

	parser = power->parser;
	pdev = power->pdev;

	for (i = DP_CORE_PM; !rc && (i < DP_MAX_PM); i++) {
		rc = msm_dss_init_vreg(&pdev->dev,
			parser->mp[i].vreg_config,
			parser->mp[i].num_vreg);
		if (rc) {
			DRM_ERROR("failed to init vregs for %s\n",
				dp_parser_pm_name(i));
			for (j = i - 1; j >= DP_CORE_PM; j--) {
				msm_dss_deinit_vreg(&pdev->dev,
				parser->mp[j].vreg_config,
				parser->mp[j].num_vreg);
			}

			goto error;
		}
	}
error:
	return rc;
}

static void dp_power_regulator_deinit(struct dp_power_private *power)
{
	int rc = 0, i = 0;
	struct platform_device *pdev;
	struct dp_parser *parser;

	parser = power->parser;
	pdev = power->pdev;

	for (i = DP_CORE_PM; (i < DP_MAX_PM); i++) {
		rc = msm_dss_deinit_vreg(&pdev->dev,
			parser->mp[i].vreg_config,
			parser->mp[i].num_vreg);
		if (rc)
			DRM_ERROR("failed to deinit vregs for %s\n",
				dp_parser_pm_name(i));
	}
}

static int dp_power_regulator_ctrl(struct dp_power_private *power, bool enable)
{
	int rc = 0, i = 0, j = 0;
	struct dp_parser *parser = power->parser;

	for (i = DP_CORE_PM; i < DP_MAX_PM; i++) {
		rc = msm_dss_enable_vreg(
			parser->mp[i].vreg_config,
			parser->mp[i].num_vreg, enable);
		if (rc) {
			DRM_ERROR("failed to '%s' vregs for %s\n",
					enable ? "enable" : "disable",
					dp_parser_pm_name(i));
			if (enable) {
				for (j = i-1; j >= DP_CORE_PM; j--) {
					msm_dss_enable_vreg(
					parser->mp[j].vreg_config,
					parser->mp[j].num_vreg, 0);
				}
			}
			goto error;
		}
	}
error:
	return rc;
}

static int dp_power_pinctrl_set(struct dp_power_private *power, bool active)
{
#if 0
	int rc = -EFAULT;
	struct pinctrl_state *pin_state;
	struct dp_parser *parser = power->parser;

	if (IS_ERR_OR_NULL(parser->pinctrl.pin))
		return PTR_ERR(parser->pinctrl.pin);

	pin_state = active ? parser->pinctrl.state_active
				: parser->pinctrl.state_suspend;

	rc = pinctrl_select_state(parser->pinctrl.pin, pin_state);
	if (rc) {
		DRM_ERROR("can not set %s pins\n",
			active ? "dp_active" : "dp_sleep");
		return rc;
	}
#endif
	return 0;
}

static int dp_power_clk_init(struct dp_power_private *power)
{
	int rc = 0;
	struct dss_module_power *core, *ctrl;
	struct device *dev = &power->pdev->dev;

	core = &power->parser->mp[DP_CORE_PM];
	ctrl = &power->parser->mp[DP_CTRL_PM];

	if (power->parser->pll && power->parser->pll->get_provider) {
		rc = power->parser->pll->get_provider(power->parser->pll,
				&power->link_provider, &power->pixel_provider);
		if (rc) {
			pr_info("%s: can't get provider from pll, don't set parent\n",
				__func__);
			return 0;
		}
	}

	rc = msm_dss_get_clk(dev, core->clk_config, core->num_clk);
	if (rc) {
		DRM_ERROR("failed to get %s clk. err=%d\n",
			dp_parser_pm_name(DP_CORE_PM), rc);
		return rc;
	}

	rc = msm_dss_get_clk(dev, ctrl->clk_config, ctrl->num_clk);
	if (rc) {
		DRM_ERROR("failed to get %s clk. err=%d\n",
			dp_parser_pm_name(DP_CTRL_PM), rc);
		msm_dss_put_clk(core->clk_config, core->num_clk);
		return -ENODEV;
	}

	power->pixel_clk_rcg = devm_clk_get(dev, "pixel_clk_rcg");
	if (IS_ERR(power->pixel_clk_rcg)) {
		DRM_DEBUG_DP("Unable to get DP pixel clk RCG\n");
		power->pixel_clk_rcg = NULL;
		msm_dss_put_clk(core->clk_config, core->num_clk);
		return -ENODEV;
	}

	return 0;
}

static int dp_power_clk_deinit(struct dp_power_private *power)
{
	struct dss_module_power *core, *ctrl;

	core = &power->parser->mp[DP_CORE_PM];
	ctrl = &power->parser->mp[DP_CTRL_PM];

	if (!core || !ctrl) {
		DRM_ERROR("invalid power_data\n");
		return -EINVAL;
	}

	msm_dss_put_clk(ctrl->clk_config, ctrl->num_clk);
	msm_dss_put_clk(core->clk_config, core->num_clk);
	return 0;
}

static int dp_power_clk_set_rate(struct dp_power_private *power,
		enum dp_pm_type module, bool enable)
{
	int rc = 0;
	struct dss_module_power *mp = &power->parser->mp[module];

	if (enable) {
		rc = msm_dss_clk_set_rate(mp->clk_config, mp->num_clk);
		if (rc) {
			DRM_ERROR("failed to set clks rate.\n");
			return rc;
		}
	}

	rc = msm_dss_enable_clk(mp->clk_config, mp->num_clk, enable);
	if (rc) {
		DRM_ERROR("failed to %d clks, err: %d\n", enable, rc);
		return rc;
	}

	return 0;
}

int dp_power_clk_enable(struct dp_power *dp_power,
		enum dp_pm_type pm_type, bool enable)
{
	int rc = 0;
	struct dss_module_power *mp;
	struct dp_power_private *power;

	power = container_of(dp_power, struct dp_power_private, dp_power);
	mp = &power->parser->mp[pm_type];

	if (pm_type != DP_CORE_PM && pm_type != DP_CTRL_PM) {
		DRM_ERROR("unsupported power module: %s\n",
				dp_parser_pm_name(pm_type));
		return -EINVAL;
	}

	if (enable) {
		if (pm_type == DP_CORE_PM && dp_power->core_clks_on) {
			DRM_DEBUG_DP("core clks already enabled\n");
			return 0;
		}

		if (pm_type == DP_CTRL_PM && dp_power->link_clks_on) {
			DRM_DEBUG_DP("links clks already enabled\n");
			return 0;
		}

		if ((pm_type == DP_CTRL_PM) && (!dp_power->core_clks_on)) {
			DRM_DEBUG_DP("Need to enable core clks before link clks\n");

			rc = dp_power_clk_set_rate(power, DP_CORE_PM, enable);
			if (rc) {
				DRM_ERROR("failed to enable clks: %s. err=%d\n",
					dp_parser_pm_name(DP_CORE_PM), rc);
				return rc;
			}
			dp_power->core_clks_on = true;
		}
	}

	rc = dp_power_clk_set_rate(power, pm_type, enable);
	if (rc) {
		DRM_ERROR("failed to '%s' clks for: %s. err=%d\n",
			enable ? "enable" : "disable",
			dp_parser_pm_name(pm_type), rc);
			return rc;
	}

	if (pm_type == DP_CORE_PM)
		dp_power->core_clks_on = enable;
	else
		dp_power->link_clks_on = enable;

	DRM_DEBUG_DP("%s clocks for %s\n",
			enable ? "enable" : "disable",
			dp_parser_pm_name(pm_type));
	DRM_DEBUG_DP("link_clks:%s core_clks:%s\n",
		dp_power->link_clks_on ? "on" : "off",
		dp_power->core_clks_on ? "on" : "off");

	return 0;
}

static int dp_power_request_gpios(struct dp_power_private *power)
{
#if 0
	int rc = 0, i;
	struct device *dev = &power->pdev->dev;
	struct dss_module_power *mp = &power->parser->mp[DP_CORE_PM];
	static const char * const gpio_names[] = {
		"aux_enable", "aux_sel", "usbplug_cc",
	};

	if (!power) {
		DRM_ERROR("invalid power data\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(gpio_names); i++) {
		unsigned int gpio = mp->gpio_config[i].gpio;

		if (!gpio_is_valid(gpio))
			continue;

		rc = devm_gpio_request(dev, gpio, gpio_names[i]);
		if (rc) {
			DRM_ERROR("request %s gpio failed, rc=%d\n",
				       gpio_names[i], rc);
			return rc;
		}
	}
#endif
	return 0;
}

static bool dp_power_find_gpio(const char *gpio1, const char *gpio2)
{
	return !!strnstr(gpio1, gpio2, strlen(gpio1));
}

static void dp_power_set_gpio(struct dp_power_private *power, bool flip)
{
#if 0
	int i;
	struct dss_module_power *mp = &power->parser->mp[DP_CORE_PM];
	struct dss_gpio *config = mp->gpio_config;

	for (i = 0; i < mp->num_gpio; i++) {
		if (dp_power_find_gpio(config->gpio_name, "aux-sel"))
			config->value = flip;

		if (gpio_is_valid(config->gpio)) {
			DRM_DEBUG_DP("gpio %s, value %d\n", config->gpio_name,
				config->value);

			if (dp_power_find_gpio(config->gpio_name, "aux-en") ||
			    dp_power_find_gpio(config->gpio_name, "aux-sel"))
				gpio_direction_output(config->gpio,
					config->value);
			else
				gpio_set_value(config->gpio, config->value);

		}
		config++;
	}
#endif
}

static int dp_power_config_gpios(struct dp_power_private *power, bool flip,
					bool enable)
{
#if 0
	int rc = 0, i;
	struct dss_module_power *mp = &power->parser->mp[DP_CORE_PM];
	struct dss_gpio *config = mp->gpio_config;

	mp = &power->parser->mp[DP_CORE_PM];
	config = mp->gpio_config;

	if (enable) {
		rc = dp_power_request_gpios(power);
		if (rc) {
			DRM_ERROR("gpio request failed\n");
			return rc;
		}

		dp_power_set_gpio(power, flip);
	} else {
		for (i = 0; i < mp->num_gpio; i++) {
			gpio_set_value(config[i].gpio, 0);
			gpio_free(config[i].gpio);
		}
	}
#endif
	return 0;
}

int dp_power_client_init(struct dp_power *dp_power)
{
	int rc = 0;
	struct dp_power_private *power;

	if (!dp_power) {
		DRM_ERROR("invalid power data\n");
		return -EINVAL;
	}

	power = container_of(dp_power, struct dp_power_private, dp_power);

	pm_runtime_enable(&power->pdev->dev);

	rc = dp_power_regulator_init(power);
	if (rc) {
		DRM_ERROR("failed to init regulators %d\n", rc);
		goto error_power;
	}

	rc = dp_power_clk_init(power);
	if (rc) {
		DRM_ERROR("failed to init clocks %d\n", rc);
		goto error_clk;
	}
	return 0;

error_clk:
	dp_power_regulator_deinit(power);
error_power:
	pm_runtime_disable(&power->pdev->dev);
	return rc;
}

void dp_power_client_deinit(struct dp_power *dp_power)
{
	struct dp_power_private *power;

	if (!dp_power) {
		DRM_ERROR("invalid power data\n");
		return;
	}

	power = container_of(dp_power, struct dp_power_private, dp_power);

	dp_power_clk_deinit(power);
	dp_power_regulator_deinit(power);
	pm_runtime_disable(&power->pdev->dev);

}

int dp_power_set_link_clk_parent(struct dp_power *dp_power)
{
	int rc = 0;
	struct dp_power_private *power;
	u32 num;
	struct dss_clk *cfg;
	char *name = "ctrl_link_clk";

	if (!dp_power) {
		DRM_ERROR("invalid power data\n");
		rc = -EINVAL;
		goto exit;
	}

	power = container_of(dp_power, struct dp_power_private, dp_power);

	num = power->parser->mp[DP_CTRL_PM].num_clk;
	cfg = power->parser->mp[DP_CTRL_PM].clk_config;

	while (num && strcmp(cfg->clk_name, name)) {
		num--;
		cfg++;
	}

	if (num && power->link_provider) {
		power->link_clk_src = clk_get_parent(cfg->clk);
			if (power->link_clk_src) {
				clk_set_parent(power->link_clk_src, power->link_provider);
				DRM_DEBUG_DP("%s: is the parent of clk=%s\n",
						__clk_get_name(power->link_provider),
						__clk_get_name(power->link_clk_src));
			} else {
				DRM_ERROR("couldn't get parent for clk=%s\n", name);
				rc = -EINVAL;
			}
	} else {
		DRM_ERROR("%s clock could not be set parent\n", name);
		rc = -EINVAL;
	}
exit:
	return rc;
}

int dp_power_set_pixel_clk_parent(struct dp_power *dp_power)
{
	int rc = 0;
	struct dp_power_private *power;

	power = container_of(dp_power, struct dp_power_private, dp_power);

	if (power->pixel_clk_rcg && power->pixel_provider) {
		rc = clk_set_parent(power->pixel_clk_rcg, power->pixel_provider);
		if (rc) {
			DRM_ERROR("failed to set parent clk src, %d\n", rc);
			return rc;
		}
		DRM_DEBUG_DP("%s: is the parent of clk=%s\n",
					__clk_get_name(power->pixel_provider),
					__clk_get_name(power->pixel_clk_rcg));
	}

	return 0;
}

int dp_power_init(struct dp_power *dp_power, bool flip)
{
	int rc = 0;
	struct dp_power_private *power;

	if (!dp_power) {
		DRM_ERROR("invalid power data\n");
		rc = -EINVAL;
		goto exit;
	}

	power = container_of(dp_power, struct dp_power_private, dp_power);

	pm_runtime_get_sync(&power->pdev->dev);
	rc = dp_power_regulator_ctrl(power, true);
	if (rc) {
		DRM_ERROR("failed to enable regulators, %d\n", rc);
		goto exit;
	}

	rc = dp_power_pinctrl_set(power, true);
	if (rc) {
		DRM_ERROR("failed to set pinctrl state, %d\n", rc);
		goto err_pinctrl;
	}

	rc = dp_power_config_gpios(power, flip, true);
	if (rc) {
		DRM_ERROR("failed to enable gpios, %d\n", rc);
		goto err_gpio;
	}

	rc = dp_power_clk_enable(dp_power, DP_CORE_PM, true);
	if (rc) {
		DRM_ERROR("failed to enable DP core clocks, %d\n", rc);
		goto err_clk;
	}

	return 0;

err_clk:
	dp_power_config_gpios(power, flip, false);
err_gpio:
	dp_power_pinctrl_set(power, false);
err_pinctrl:
	dp_power_regulator_ctrl(power, false);
exit:
	pm_runtime_put_sync(&power->pdev->dev);
	return rc;
}

int dp_power_deinit(struct dp_power *dp_power)
{
	struct dp_power_private *power;

	power = container_of(dp_power, struct dp_power_private, dp_power);

	dp_power_clk_enable(dp_power, DP_CORE_PM, false);
	dp_power_config_gpios(power, false, false);
	dp_power_pinctrl_set(power, false);
	dp_power_regulator_ctrl(power, false);
	pm_runtime_put_sync(&power->pdev->dev);
	return 0;
}

struct dp_power *dp_power_get(struct dp_parser *parser)
{
	int rc = 0;
	struct dp_power_private *power;
	struct dp_power *dp_power;

	if (!parser) {
		DRM_ERROR("invalid input\n");
		rc = -EINVAL;
		goto error;
	}

	power = devm_kzalloc(&parser->pdev->dev, sizeof(*power), GFP_KERNEL);
	if (!power) {
		rc = -ENOMEM;
		goto error;
	}

	power->parser = parser;
	power->pdev = parser->pdev;

	dp_power = &power->dp_power;

	return dp_power;
error:
	return ERR_PTR(rc);
}

void dp_power_put(struct dp_power *dp_power)
{
	struct dp_power_private *power = NULL;

	if (!dp_power)
		return;

	power = container_of(dp_power, struct dp_power_private, dp_power);

	devm_kfree(&power->pdev->dev, power);
}
