/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 */

#ifndef __DP_PLL_H
#define __DP_PLL_H

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "dpu_io_util.h"
#include "msm_drv.h"

#define PLL_REG_W(base, offset, data)	\
				writel_relaxed((data), (base) + (offset))
#define PLL_REG_R(base, offset)	readl_relaxed((base) + (offset))

enum msm_dp_pll_type {
	MSM_DP_PLL_10NM,
	MSM_DP_PLL_MAX
};

struct msm_dp_pll {
	enum msm_dp_pll_type type;
	struct clk_hw clk_hw;
	unsigned long	rate;		/* current vco rate */
	u64		min_rate;	/* min vco rate */
	u64		max_rate;	/* max vco rate */
	bool		pll_on;
	void		*priv;
	/* Pll specific resources like GPIO, power supply, clocks, etc*/
	struct dss_module_power mp;
	int (*get_provider)(struct msm_dp_pll *pll,
			struct clk **link_clk_provider,
			struct clk **pixel_clk_provider);
};

#define hw_clk_to_pll(x) container_of(x, struct msm_dp_pll, clk_hw)

struct msm_dp_pll *msm_dp_pll_init(struct platform_device *pdev,
			enum msm_dp_pll_type type, int id);

int msm_dp_pll_util_parse_dt_clock(struct platform_device *pdev,
					struct msm_dp_pll *pll);

#ifdef CONFIG_DRM_MSM_DP_10NM_PLL
struct msm_dp_pll *msm_dp_pll_10nm_init(struct platform_device *pdev, int id);
#else
struct msm_dp_pll *msm_dp_pll_10nm_init(struct platform_device *pdev, int id)
{
	return ERR_PTR(-ENODEV);
}
#endif
#endif /* __DP_PLL_H */
