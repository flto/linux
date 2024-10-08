#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <linux/soundwire/sdw.h>

#define BITRANGE(x, lo, hi) (((x) << lo) & GENMASK(hi, lo))

#define REG_IRQ_EN	0x9000
#define REG_IRQ_STAT	0x9004
#define REG_IRQ_CLEAR	0x900c

#define IRQ_RD_PER(a) BIT((a) * 3 + 0)
#define IRQ_RD_XRUN(a) BIT((a) * 3 + 1)
#define IRQ_RD_ERR(a) BIT((a) * 3 + 2)
#define IRQ_WR_PER(a) BIT((a) * 3 + 15)
#define IRQ_WR_XRUN(a) BIT((a) * 3 + 16)
#define IRQ_WR_ERR(a) BIT((a) * 3 + 17)

#define RDDMA_MAX 5
#define WRDMA_MAX 4

#define RDDMA_BASE(i) (0xe000 + 0x1000 * (i))
#define WRDMA_BASE(i) (0x1a000 + 0x1000 * (i))

#define REG_DMA_CTL	0x00
#define REG_DMA_BASE	0x04
#define REG_DMA_LEN	0x08
#define REG_DMA_CURR_ADDR	0x0c
#define REG_DMA_PER_LEN		0x10
#define REG_DMA_CODEC_INTF	0x50

#define DMA_CTL_ENABLE BIT(0)
#define DMA_CTL_FIFO_WATERMARK(x) BITRANGE(x, 1, 5)
#define DMA_CTL_AUDIO_INTF(x) BITRANGE(x, 12, 15)
#define DMA_CTL_RESET BIT(31)

#define RDDMA_CTL_WPSCNT(x) BITRANGE(x, 16, 19)
#define RDDMA_CTL_BURST_EN BIT(20)
#define RDDMA_CTL_DYNAMIC_CLOCK BIT(21)
#define RDDMA_CTL_PADDING_EN BIT(22)
#define RDDMA_CTL_PADDING_NUM(x) BITRANGE(x, 23, 27)
#define RDDMA_CTL_BURST8 BIT(28)
#define RDDMA_CTL_BURST16 BIT(29)
#define RDDMA_CTL_DYNBURST BIT(30)

#define WRDMA_CTL_WPSCNT(x) BITRANGE(x, 17, 20)
#define WRDMA_CTL_BURST_EN BIT(21)
#define WRDMA_CTL_DYNAMIC_CLOCK BIT(22)
#define WRDMA_CTL_BURST8 BIT(23)
#define WRDMA_CTL_BURST16 BIT(24)

#define HDMIDMA_CTL_WPSCNT(x) BITRANGE(x, 10, 12)
#define HDMIDMA_CTL_BURST_EN BIT(13)
#define HDMIDMA_CTL_DYNAMIC_CLOCK BIT(14)
#define HDMIDMA_CTL_BURST8 BIT(15)
#define HDMIDMA_CTL_BURST16 BIT(16)
#define HDMIDMA_CTL_GATHER_MODE BIT(17)
#define HDMIDMA_CTL_DYNBURST BIT(18)

#define DMA_CODEC_INTF_NUM_ACTIVE_CHANNEL(x) BITRANGE(x, 0, 7)
#define DMA_CODEC_INTF_CODEC_INTF(x) BITRANGE(x, 16, 19)
#define DMA_CODEC_INTF_CODEC_FS_DELAY(x) BITRANGE(x, 21, 24)
#define DMA_CODEC_INTF_CODEC_FS_SEL(x) BITRANGE(x, 25, 27)
#define DMA_CODEC_INTF_CODEC_ENABLE_16BIT_PACKING BIT(29)
#define DMA_CODEC_INTF_CODEC_ENABLE BIT(30)
#define DMA_CODEC_INTF_CODEC_RESET BIT(31)

//..

#define DMA_CTL_DEFAULT (DMA_CTL_ENABLE | DMA_CTL_FIFO_WATERMARK(0x7) | \
			 DMA_CTL_AUDIO_INTF(0) | RDDMA_CTL_WPSCNT(0) |  \
			 RDDMA_CTL_BURST_EN | RDDMA_CTL_DYNAMIC_CLOCK)

#define DMA_CTL_DEFAULT_CAPTURE (DMA_CTL_ENABLE | DMA_CTL_FIFO_WATERMARK(0x7) | \
				 DMA_CTL_AUDIO_INTF(0) | WRDMA_CTL_WPSCNT(0) | \
				 WRDMA_CTL_BURST_EN |  WRDMA_CTL_DYNAMIC_CLOCK)

/* for device tree */
#define LPASS_INTF_CODEC BIT(4)
#define LPASS_INTF_IS_CAPTURE BIT(5)

