// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 */

/*
 * Display Port PLL driver block diagram for branch clocks
 *
 *		+------------------------------+
 *		|         DP_VCO_CLK           |
 *		|                              |
 *		|    +-------------------+     |
 *		|    |   (DP PLL/VCO)    |     |
 *		|    +---------+---------+     |
 *		|              v               |
 *		|   +----------+-----------+   |
 *		|   | hsclk_divsel_clk_src |   |
 *		|   +----------+-----------+   |
 *		+------------------------------+
 *				|
 *	 +------------<---------v------------>----------+
 *	 |                                              |
 * +-----v------------+                                 |
 * | dp_link_clk_src  |                                 |
 * |    divsel_ten    |                                 |
 * +---------+--------+                                 |
 *	|                                               |
 *	|                                               |
 *	v                                               v
 * Input to DISPCC block                                |
 * for link clk, crypto clk                             |
 * and interface clock                                  |
 *							|
 *							|
 *	+--------<------------+-----------------+---<---+
 *	|                     |                 |
 * +-------v------+  +--------v-----+  +--------v------+
 * | vco_divided  |  | vco_divided  |  | vco_divided   |
 * |    _clk_src  |  |    _clk_src  |  |    _clk_src   |
 * |              |  |              |  |               |
 * |divsel_six    |  |  divsel_two  |  |  divsel_four  |
 * +-------+------+  +-----+--------+  +--------+------+
 *         |	           |		        |
 *	v------->----------v-------------<------v
 *                         |
 *		+----------+---------+
 *		|   vco_divided_clk  |
 *		|       _src_mux     |
 *		+---------+----------+
 *                        |
 *                        v
 *              Input to DISPCC block
 *              for DP pixel clock
 *
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/clk.h>

#include "dp_pll_10nm.h"

#define NUM_PROVIDED_CLKS		2

#define DP_LINK_CLK_SRC			0
#define DP_PIXEL_CLK_SRC		1

static struct dp_pll_10nm *dp_pdb;

/* Op structures */
static const struct clk_ops dp_10nm_vco_clk_ops = {
	.recalc_rate = dp_vco_recalc_rate_10nm,
	.set_rate = dp_vco_set_rate_10nm,
	.round_rate = dp_vco_round_rate_10nm,
	.prepare = dp_vco_prepare_10nm,
	.unprepare = dp_vco_unprepare_10nm,
};

struct dp_pll_10nm_pclksel {
	struct clk_hw hw;

	/* divider params */
	u8 shift;
	u8 width;
	u8 flags; /* same flags as used by clk_divider struct */

	struct dp_pll_10nm *pll;
};
#define to_pll_10nm_pclksel(_hw) container_of(_hw, struct dp_pll_10nm_pclksel, hw)

static int dp_mux_set_parent_10nm(struct clk_hw *hw, u8 val)
{
	struct dp_pll_10nm_pclksel *pclksel = to_pll_10nm_pclksel(hw);
	struct dp_pll_10nm *dp_res = pclksel->pll;
	u32 auxclk_div;

	auxclk_div = PLL_REG_R(dp_res->phy_base, REG_DP_PHY_VCO_DIV);
	auxclk_div &= ~0x03;	/* bits 0 to 1 */

	if (val == 0) /* mux parent index = 0 */
		auxclk_div |= 1;
	else if (val == 1) /* mux parent index = 1 */
		auxclk_div |= 2;
	else if (val == 2) /* mux parent index = 2 */
		auxclk_div |= 0;

	PLL_REG_W(dp_res->phy_base,
			REG_DP_PHY_VCO_DIV, auxclk_div);
	/* Make sure the PHY registers writes are done */
	wmb();
	DRM_DEBUG_DP("%s: mux=%d auxclk_div=%x\n", __func__, val, auxclk_div);

	return 0;
}

static u8 dp_mux_get_parent_10nm(struct clk_hw *hw)
{
	u32 auxclk_div = 0;
	struct dp_pll_10nm_pclksel *pclksel = to_pll_10nm_pclksel(hw);
	struct dp_pll_10nm *dp_res = pclksel->pll;
	u8 val = 0;

	DRM_ERROR("clk_hw->init->name = %s\n", hw->init->name);
	auxclk_div = PLL_REG_R(dp_res->phy_base, REG_DP_PHY_VCO_DIV);
	auxclk_div &= 0x03;

	if (auxclk_div == 1) /* Default divider */
		val = 0;
	else if (auxclk_div == 2)
		val = 1;
	else if (auxclk_div == 0)
		val = 2;

	DRM_DEBUG_DP("%s: auxclk_div=%d, val=%d\n", __func__, auxclk_div, val);

	return val;
}

