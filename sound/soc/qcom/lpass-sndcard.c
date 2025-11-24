#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/soundwire/sdw.h>

struct lpass_card_data {
	struct snd_soc_card card;
	/* extra per-dai_link data: */
	struct link_data {
		struct sdw_stream_runtime *sdw_stream;
		struct clk *clk;
		struct clk *clk2;
		bool sdw_prepared;
	} link_data[];
};

struct lpass_sdw_stream_runtime {
	struct sdw_stream_runtime sdw; /* must be first */
	unsigned extra_enable_count;
};

static int i2s_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct lpass_card_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct link_data *link = &data->link_data[rtd->dai_link - rtd->card->dai_link];
	unsigned long rate;
	int ret;

	rate = snd_soc_calc_bclk(params_rate(params), snd_pcm_format_width(params_format(params)),
				 params_channels(params), 1);

	/* note that the clock enable could be done in a trigger() callback (atomic) */

	clk_set_rate(link->clk, rate);
	if (link->clk2) {
		clk_set_rate(link->clk2, rate);
		ret = clk_prepare_enable(link->clk2);
		if (ret)
			return ret;
	}

	ret = clk_prepare_enable(link->clk);
	if (ret && link->clk2)
		clk_disable_unprepare(link->clk2);
	return ret;
}

static int i2s_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct lpass_card_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct link_data *link = &data->link_data[rtd->dai_link - rtd->card->dai_link];

	clk_disable_unprepare(link->clk);
	if (link->clk2)
		clk_disable_unprepare(link->clk2);
	return 0;
}

/* sdw_alloc_stream(), but with larger devres'd allocation  */
static struct sdw_stream_runtime*
lpass_sdw_alloc_stream(struct device *dev, const char *stream_name)
{
	struct sdw_stream_runtime *stream;

	stream = devm_kzalloc(dev, sizeof(struct lpass_sdw_stream_runtime), GFP_KERNEL);
	if (!stream)
		return NULL;

	stream->name = stream_name;
	INIT_LIST_HEAD(&stream->master_list);
	stream->state = SDW_STREAM_ALLOCATED;
	stream->m_rt_count = 0;
	stream->type = SDW_STREAM_PDM;

	return stream;
}

static int sdw_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct lpass_card_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct link_data *link = &data->link_data[rtd->dai_link - rtd->card->dai_link];
	struct sdw_stream_runtime *sdw_stream = link->sdw_stream;

	/* reset stream params (needed because re-using sdw_stream) */
	sdw_stream->params = (struct sdw_stream_params) {};

	if (link->clk)
		return i2s_hw_params(substream, params);
	return 0;
}

/* this has to happen after the soundwire codecs' hw_params() calls.
 * the link's hw_params() happens first, so do this in prepare()
 *
 * there's no unprepare(), so keep track using a "sdw_prepared" to determine
 * if we need to unprepare in hw_free()
 *
 * note its important that the link's hw_free() runs before the codecs'
 */
static int sdw_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct lpass_card_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct link_data *link = &data->link_data[rtd->dai_link - rtd->card->dai_link];
	struct sdw_stream_runtime *sdw_stream = link->sdw_stream;
	int ret;

	/* prepare() can be called again without a hw_free() */
	if (link->sdw_prepared)
		return 0;

	/* XXX: needs a lock on stream */

	if (sdw_stream->state == SDW_STREAM_ENABLED) {
		((struct lpass_sdw_stream_runtime*) sdw_stream)->extra_enable_count++;
		link->sdw_prepared = true;
		return 0;
	}

	ret = sdw_prepare_stream(sdw_stream);
	if (ret)
		return ret;

	ret = sdw_enable_stream(sdw_stream);
	if (ret) {
		sdw_deprepare_stream(sdw_stream);
		return ret;
	}

	link->sdw_prepared = true;
	return 0;
}

static int sdw_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct lpass_card_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct link_data *link = &data->link_data[rtd->dai_link - rtd->card->dai_link];
	struct sdw_stream_runtime *sdw_stream = link->sdw_stream;
	struct lpass_sdw_stream_runtime *stream = (struct lpass_sdw_stream_runtime*) sdw_stream;

	if (link->clk)
		i2s_hw_free(substream);

	if (!link->sdw_prepared)
		return 0;

	/* XXX: needs a lock on stream */

	if (stream->extra_enable_count) {
		stream->extra_enable_count--;
		link->sdw_prepared = false;
		return 0;
	}

	sdw_disable_stream(sdw_stream);
	sdw_deprepare_stream(sdw_stream);
	link->sdw_prepared = false;
	return 0;
}

static const struct snd_soc_ops sdw_ops = {
	.hw_params = sdw_hw_params,
	.prepare = sdw_prepare,
	.hw_free = sdw_hw_free,
};

static const struct snd_soc_ops i2s_ops = {
	.hw_params = i2s_hw_params,
	.hw_free = i2s_hw_free,
};