struct lpaif {
	struct device *dev;
	void __iomem *base;
	void *mem;
	u32 mem_phys;
	u32 mem_size;
	u32 rddma_count;
	u32 wrdma_count;
	u32 lpm_config_size;

	struct lpaif_channel {
		void __iomem *base;
		struct snd_pcm_substream *substream;
		u32 intf;
	} dma[RDDMA_MAX + WRDMA_MAX];
	struct {
		u32 intf;
		u32 size;
	} lpm_config[16];
};

static int
lpaif_open(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct lpaif *lpaif = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct lpaif_channel *ch = NULL;
	u32 mem_offset, mem_size, intf, i;

	intf = soc_runtime->dai_link->id;

	/* "allocate" lpm */
	if (!lpaif->lpm_config_size) {
		/* XXX: check that it is not already in use */
		mem_offset = 0;
		mem_size = lpaif->mem_size;
	} else {
		/* try to find a matching lpm-config entry */
		mem_offset = 0;
		for (i = 0; i < lpaif->lpm_config_size; i++) {
			mem_size = lpaif->lpm_config[i].size;
			if (intf == lpaif->lpm_config[i].intf)
				break;
			mem_offset += mem_size;
		}
		if (i == lpaif->lpm_config_size) {
			return -ENOMEM;
		}
	}

	/* allocate a channel */
	/* XXX: do this need a spinlock ? */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (intf & LPASS_INTF_IS_CAPTURE)
			return -EINVAL;

		for (i = 0; i < lpaif->rddma_count; i++) {
			if (!lpaif->dma[i].substream) {
				ch = &lpaif->dma[i];
				break;
			}
		}
	} else {
		if ((intf & LPASS_INTF_IS_CAPTURE) == 0)
			return -EINVAL;

		for (i = 0; i < lpaif->wrdma_count; i++) {
			if (!lpaif->dma[RDDMA_MAX + i].substream) {
				ch = &lpaif->dma[RDDMA_MAX + i];
				break;
			}
		}
	}

	if (!ch)
		return -ENOMEM;

	ch->substream = substream;
	ch->intf = intf;

	runtime->private_data = ch;

	runtime->hw.info = SNDRV_PCM_INFO_MMAP |
			   SNDRV_PCM_INFO_MMAP_VALID |
			   SNDRV_PCM_INFO_INTERLEAVED |
			   SNDRV_PCM_INFO_PAUSE |
			   SNDRV_PCM_INFO_RESUME;
	runtime->hw.formats = SNDRV_PCM_FMTBIT_S16;
	runtime->hw.period_bytes_min = 32; /* lowest CURR_ADDR increment? */
	runtime->hw.period_bytes_max = mem_size;
	runtime->hw.periods_min = 1;
	runtime->hw.periods_max = 16; /* no real limit */
	runtime->hw.buffer_bytes_max = mem_size;
	runtime->hw.fifo_size = 0;
	runtime->hw.channels_min = 1;
	runtime->hw.channels_max = 8;



	substream->dma_buffer.area = lpaif->mem + mem_offset;
	substream->dma_buffer.addr = lpaif->mem_phys + mem_offset;
	substream->dma_buffer.bytes = mem_size;
	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

static int
lpaif_close(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct lpaif_channel *ch = substream->runtime->private_data;
	ch->substream = NULL;
	return 0;
}

static int
lpaif_prepare(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct lpaif_channel *ch = substream->runtime->private_data;

	writel_relaxed(substream->runtime->dma_addr, ch->base + REG_DMA_BASE);
	writel_relaxed((snd_pcm_lib_buffer_bytes(substream) >> 2) - 1, ch->base + REG_DMA_LEN);
	writel_relaxed((snd_pcm_lib_period_bytes(substream) >> 2) - 1, ch->base + REG_DMA_PER_LEN);

	return 0;
}

static int
lpaif_trigger(struct snd_soc_component *component,
	      struct snd_pcm_substream *substream,
	      int cmd)
{
	struct lpaif_channel *ch = substream->runtime->private_data;
	u32 dma_ctl = DMA_CTL_DEFAULT;
	u32 codec_intf = 0;

	if (ch->intf & LPASS_INTF_IS_CAPTURE)
		dma_ctl = DMA_CTL_DEFAULT_CAPTURE;

	if (ch->intf & LPASS_INTF_CODEC) {
		u32 ch_mask = BIT(substream->runtime->channels) - 1;
		codec_intf =
			DMA_CODEC_INTF_NUM_ACTIVE_CHANNEL(ch_mask) |
			DMA_CODEC_INTF_CODEC_INTF(ch->intf & 0xf) |
			DMA_CODEC_INTF_CODEC_ENABLE |
			DMA_CODEC_INTF_CODEC_ENABLE_16BIT_PACKING;
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		writel_relaxed(dma_ctl, ch->base + REG_DMA_CTL);
		if (codec_intf)
			writel_relaxed(codec_intf, ch->base + REG_DMA_CODEC_INTF);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		writel_relaxed(0, ch->base + REG_DMA_CTL);
		if (codec_intf)
			writel_relaxed(DMA_CODEC_INTF_CODEC_RESET, ch->base + REG_DMA_CODEC_INTF);
		break;
	}
	return 0;
}

