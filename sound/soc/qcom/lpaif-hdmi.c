#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#define REG_PARITY_CALC_EN 	0x34
#define REG_DMA0_CTRL		0x38
#define REG_CHANNEL_STATUS(x)	(0x48 + (x) * 8)
#define REG_VBIT_CTRL		0xc0

#define REG_HDMI_IRQ_EN 0x2004
#define REG_HDMI_IRQ_STAT 0x2008
#define REG_HDMI_IRQ_CLEAR 0x200c

#define IRQ_PERIOD(a)	BIT((a) * 3 + 0)

#define HDMI_RDDMA 0x3000

#define REG_DMA_CTL	0x00
#define REG_DMA_BASE	0x04
#define REG_DMA_LEN	0x08
#define REG_DMA_CURR_ADDR	0x0c
#define REG_DMA_PER_LEN		0x10

struct lpaif_hdmi {
	struct device *dev;
	void __iomem *base, *ctl;
	struct clk_bulk_data *clks;
	int num_clks;
	struct snd_pcm_substream *substream;
};

static int
lpaif_hdmi_open(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct lpaif_hdmi *lpaif = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	int ret;

	if (cmpxchg(&lpaif->substream, NULL, substream))
		return -EBUSY;

	ret = clk_bulk_prepare_enable(lpaif->num_clks, lpaif->clks);
	if (ret) {
		lpaif->substream = NULL; /* must be atomic */
		return ret;
	}

	lpaif->substream = substream;

	runtime->private_data = (void*) (size_t) soc_runtime->dai_link->id;

	runtime->hw.info = SNDRV_PCM_INFO_MMAP |
			   SNDRV_PCM_INFO_MMAP_VALID |
			   SNDRV_PCM_INFO_INTERLEAVED |
			   SNDRV_PCM_INFO_PAUSE |
			   SNDRV_PCM_INFO_RESUME;
	runtime->hw.formats = SNDRV_PCM_FMTBIT_S24_LE;
	/* soft limits: */
	runtime->hw.period_bytes_max = 0x2000;
	runtime->hw.buffer_bytes_max = 0x2000;
	runtime->hw.periods_min = 2;
	runtime->hw.periods_max = 16;

	runtime->hw.period_bytes_min = 64;
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 64);

	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, lpaif->dev, 0x2000, &substream->dma_buffer);
	if (ret) {
		clk_bulk_disable_unprepare(lpaif->num_clks, lpaif->clks);
		lpaif->substream = NULL; /* must be atomic */
		return ret;
	}

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

static int
lpaif_hdmi_close(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct lpaif_hdmi *lpaif = snd_soc_component_get_drvdata(component);

	clk_bulk_disable_unprepare(lpaif->num_clks, lpaif->clks);

	lpaif->substream = NULL;

	snd_dma_free_pages(&substream->dma_buffer);
	return 0;
}

static int
lpaif_hdmi_prepare(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct lpaif_hdmi *lpaif = snd_soc_component_get_drvdata(component);
	void __iomem *base = lpaif->base + HDMI_RDDMA;

	/* reset and disable legacy mode */
	writel_relaxed(BIT(31), lpaif->ctl);
	writel_relaxed(0, lpaif->ctl);
	writel_relaxed(0, lpaif->ctl + 0x08);

	/* HW insert P (parity), V (valid), C(channel status), U(user) */
	writel_relaxed(1, lpaif->base + REG_PARITY_CALC_EN);
	writel_relaxed(1, lpaif->base + REG_VBIT_CTRL);
	writel_relaxed(3, lpaif->base + REG_DMA0_CTRL);

	/* channel status bits
	 * 48khz 16bit (even though only 24-bit is supported. required for Index audio)
	 */
	writel_relaxed(0x02000000, lpaif->base + REG_CHANNEL_STATUS(0));
	writel_relaxed(0x00000002, lpaif->base + REG_CHANNEL_STATUS(0) + 4);

	writel_relaxed(substream->runtime->dma_addr, base + REG_DMA_BASE);
	writel_relaxed((snd_pcm_lib_buffer_bytes(substream) >> 2) - 1, base + REG_DMA_LEN);
	writel_relaxed((snd_pcm_lib_period_bytes(substream) >> 2) - 1, base + REG_DMA_PER_LEN);

	return 0;
}