static int lpass_card_late_probe(struct snd_soc_card *card)
{
	struct lpass_card_data *data = snd_soc_card_get_drvdata(card);
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *codec_dai;
	struct sdw_stream_runtime *sdw_stream;
	int ret, i, direction;

	/* clear any previously set stream pointers */
	for_each_card_rtds(card, rtd) {
		direction = SNDRV_PCM_STREAM_PLAYBACK;
		if (rtd->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream_count == 0)
			direction = SNDRV_PCM_STREAM_CAPTURE;

		for_each_rtd_codec_dais(rtd, i, codec_dai)
			snd_soc_dai_set_stream(codec_dai, NULL, direction);
	}

	/* find links that use soundwire codecs and set sdw_ops + allocate sdw_stream */
	for_each_card_rtds(card, rtd) {
		/* note: the qcom sdw codecs don't care about direction (set it right anyway) */
		direction = SNDRV_PCM_STREAM_PLAYBACK;
		if (rtd->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream_count == 0)
			direction = SNDRV_PCM_STREAM_CAPTURE;

		sdw_stream = ERR_PTR(-ENOENT);
		for_each_rtd_codec_dais(rtd, i, codec_dai) {
			sdw_stream = snd_soc_dai_get_stream(codec_dai, direction);
			if (!IS_ERR(sdw_stream))
				break;
		}

		if (IS_ERR(sdw_stream))
			continue; /* not a soundwire link */

		if (!sdw_stream) {
			sdw_stream = lpass_sdw_alloc_stream(card->dev, rtd->dai_link->name);
			if (!sdw_stream)
				return -ENOMEM;

			for_each_rtd_codec_dais(rtd, i, codec_dai) {
				ret = snd_soc_dai_set_stream(codec_dai, sdw_stream, direction);
				if (ret && ret != -ENOTSUPP)
					return ret;
			}
		}

		data->link_data[rtd->dai_link - card->dai_link].sdw_stream = sdw_stream;
		rtd->dai_link->ops = &sdw_ops;
	}

	return 0;
}

static int lpass_snd_platform_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct lpass_card_data *data;
	struct device *dev = &pdev->dev;
	struct device_node *np;
	struct device_node *node;
	struct of_phandle_args args;
	struct snd_soc_dai_link *link;
	struct snd_soc_dai_link_component *platform;
	struct clk *clk;
	int num_links, ret, i;

	num_links = of_get_available_child_count(dev->of_node);

	data = devm_kzalloc(dev, struct_size(data, link_data, num_links), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	card = &data->card;
	snd_soc_card_set_drvdata(card, data);
	dev_set_drvdata(dev, data); /* for remove() */

	card->owner = THIS_MODULE;
	card->driver_name = "LPASS-SNDCARD";
	card->dev = dev;
	card->late_probe = lpass_card_late_probe;

	ret = snd_soc_of_parse_card_name(card, "model");
	if (ret)
		return dev_err_probe(dev, ret, "Error parsing card name\n");

	card->dai_link = devm_kcalloc(dev, num_links, sizeof(*link), GFP_KERNEL);
	if (!card->dai_link)
		return -ENOMEM;

	platform = devm_kcalloc(dev, num_links*2, sizeof(*platform), GFP_KERNEL);
	if (!platform)
		return -ENOMEM;

	card->num_links = num_links;
	link = card->dai_link;

	for_each_available_child_of_node(dev->of_node, np) {
		link->cpus = &snd_soc_dummy_dlc;
		link->num_cpus = 1;

		link->platforms = platform;
		link->num_platforms = 0;

		ret = of_property_read_string(np, "link-name", &link->name);
		if (ret) {
			of_node_put(np);
			return dev_err_probe(dev, ret, "error getting link-name\n");
		}

		node = of_get_child_by_name(np, "codec");
		if (!node)  {
			of_node_put(np);
			return dev_err_probe(dev, -EINVAL, "%s: can't find codec DT node\n", link->name);
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

		for (i = 0; i < 2; i++) {
			ret = snd_soc_of_get_dlc(node, &args, &link->platforms[i], i);
			if (ret) {
				if (i)
					break;
				of_node_put(node);
				of_node_put(np);
				return dev_err_probe(dev, ret,
						"%s: error getting platform dai name\n", link->name);
			}
			if (i == 0)
				link->id = args.args[0];
			else
				link->id |= args.args[0] << 16;
			link->num_platforms++;
		}
		of_node_put(node);

		clk = devm_get_clk_from_child(dev, np, NULL);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			if (ret != -ENOENT) {
				of_node_put(np);
				return dev_err_probe(dev, ret, "%s: error getting clock\n", link->name);
			}
		} else {
			data->link_data[link - card->dai_link].clk = clk;
			link->ops = &i2s_ops;

			clk = devm_get_clk_from_child(dev, np, "slave");
			if (!IS_ERR(clk)) {
				data->link_data[link - card->dai_link].clk2 = clk;
				// XXX hack for i2s speakers:
				link->playback_only = true;
			}
		}

		link->stream_name = link->name;
		platform += link->num_platforms;
		link++;
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
