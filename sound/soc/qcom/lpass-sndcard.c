#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <linux/soundwire/sdw.h>

struct lpass_snd_data {
	struct snd_soc_card card;
	struct sdw_stream_runtime *sruntime;
	bool stream_prepared;
};

static int sdw_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct lpass_snd_data *lp = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *codec_dai;
	int ret, i;

	lp->sruntime = sdw_alloc_stream(rtd->dai_link->name);
	if (!lp->sruntime)
		return -ENOMEM;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		ret = snd_soc_dai_set_stream(codec_dai, lp->sruntime,
					     substream->stream);
		if (ret < 0 && ret != -ENOTSUPP) {
			dev_err(rtd->dev, "Failed to set sdw stream on %s\n",
				codec_dai->name);
			sdw_release_stream(lp->sruntime);
			return ret;
		}
	}

	return 0;
}

static void sdw_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct lpass_snd_data *lp = snd_soc_card_get_drvdata(rtd->card);

	sdw_release_stream(lp->sruntime);
}

static int sdw_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct lpass_snd_data *lp = snd_soc_card_get_drvdata(rtd->card);
	int ret;

	if (lp->stream_prepared)
		return 0;

	ret = sdw_prepare_stream(lp->sruntime);
	if (ret)
		return ret;

	/**
	 * NOTE: there is a strict hw requirement about the ordering of port
	 * enables and actual WSA881x PA enable. PA enable should only happen
	 * after soundwire ports are enabled if not DC on the line is
	 * accumulated resulting in Click/Pop Noise
	 * PA enable/mute are handled as part of codec DAPM and digital mute.
	 */

	ret = sdw_enable_stream(lp->sruntime);
	if (ret) {
		sdw_deprepare_stream(lp->sruntime);
		return ret;
	}
	lp->stream_prepared = true;

	return 0;
}

static int sdw_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct lpass_snd_data *lp = snd_soc_card_get_drvdata(rtd->card);

	if (lp->stream_prepared) {
		sdw_disable_stream(lp->sruntime);
		sdw_deprepare_stream(lp->sruntime);
		lp->stream_prepared = false;
	}
	return 0;
}

static const struct snd_soc_ops sdw_be_ops = {
	.startup = sdw_startup,
	.shutdown = sdw_shutdown,
	.hw_free = sdw_hw_free,
	.prepare = sdw_prepare,
};

static int lpass_snd_platform_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct lpass_snd_data *data;
	struct device *dev = &pdev->dev;
	struct device_node *np;
	struct device_node *node;
	struct of_phandle_args args;
	struct snd_soc_dai_link *link;
	struct snd_soc_dai_link_component *platform;
	int num_links, ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	card = &data->card;
	snd_soc_card_set_drvdata(card, data);

	card->owner = THIS_MODULE;
	card->driver_name = "LPASS-SNDCARD";
	card->dev = dev;

	ret = snd_soc_of_parse_card_name(card, "model");
	if (ret)
		return dev_err_probe(dev, ret, "Error parsing card name\n");

	/* Populate links */
	num_links = of_get_available_child_count(dev->of_node);

	/* Allocate the DAI link array */
	card->dai_link = devm_kcalloc(dev, num_links, sizeof(*link), GFP_KERNEL);
	if (!card->dai_link)
		return -ENOMEM;

	platform = devm_kcalloc(dev, num_links, sizeof(*platform), GFP_KERNEL);
	if (!platform)
		return -ENOMEM;

	card->num_links = num_links;
	link = card->dai_link;

	for_each_available_child_of_node(dev->of_node, np) {
		link->cpus = &snd_soc_dummy_dlc;
		link->num_cpus = 1;

		link->platforms = platform;
		link->num_platforms = 1;

		if (of_property_present(np, "soundwire"))
			link->ops = &sdw_be_ops;

		if (of_property_present(np, "capture"))
			link->capture_only = true;
		else
			link->playback_only = true;

		ret = of_property_read_string(np, "link-name", &link->name);
		if (ret) {
			of_node_put(np);
			return dev_err_probe(dev, ret, "error getting codec dai_link name\n");
		}

		node = of_get_child_by_name(np, "codec");
		if (!node)  {
			of_node_put(np);
			return dev_err_probe(dev, -EINVAL, "%s: Can't find cpu DT node\n", link->name);
		}

		ret = snd_soc_of_get_dai_link_codecs(dev, node, link);
		of_node_put(node);
		if (ret < 0) {
			of_node_put(np);
			return dev_err_probe(dev, ret, "%s: codec dais not found\n", link->name);
		}

		node = of_get_child_by_name(np, "platform");
		if (!node)  {
			of_node_put(np);
			return dev_err_probe(dev, -EINVAL, "%s: Can't find platform DT node\n", link->name);
		}

		ret = snd_soc_of_get_dlc(node, &args, link->platforms, 0);
		of_node_put(node);
		if (ret) {
			of_node_put(np);
			return dev_err_probe(card->dev, ret,
					     "%s: error getting platform dai name\n", link->name);
		}

		link->id = args.args[0];

		link->stream_name = link->name;
		link++;
		platform++;
	}

	return devm_snd_soc_register_card(dev, card);
}

static const struct of_device_id lpass_snd_device_id[]  = {
	{ .compatible = "qcom,lpass-sndcard" },
	{}
};
MODULE_DEVICE_TABLE(of, lpass_snd_device_id);

static struct platform_driver lpass_snd_driver = {
	.probe = lpass_snd_platform_probe,
	.driver = {
		.name = "lpass-sndcard",
		.of_match_table = lpass_snd_device_id,
		.pm = &snd_soc_pm_ops,
	},
};
module_platform_driver(lpass_snd_driver);

MODULE_DESCRIPTION("LPASS sndcard ASoC Machine Driver");
MODULE_LICENSE("GPL");