static snd_pcm_uframes_t
lpaif_pointer(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct lpaif_channel *ch = substream->runtime->private_data;
	u32 curr_addr = readl_relaxed(ch->base + REG_DMA_CURR_ADDR);
	return bytes_to_frames(substream->runtime, curr_addr - substream->runtime->dma_addr);
}

static int
lpaif_mmap(struct snd_soc_component *component,
	   struct snd_pcm_substream *substream,
	   struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long size, offset;

	vma->vm_page_prot = pgprot_dmacoherent(vma->vm_page_prot);
	size = vma->vm_end - vma->vm_start;
	offset = vma->vm_pgoff << PAGE_SHIFT;
	return remap_pfn_range(vma, vma->vm_start,
			(runtime->dma_addr + offset) >> PAGE_SHIFT,
			size, vma->vm_page_prot);
}

static const struct snd_soc_component_driver lpaif_component_driver = {
	.name = "lpaif",
	.open = lpaif_open,
	.close = lpaif_close,
	.prepare = lpaif_prepare,
	.trigger = lpaif_trigger,
	.pointer = lpaif_pointer,
	.mmap = lpaif_mmap,
};

static irqreturn_t lpaif_irq(int irq, void *data)
{
	struct lpaif *lpaif = data;
	u32 status = readl_relaxed(lpaif->base + REG_IRQ_STAT);
	u32 status_ok = 0;
	int i;

	for (i = 0; i < 9; i++) {
		if ((status & BIT(i * 3)) && lpaif->dma[i].substream)
			snd_pcm_period_elapsed(lpaif->dma[i].substream);

		status_ok |= BIT(i * 3);
	}

	if (status & ~status_ok)
		dev_warn(lpaif->dev, "err/overflow/underflow irq (0x%.8x)\n", status);

	writel_relaxed(status, lpaif->base + REG_IRQ_CLEAR);
	return IRQ_HANDLED;
}

static int lpaif_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct lpaif *lpaif;
	int irq, ret, i;

	lpaif = devm_kzalloc(dev, sizeof(*lpaif), GFP_KERNEL);
	if (!lpaif)
		return -ENOMEM;

	lpaif->dev = dev;

	lpaif->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(lpaif->base))
		return PTR_ERR(lpaif->base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	lpaif->mem = devm_ioremap_resource_wc(dev, res);
	if (IS_ERR(lpaif->mem))
		return PTR_ERR(lpaif->mem);

	lpaif->mem_phys = res->start;
	lpaif->mem_size = resource_size(res);

	/* XXX: get these from HW register (can be lower) */
	lpaif->rddma_count = RDDMA_MAX;
	lpaif->wrdma_count = WRDMA_MAX;
	for (i = 0; i < lpaif->rddma_count; i++)
		lpaif->dma[i].base = lpaif->base + RDDMA_BASE(i);
	for (i = 0; i < lpaif->wrdma_count; i++)
		lpaif->dma[5 + i].base = lpaif->base + WRDMA_BASE(i);

	ret = of_property_read_variable_u32_array(dev->of_node, "lpm-config",
		(u32*)&lpaif->lpm_config[0], 0, ARRAY_SIZE(lpaif->lpm_config) * 2);
	if (ret >= 0)
		lpaif->lpm_config_size = ret / 2;

	dev_set_drvdata(dev, lpaif);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, lpaif_irq, IRQF_TRIGGER_HIGH, "lpaif", lpaif);
	if (ret < 0)
		return ret;

	/* make sure all interrupts are enabled */
	writel_relaxed(~0u, lpaif->base + REG_IRQ_EN);

	return devm_snd_soc_register_component(dev, &lpaif_component_driver, NULL, 0);
}

static const struct of_device_id lpaif_dt_match[] = {
	{.compatible = "qcom,x1e80100-lpaif"},
	{}
};

MODULE_DEVICE_TABLE(of, lpaif_dt_match);

static struct platform_driver lpaif_driver = {
	.probe  = lpaif_probe,
	.driver = {
		.name = "snd-lpaif",
		.of_match_table = lpaif_dt_match,
	},
};
module_platform_driver(lpaif_driver);
