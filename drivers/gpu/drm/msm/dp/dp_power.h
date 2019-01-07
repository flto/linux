/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2019, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_POWER_H_
#define _DP_POWER_H_

#include "dp_parser.h"

/**
 * sruct dp_power - DisplayPort's power related data
 *
 * @init: initializes the regulators/core clocks/GPIOs/pinctrl
 * @deinit: turns off the regulators/core clocks/GPIOs/pinctrl
 * @clk_enable: enable/disable the DP clocks
 * @set_pixel_clk_parent: set the parent of DP pixel clock
 */
struct dp_power {
	bool core_clks_on;
	bool link_clks_on;
};

int dp_power_init(struct dp_power *power, bool flip);
int dp_power_deinit(struct dp_power *power);
int dp_power_clk_enable(struct dp_power *power, enum dp_pm_type pm_type,
				bool enable);
int dp_power_set_link_clk_parent(struct dp_power *power);
int dp_power_set_pixel_clk_parent(struct dp_power *power);
int dp_power_client_init(struct dp_power *power);
void dp_power_client_deinit(struct dp_power *power);

/**
 * dp_power_get() - configure and get the DisplayPort power module data
 *
 * @parser: instance of parser module
 * return: pointer to allocated power module data
 *
 * This API will configure the DisplayPort's power module and provides
 * methods to be called by the client to configure the power related
 * modueles.
 */
struct dp_power *dp_power_get(struct dp_parser *parser);

/**
 * dp_power_put() - release the power related resources
 *
 * @power: pointer to the power module's data
 */
void dp_power_put(struct dp_power *power);
#endif /* _DP_POWER_H_ */
