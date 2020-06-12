// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2011-2017, The Linux Foundation. All rights reserved.
// Copyright (c) 2018, Linaro Limited

#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include "q6afe.h"

struct q6afe_clk_data {
	struct q6afe *afe;
	struct clk *clk;
};

int q6afe_vote_lpass_core_hw(struct q6afe *afe, u32 hw_block_id);
int q6afe_set_lpass_clk(struct q6afe *afe, u32 clk_id, u32 freq);

static int audio_ext_clk_prepare(struct clk_hw *hw)
{
	printk("audio_ext_clk_prepare\n");
	return 0;
}

static void audio_ext_clk_unprepare(struct clk_hw *hw)
{
}

static u8 audio_ext_clk_get_parent(struct clk_hw *hw)
{
	return 0;
}

static const struct clk_ops audio_clk_ops = {
	.prepare = audio_ext_clk_prepare,
	.unprepare = audio_ext_clk_unprepare,
	.get_parent = audio_ext_clk_get_parent,
};

static struct clk_hw clk_hw = {
	.init = &(struct clk_init_data){
		.name = "audio_clk_dummy",
		.parent_names = NULL,
		.num_parents = 0,
		.ops = &audio_clk_ops,
	},
};

// clk_hw_onecell_data
struct {
	unsigned int num;
	struct clk_hw *hws[1];
} onecell_data = {
	.num = 1,
	.hws[0] = &clk_hw
};

#include <linux/delay.h>

static int q6afe_clk_dev_probe(struct platform_device *pdev)
{
	struct q6afe_clk_data *clk_data;
	struct device *dev = &pdev->dev;
	int ret;

	printk("q6afe_clk_dev_probe 0\n");

	clk_data = devm_kzalloc(dev, sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	dev_set_drvdata(dev, clk_data);

	clk_data->afe = dev_get_drvdata(dev->parent);

	msleep(1000);

	q6afe_vote_lpass_core_hw(clk_data->afe, 4); // AFE_LPASS_CORE_HW_DCODEC_BLOCK
	q6afe_vote_lpass_core_hw(clk_data->afe, 3); // AFE_LPASS_CORE_HW_MACRO_BLOCK

	// tx
	q6afe_set_lpass_clk(clk_data->afe, 0x30c, 19200000);
	q6afe_set_lpass_clk(clk_data->afe, 0x30d, 19200000);

	// wsa
	q6afe_set_lpass_clk(clk_data->afe, 0x309, 19200000);
	q6afe_set_lpass_clk(clk_data->afe, 0x30a, 19200000);

	// rx
	q6afe_set_lpass_clk(clk_data->afe, 0x30e, 22579200);
	q6afe_set_lpass_clk(clk_data->afe, 0x30f, 22579200);

	// tx
	q6afe_set_lpass_clk(clk_data->afe, 0x30c, 19200000);
	q6afe_set_lpass_clk(clk_data->afe, 0x30d, 19200000);

	// va
	q6afe_set_lpass_clk(clk_data->afe, 0x30b, 19200000);
	q6afe_set_lpass_clk(clk_data->afe, 0x310, 19200000);

	ret = devm_clk_hw_register(&pdev->dev, &clk_hw);
	if (ret) {
		dev_err(dev, "failed to register afe clk %d\n", ret);
		return ret;
	}

	ret = of_clk_add_hw_provider(pdev->dev.of_node, of_clk_hw_onecell_get, &onecell_data);
	if (ret) {
		dev_err(dev, "failed to register afe provider %d\n", ret);
		return ret;
	}

	printk("q6afe_clk_dev_probe 1\n");

	return 0;
}

static const struct of_device_id q6afe_clk_device_id[] = {
	{ .compatible = "qcom,q6afe-clocks" },
	{},
};
MODULE_DEVICE_TABLE(of, q6afe_clk_device_id);

static struct platform_driver q6afe_clk_platform_driver = {
	.driver = {
		.name = "q6afe-clk",
		.of_match_table = of_match_ptr(q6afe_clk_device_id),
	},
	.probe = q6afe_clk_dev_probe,
};
module_platform_driver(q6afe_clk_platform_driver);

MODULE_DESCRIPTION("Q6 Audio Frontend clk driver");
MODULE_LICENSE("GPL v2");