static int clk_mux_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	int ret = 0;

	ret = __clk_mux_determine_rate_closest(hw, req);
	if (ret)
		return ret;

	/* Set the new parent of mux if there is a new valid parent */
	if (hw->clk && req->best_parent_hw->clk)
		clk_set_parent(hw->clk, req->best_parent_hw->clk);

	return 0;
}

static unsigned long mux_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct clk *div_clk = NULL, *vco_clk = NULL;
	struct msm_dp_pll *vco = NULL;

	div_clk = clk_get_parent(hw->clk);
	if (!div_clk)
		return 0;

	vco_clk = clk_get_parent(div_clk);
	if (!vco_clk)
		return 0;

	vco = hw_clk_to_pll(__clk_get_hw(vco_clk));
	if (!vco)
		return 0;

	if (vco->rate == DP_VCO_HSCLK_RATE_8100MHZDIV1000)
		return (vco->rate / 6);
	else if (vco->rate == DP_VCO_HSCLK_RATE_5400MHZDIV1000)
		return (vco->rate / 4);
	else
		return (vco->rate / 2);
}

static int dp_pll_10nm_get_provider(struct msm_dp_pll *pll,
				     struct clk **link_clk_provider,
				     struct clk **pixel_clk_provider)
{
	struct dp_pll_10nm *pll_10nm = to_dp_pll_10nm(pll);
	struct clk_hw_onecell_data *hw_data = pll_10nm->hw_data;

	if (link_clk_provider)
		*link_clk_provider = hw_data->hws[DP_LINK_CLK_SRC]->clk;
	if (pixel_clk_provider)
		*pixel_clk_provider = hw_data->hws[DP_PIXEL_CLK_SRC]->clk;

	return 0;
}

static const struct clk_ops dp_10nm_pclksel_clk_ops = {
	.get_parent = dp_mux_get_parent_10nm,
	.set_parent = dp_mux_set_parent_10nm,
	.recalc_rate = mux_recalc_rate,
	.determine_rate = clk_mux_determine_rate,
};

static struct clk_hw *dp_pll_10nm_pixel_clk_sel(struct dp_pll_10nm *pll_10nm)
{
	struct device *dev = &pll_10nm->pdev->dev;
	struct dp_pll_10nm_pclksel *pll_pclksel;
	struct clk_init_data pclksel_init = {
		.parent_names = (const char *[]){
				"dp_vco_divsel_two_clk_src",
				"dp_vco_divsel_four_clk_src",
				"dp_vco_divsel_six_clk_src" },
		.num_parents = 3,
		.name = "dp_vco_divided_clk_src_mux",
		.flags = CLK_IGNORE_UNUSED,
		.ops = &dp_10nm_pclksel_clk_ops,
	};
	int ret;

	pll_pclksel = devm_kzalloc(dev, sizeof(*pll_pclksel), GFP_KERNEL);
	if (!pll_pclksel)
		return ERR_PTR(-ENOMEM);

	pll_pclksel->pll = pll_10nm;
	pll_pclksel->shift = 0;
	pll_pclksel->width = 4;
	pll_pclksel->flags = CLK_DIVIDER_ONE_BASED;
	pll_pclksel->hw.init = &pclksel_init;

	ret = clk_hw_register(dev, &pll_pclksel->hw);
	if (ret)
		return ERR_PTR(ret);

	return &pll_pclksel->hw;
}