static int
lpaif_hdmi_trigger(struct snd_soc_component *component,
		   struct snd_pcm_substream *substream,
		   int cmd)
{
	struct lpaif_hdmi *lpaif = snd_soc_component_get_drvdata(component);
	size_t intf = (size_t) substream->runtime->private_data;
	void __iomem *base = lpaif->base + HDMI_RDDMA;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		writel_relaxed(0x0005a00f, base + REG_DMA_CTL);
		writel_relaxed(0x00003de9, lpaif->base + 0x1c);
		writel_relaxed(0x00020002 | (intf<<30), lpaif->base + 0xc8);
		writel_relaxed(0x0005e00f, base + REG_DMA_CTL);
		writel_relaxed(0x01804007, lpaif->base + REG_HDMI_IRQ_EN);
		writel_relaxed(0x01804007, lpaif->base + REG_HDMI_IRQ_CLEAR);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		writel_relaxed(0, lpaif->base + REG_HDMI_IRQ_EN);
		writel_relaxed(0x0005a00f, base + REG_DMA_CTL);
		writel_relaxed(0x00003de8, lpaif->base + 0x1c);
		writel_relaxed(0x00020003 | (intf<<30), lpaif->base + 0xc8);
		writel_relaxed(0x0005a00e, base + REG_DMA_CTL);
		break;
	}
	return 0;
}

static snd_pcm_uframes_t
lpaif_hdmi_pointer(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct lpaif_hdmi *lpaif = snd_soc_component_get_drvdata(component);
	void __iomem *base = lpaif->base + HDMI_RDDMA;
	u32 curr_addr = readl_relaxed(base + REG_DMA_CURR_ADDR);
	return bytes_to_frames(substream->runtime, curr_addr - substream->runtime->dma_addr);
}

static const struct snd_soc_component_driver lpaif_hdmi_component_driver = {
	.name = "lpaif-hdmi",
	.open = lpaif_hdmi_open,
	.close = lpaif_hdmi_close,
	.prepare = lpaif_hdmi_prepare,
	.trigger = lpaif_hdmi_trigger,
	.pointer = lpaif_hdmi_pointer,
};

static irqreturn_t lpaif_hdmi_irq(int irq, void *data)
{
	struct lpaif_hdmi *lpaif = data;
	u32 status = readl_relaxed(lpaif->base + REG_HDMI_IRQ_STAT);
	u32 status_ok = 0x01804000 | IRQ_PERIOD(0);

	if ((status & IRQ_PERIOD(0)) && lpaif->substream)
		snd_pcm_period_elapsed(lpaif->substream);

	if (status & ~status_ok)
		dev_warn(lpaif->dev, "unexpected irq (0x%.8x)\n", status);

	writel_relaxed(status, lpaif->base + REG_HDMI_IRQ_CLEAR);
	return IRQ_HANDLED;
}

static int lpaif_hdmi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lpaif_hdmi *lpaif;
	int irq, ret;

	lpaif = devm_kzalloc(dev, sizeof(*lpaif), GFP_KERNEL);
	if (!lpaif)
		return -ENOMEM;

	lpaif->dev = dev;

	lpaif->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(lpaif->base))
		return PTR_ERR(lpaif->base);

	lpaif->ctl = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(lpaif->ctl))
		return PTR_ERR(lpaif->ctl);

	ret = devm_clk_bulk_get_all(dev, &lpaif->clks);
	if (ret < 0)
		return ret;
	lpaif->num_clks = ret;

	dev_set_drvdata(dev, lpaif);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, lpaif_hdmi_irq, IRQF_TRIGGER_HIGH, "lpaif-hdmi", lpaif);
	if (ret < 0)
		return ret;

	/* enable some interrupts... */
	writel_relaxed(0x01804007, lpaif->base + REG_HDMI_IRQ_EN);

	return devm_snd_soc_register_component(dev, &lpaif_hdmi_component_driver, NULL, 0);
}

static const struct of_device_id lpaif_hdmi_dt_match[] = {
	{.compatible = "qcom,lpaif-hdmi"},
	{}
};

MODULE_DEVICE_TABLE(of, lpaif_hdmi_dt_match);

static struct platform_driver lpaif_hdmi_driver = {
	.probe  = lpaif_hdmi_probe,
	.driver = {
		.name = "snd-lpaif-hdmi",
		.of_match_table = lpaif_hdmi_dt_match,
	},
};
module_platform_driver(lpaif_hdmi_driver);
