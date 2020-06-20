// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <uapi/linux/input-event-codes.h>
#include <dt-bindings/sound/apq8016-lpass.h>
#include "qdsp6/q6afe.h"

struct apq8016_sbc_data {
	void __iomem *mic_iomux;
	void __iomem *spkr_iomux;
	struct snd_soc_jack jack;
	bool jack_setup;
	struct snd_soc_dai_link dai_link[];	/* dynamically allocated */
};

#define MIC_CTRL_TER_WS_SLAVE_SEL	BIT(21)
#define MIC_CTRL_QUA_WS_SLAVE_SEL_10	BIT(17)
#define MIC_CTRL_TLMM_SCLK_EN		BIT(1)
#define	SPKR_CTL_PRI_WS_SLAVE_SEL_11	(BIT(17) | BIT(16))
#define DEFAULT_MCLK_RATE		9600000
#define DEFAULT_BCLK_RATE		1536000

static int apq8016_sbc_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai;
	struct snd_soc_component *component;
	struct snd_soc_card *card = rtd->card;
	struct apq8016_sbc_data *pdata = snd_soc_card_get_drvdata(card);
	int i, rval;

	switch (cpu_dai->id) {
	case PRIMARY_MI2S_RX:
		writel(readl(pdata->spkr_iomux) | SPKR_CTL_PRI_WS_SLAVE_SEL_11,
			pdata->spkr_iomux);

		snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_INTERNAL_DIGITAL_CODEC_CORE,
				DEFAULT_MCLK_RATE, SNDRV_PCM_STREAM_PLAYBACK);
		snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_PRI_MI2S_IBIT,
				DEFAULT_BCLK_RATE, SNDRV_PCM_STREAM_PLAYBACK);
		break;

	case TERTIARY_MI2S_TX:
		writel(readl(pdata->mic_iomux) | MIC_CTRL_TER_WS_SLAVE_SEL |
			0, //MIC_CTRL_TLMM_SCLK_EN,
			pdata->mic_iomux);

		// normal: 0x00200000
		// quat: | 0x02020002
		// aux: | 0x2

		snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_INTERNAL_DIGITAL_CODEC_CORE,
				DEFAULT_MCLK_RATE, SNDRV_PCM_STREAM_CAPTURE);
		snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_TER_MI2S_IBIT,
				DEFAULT_BCLK_RATE, SNDRV_PCM_STREAM_CAPTURE);

		break;
	default:
		dev_err(card->dev, "unsupported cpu dai configuration\n");
		return 0;

	}

	snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_CBS_CFS);

	if (!pdata->jack_setup) {
		struct snd_jack *jack;

		rval = snd_soc_card_jack_new(card, "Headset Jack",
					     SND_JACK_HEADSET |
					     SND_JACK_HEADPHONE |
					     SND_JACK_BTN_0 | SND_JACK_BTN_1 |
					     SND_JACK_BTN_2 | SND_JACK_BTN_3 |
					     SND_JACK_BTN_4,
					     &pdata->jack, NULL, 0);

		if (rval < 0) {
			dev_err(card->dev, "Unable to add Headphone Jack\n");
			return rval;
		}

		jack = pdata->jack.jack;

		snd_jack_set_key(jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
		snd_jack_set_key(jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
		snd_jack_set_key(jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
		snd_jack_set_key(jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);
		pdata->jack_setup = true;
	}

	for_each_rtd_codec_dais(rtd, i, codec_dai) {

		component = codec_dai->component;
		/* Set default mclk for internal codec */
		rval = snd_soc_component_set_sysclk(component, 0, 0, DEFAULT_MCLK_RATE,
				       SND_SOC_CLOCK_IN);
		if (rval != 0 && rval != -ENOTSUPP) {
			dev_warn(card->dev, "Failed to set mclk: %d\n", rval);
			return rval;
		}
		rval = snd_soc_component_set_jack(component, &pdata->jack, NULL);
		if (rval != 0 && rval != -ENOTSUPP) {
			dev_warn(card->dev, "Failed to set jack: %d\n", rval);
			return rval;
		}
	}

	return 0;
}

static struct apq8016_sbc_data *apq8016_sbc_parse_of(struct snd_soc_card *card)
{
	struct device *dev = card->dev;
	struct snd_soc_dai_link *link;
	struct device_node *np, *codec, *cpu, *node  = dev->of_node, *platform;
	struct apq8016_sbc_data *data;
	struct snd_soc_dai_link_component *dlc;
	int ret, num_links;

	ret = snd_soc_of_parse_card_name(card, "qcom,model");
	if (ret) {
		dev_err(dev, "Error parsing card name: %d\n", ret);
		return ERR_PTR(ret);
	}

	/* DAPM routes */
	if (of_property_read_bool(node, "qcom,audio-routing")) {
		ret = snd_soc_of_parse_audio_routing(card,
					"qcom,audio-routing");
		if (ret)
			return ERR_PTR(ret);
	}


	/* Populate links */
	num_links = of_get_child_count(node);

	/* Allocate the private data and the DAI link array */
	data = devm_kzalloc(dev,
			    struct_size(data, dai_link, num_links),
			    GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	card->dai_link	= &data->dai_link[0];
	card->num_links	= num_links;

	link = data->dai_link;

	for_each_child_of_node(node, np) {
		dlc = devm_kzalloc(dev, 2 * sizeof(*dlc), GFP_KERNEL);
		if (!dlc)
			return ERR_PTR(-ENOMEM);

		link->cpus	= &dlc[0];
		link->platforms	= &dlc[1];

		link->num_cpus		= 1;
		link->num_platforms	= 1;

		cpu = of_get_child_by_name(np, "cpu");
<<<<<<< HEAD
		codec = of_get_child_by_name(np, "codec");

		if (!cpu || !codec) {
			dev_err(dev, "Can't find cpu/codec DT node\n");
			ret = -EINVAL;
			goto error;
=======
		if (!cpu) {
			dev_err(dev, "Can't find cpu DT node\n");
			ret = -EINVAL;
			return ERR_PTR(ret);
>>>>>>> f1aa5f4ab0f4c7b9bb0400ec261a2febad98f3ee
		}

		link->cpus->of_node = of_parse_phandle(cpu, "sound-dai", 0);
		if (!link->cpus->of_node) {
			dev_err(card->dev, "error getting cpu phandle\n");
			ret = -EINVAL;
<<<<<<< HEAD
			goto error;
=======
			return ERR_PTR(ret);
>>>>>>> f1aa5f4ab0f4c7b9bb0400ec261a2febad98f3ee
		}

		ret = snd_soc_of_get_dai_name(cpu, &link->cpus->dai_name);
		if (ret) {
			dev_err(card->dev, "error getting cpu dai name\n");
			goto error;
		}

<<<<<<< HEAD
		ret = snd_soc_of_get_dai_link_codecs(dev, codec, link);

		if (ret < 0) {
			dev_err(card->dev, "error getting codec dai name\n");
			goto error;
		}

		link->platforms->of_node = link->cpus->of_node;
=======
		platform = of_get_child_by_name(np, "platform");
		codec = of_get_child_by_name(np, "codec");
		if (codec && platform) {
			link->platform_of_node = of_parse_phandle(platform,
					"sound-dai",
					0);
			if (!link->platform_of_node) {
				dev_err(card->dev, "platform dai not found\n");
				ret = -EINVAL;
				return ERR_PTR(ret);
			}

			ret = snd_soc_of_get_dai_link_codecs(dev, codec, link);
			if (ret < 0) {
				dev_err(card->dev, "codec dai not found\n");
				return ERR_PTR(ret);
			}
			link->no_pcm = 1;
			link->ignore_pmdown_time = 1;
			link->init = apq8016_sbc_dai_init;
		} else {
			link->platform_of_node = link->cpu_of_node;
			link->codec_dai_name = "snd-soc-dummy-dai";
			link->codec_name = "snd-soc-dummy";
			link->dynamic = 1;
		}

		link->ignore_suspend = 1;
>>>>>>> f1aa5f4ab0f4c7b9bb0400ec261a2febad98f3ee
		ret = of_property_read_string(np, "link-name", &link->name);
		if (ret) {
			dev_err(card->dev, "error getting codec dai_link name\n");
			goto error;
		}

		link->dpcm_playback = 1;
		link->dpcm_capture = 1;
		link->stream_name = link->name;
		link++;

		of_node_put(cpu);
		of_node_put(codec);
	}

	return data;

 error:
	of_node_put(np);
	of_node_put(cpu);
	of_node_put(codec);
	return ERR_PTR(ret);
}

static const struct snd_soc_dapm_widget apq8016_sbc_dapm_widgets[] = {

	SND_SOC_DAPM_MIC("Handset Mic", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Secondary Mic", NULL),
	SND_SOC_DAPM_MIC("Digital Mic1", NULL),
	SND_SOC_DAPM_MIC("Digital Mic2", NULL),
};

static int apq8016_sbc_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card;
	struct apq8016_sbc_data *data;
	struct resource *res;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->dev = dev;
	card->dapm_widgets = apq8016_sbc_dapm_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(apq8016_sbc_dapm_widgets);
	data = apq8016_sbc_parse_of(card);
	if (IS_ERR(data)) {
		dev_err(&pdev->dev, "Error resolving dai links: %ld\n",
			PTR_ERR(data));
		return PTR_ERR(data);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mic-iomux");
	data->mic_iomux = devm_ioremap_resource(dev, res);
	if (IS_ERR(data->mic_iomux))
		return PTR_ERR(data->mic_iomux);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "spkr-iomux");
	data->spkr_iomux = devm_ioremap_resource(dev, res);
	if (IS_ERR(data->spkr_iomux))
		return PTR_ERR(data->spkr_iomux);

	snd_soc_card_set_drvdata(card, data);

	return devm_snd_soc_register_card(&pdev->dev, card);
}

static const struct of_device_id apq8016_sbc_device_id[]  = {
	{ .compatible = "qcom,apq8016-sbc-sndcard" },
	{},
};
MODULE_DEVICE_TABLE(of, apq8016_sbc_device_id);

static struct platform_driver apq8016_sbc_platform_driver = {
	.driver = {
		.name = "qcom-apq8016-sbc",
		.of_match_table = of_match_ptr(apq8016_sbc_device_id),
	},
	.probe = apq8016_sbc_platform_probe,
};
module_platform_driver(apq8016_sbc_platform_driver);

MODULE_AUTHOR("Srinivas Kandagatla <srinivas.kandagatla@linaro.org");
MODULE_DESCRIPTION("APQ8016 ASoC Machine Driver");
MODULE_LICENSE("GPL v2");