static int dp_pll_10nm_register(struct dp_pll_10nm *pll_10nm)
{
	char clk_name[32], parent[32], vco_name[32];
	struct clk_init_data vco_init = {
		.parent_names = (const char *[]){ "bi_tcxo" },
		.num_parents = 1,
		.name = vco_name,
		.flags = CLK_IGNORE_UNUSED,
		.ops = &dp_10nm_vco_clk_ops,
	};
	struct device *dev = &pll_10nm->pdev->dev;
	struct clk_hw **hws = pll_10nm->hws;
	struct clk_hw_onecell_data *hw_data;
	struct clk_hw *hw;
	int num = 0;
	int ret;

	DBG("DP->id = %d", pll_10nm->id);

	hw_data = devm_kzalloc(dev, sizeof(*hw_data) +
			       NUM_PROVIDED_CLKS * sizeof(struct clk_hw *),
			       GFP_KERNEL);
	if (!hw_data)
		return -ENOMEM;

	snprintf(vco_name, 32, "dp_vco_clk");
	pll_10nm->base.clk_hw.init = &vco_init;
	ret = clk_hw_register(dev, &pll_10nm->base.clk_hw);
	if (ret)
		return ret;
	hws[num++] = &pll_10nm->base.clk_hw;

	snprintf(clk_name, 32, "dp_link_clk_divsel_ten");
	snprintf(parent, 32, "dp_vco_clk");
	hw = clk_hw_register_fixed_factor(dev, clk_name, parent,
					  CLK_SET_RATE_PARENT, 1, 10);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	hws[num++] = hw;
	hw_data->hws[DP_LINK_CLK_SRC] = hw;

	snprintf(clk_name, 32, "dp_vco_divsel_two_clk_src");
	snprintf(parent, 32, "dp_vco_clk");
	hw = clk_hw_register_fixed_factor(dev, clk_name, parent,
					  0, 1, 2);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	hws[num++] = hw;

	snprintf(clk_name, 32, "dp_vco_divsel_four_clk_src");
	snprintf(parent, 32, "dp_vco_clk");
	hw = clk_hw_register_fixed_factor(dev, clk_name, parent,
					  0, 1, 4);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	hws[num++] = hw;

	snprintf(clk_name, 32, "dp_vco_divsel_six_clk_src");
	snprintf(parent, 32, "dp_vco_clk");
	hw = clk_hw_register_fixed_factor(dev, clk_name, parent,
					  0, 1, 6);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	hws[num++] = hw;

	hw = dp_pll_10nm_pixel_clk_sel(pll_10nm);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	hws[num++] = hw;
	hw_data->hws[DP_PIXEL_CLK_SRC] = hw;

	pll_10nm->num_hws = num;

	hw_data->num = NUM_PROVIDED_CLKS;
	pll_10nm->hw_data = hw_data;

	ret = of_clk_add_hw_provider(dev->of_node, of_clk_hw_onecell_get,
				     pll_10nm->hw_data);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to register clk provider: %d\n", ret);
		return ret;
	}

	return ret;
}

struct msm_dp_pll *msm_dp_pll_10nm_init(struct platform_device *pdev, int id)
{
	struct dp_pll_10nm *dp_10nm_pll;
	struct msm_dp_pll *pll;
	int ret;

	if (!pdev)
		return ERR_PTR(-ENODEV);

	dp_10nm_pll = devm_kzalloc(&pdev->dev, sizeof(*dp_10nm_pll), GFP_KERNEL);
	if (!dp_10nm_pll)
		return ERR_PTR(-ENOMEM);

	DBG("DP PLL%d", id);

	dp_10nm_pll->pdev = pdev;
	dp_10nm_pll->id = id;
	dp_pdb = dp_10nm_pll;

	dp_10nm_pll->pll_base = msm_ioremap(pdev, "pll_base", "DP_PLL");
	if (IS_ERR_OR_NULL(dp_10nm_pll->pll_base)) {
		DRM_DEV_ERROR(&pdev->dev, "failed to map CMN PLL base\n");
		return ERR_PTR(-ENOMEM);
	}

	dp_10nm_pll->phy_base = msm_ioremap(pdev, "phy_base", "DP_PHY");
	if (IS_ERR_OR_NULL(dp_10nm_pll->phy_base)) {
		DRM_DEV_ERROR(&pdev->dev, "failed to map CMN PHY base\n");
		return ERR_PTR(-ENOMEM);
	}

	dp_10nm_pll->ln_tx0_base = msm_ioremap(pdev, "ln_tx0_base", "DP_LN_TX0");
	if (IS_ERR_OR_NULL(dp_10nm_pll->ln_tx0_base)) {
		DRM_DEV_ERROR(&pdev->dev, "failed to map CMN LN_TX0 base\n");
		return ERR_PTR(-ENOMEM);
	}

	dp_10nm_pll->ln_tx1_base = msm_ioremap(pdev, "ln_tx1_base", "DP_LN_TX1");
	if (IS_ERR_OR_NULL(dp_10nm_pll->ln_tx1_base)) {
		DRM_DEV_ERROR(&pdev->dev, "failed to map CMN LN_TX1 base\n");
		return ERR_PTR(-ENOMEM);
	}

	ret = of_property_read_u32(pdev->dev.of_node, "cell-index",
				&dp_10nm_pll->index);
	if (ret) {
		DRM_ERROR("Unable to get the cell-index ret=%d\n", ret);
		dp_10nm_pll->index = 0;
	}

	ret = msm_dp_pll_util_parse_dt_clock(pdev, &dp_10nm_pll->base);
	if (ret) {
		DRM_ERROR("Unable to parse dt clocks ret=%d\n", ret);
		return ERR_PTR(ret);
	}

	ret = dp_pll_10nm_register(dp_10nm_pll);
	if (ret) {
		DRM_DEV_ERROR(&pdev->dev, "failed to register PLL: %d\n", ret);
		return ERR_PTR(ret);
	}

	pll = &dp_10nm_pll->base;
	pll->min_rate = DP_VCO_HSCLK_RATE_1620MHZDIV1000;
	pll->max_rate = DP_VCO_HSCLK_RATE_8100MHZDIV1000;
	pll->get_provider = dp_pll_10nm_get_provider;

	return pll;
}
