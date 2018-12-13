/*
 * Copyright 2017 NXP
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/clk.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/component.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/irq.h>
#include <linux/of_device.h>

#include "imx-hdp.h"
#include "imx-hdmi.h"

struct imx_hdp *g_hdp;
struct drm_display_mode *g_mode;

static const struct drm_display_mode edid_cea_modes[] = {
#if 0
	/* 3 - 720x480@60Hz */
	{ DRM_MODE("720x480", DRM_MODE_TYPE_DRIVER, 27000, 720, 736,
		   798, 858, 0, 480, 489, 495, 525, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	  .vrefresh = 60, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 4 - 1280x720@60Hz */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1390,
		   1430, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .vrefresh = 60, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
#endif
	/* 16 - 1920x1080@60Hz */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .vrefresh = 60, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
#if 0
	/* 97 - 3840x2160@60Hz */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 594000,
		   3840, 4016, 4104, 4400, 0,
		   2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .vrefresh = 60, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 96 - 3840x2160@30Hz */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 297000,
		   3840, 4016, 4104, 4400, 0,
		   2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .vrefresh = 30, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
#endif
};

static inline struct imx_hdp *enc_to_imx_hdp(struct drm_encoder *e)
{
	return container_of(e, struct imx_hdp, encoder);
}

static u32 TMDS_rate_table[7] = {
25200, 27000, 54000, 74250, 148500, 297000, 594000,
};

static u32 N_table_32k[8] = {
/*25200, 27000, 54000, 74250, 148500, 297000, 594000,*/
4096, 4096, 4096, 4096, 4096, 3072, 3072, 4096,
};

static u32 N_table_44k[8] = {
6272, 6272, 6272, 6272, 6272, 4704, 9408, 6272,
};

static u32 N_table_48k[8] = {
6144, 6144, 6144, 6144, 6144, 5120, 6144, 6144,
};

static int select_N_index(int vmode_index)
{

	int i = 0, j = 0;

	for (i = 0; i < VIC_MODE_COUNT; i++) {
		if (vic_table[i][23] == vmode_index)
			break;
	}

	if (i == VIC_MODE_COUNT) {
		pr_err("vmode is wrong!\n");
		j = 7;
		return j;
	}

	for (j = 0; j < 7; j++) {
		if (vic_table[i][13] == TMDS_rate_table[j])
			break;
	}

	return j;
}

u32 imx_hdp_audio(AUDIO_TYPE type, u32 sample_rate, u32 channels, u32 width)
{
	AUDIO_FREQ  freq;
	AUDIO_WIDTH bits;
	int ncts_n;
	state_struct *state = &g_hdp->state;
	int idx_n = select_N_index(g_hdp->vic);

	switch (sample_rate) {
	case 32000:
		freq = AUDIO_FREQ_32;
		ncts_n = N_table_32k[idx_n];
		break;
	case 44100:
		freq = AUDIO_FREQ_44_1;
		ncts_n = N_table_44k[idx_n];
		break;
	case 48000:
		freq = AUDIO_FREQ_48;
		ncts_n = N_table_48k[idx_n];
		break;
	case 88200:
		freq = AUDIO_FREQ_88_2;
		ncts_n = N_table_44k[idx_n] * 2;
		break;
	case 96000:
		freq = AUDIO_FREQ_96;
		ncts_n = N_table_48k[idx_n] * 2;
		break;
	case 176400:
		freq = AUDIO_FREQ_176_4;
		ncts_n = N_table_44k[idx_n] * 4;
		break;
	case 192000:
		freq = AUDIO_FREQ_192;
		ncts_n = N_table_48k[idx_n] * 4;
		break;
	default:
		return -EINVAL;
	}

	switch (width) {
	case 16:
		bits = AUDIO_WIDTH_16;
		break;
	case 24:
		bits = AUDIO_WIDTH_24;
		break;
	case 32:
		bits = AUDIO_WIDTH_32;
		break;
	default:
		return -EINVAL;
	}


	CDN_API_AudioOff_blocking(state, type);
	CDN_API_AudioAutoConfig_blocking(state,
				type,
				channels,
				freq,
				0,
				bits,
				g_hdp->audio_type,
				ncts_n,
				AUDIO_MUTE_MODE_UNMUTE);
	return 0;
}

static void imx_hdp_plmux_config(struct imx_hdp *hdp, struct drm_display_mode *mode)
{
	u32 val;

	val = 4; /* RGB */
	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		val |= 1 << PL_MUX_CTL_VCP_OFFSET;
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		val |= 1 << PL_MUX_CTL_HCP_OFFSET;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		val |= 0x2;

	writel(val, hdp->ss_base + CSR_PIXEL_LINK_MUX_CTL);
}

static void imx_hdp_state_init(struct imx_hdp *hdp)
{
	state_struct *state = &hdp->state;

	memset(state, 0, sizeof(state_struct));
	mutex_init(&state->mutex);

	state->mem.regs_base = hdp->regs_base;
	state->mem.ss_base = hdp->ss_base;
	state->rw = hdp->rw;
}

static int imx_get_vic_index(struct drm_display_mode *mode)
{
	int i;

	for (i = 0; i < VIC_MODE_COUNT; i++) {
		if (mode->hdisplay == vic_table[i][H_ACTIVE] &&
			mode->vdisplay == vic_table[i][V_ACTIVE] &&
			mode->clock == vic_table[i][PIXEL_FREQ_KHZ])
			return i;
	}
	/* vidoe mode not support now  */
	return -1;
}

static void imx_hdp_mode_setup(struct imx_hdp *hdp, struct drm_display_mode *mode)
{
	int dp_vic;
	int ret;

	imx_hdp_call(hdp, pixel_clock_set_rate, &hdp->clks);

	imx_hdp_call(hdp, pixel_clock_enable, &hdp->clks);

	imx_hdp_plmux_config(hdp, mode);

	dp_vic = imx_get_vic_index(mode);
	if (dp_vic < 0) {
		DRM_ERROR("Unsupport video mode now, %s, clk=%d\n", mode->name, mode->clock);
		return;
	}

	ret = imx_hdp_call(hdp, phy_init, &hdp->state, dp_vic, 1, 8);
	if (ret < 0) {
		DRM_ERROR("Failed to initialise HDP PHY\n");
		return;
	}
	imx_hdp_call(hdp, mode_set, &hdp->state, dp_vic, 1, 8, hdp->link_rate);

	/* Get vic of CEA-861 */
	hdp->vic = drm_match_cea_mode(mode);
}

static void imx_hdp_bridge_mode_set(struct drm_bridge *bridge,
				    struct drm_display_mode *orig_mode,
				    struct drm_display_mode *mode)
{
	struct imx_hdp *hdp = bridge->driver_private;

	mutex_lock(&hdp->mutex);

	memcpy(&hdp->video.cur_mode, mode, sizeof(hdp->video.cur_mode));
	imx_hdp_mode_setup(hdp, mode);
	/* Store the display mode for plugin/DKMS poweron events */
	memcpy(&hdp->video.pre_mode, mode, sizeof(hdp->video.pre_mode));

	mutex_unlock(&hdp->mutex);
}

static void imx_hdp_bridge_disable(struct drm_bridge *bridge)
{
}

static void imx_hdp_bridge_enable(struct drm_bridge *bridge)
{
}

static enum drm_connector_status
imx_hdp_connector_detect(struct drm_connector *connector, bool force)
{
	struct imx_hdp *hdp = container_of(connector,
						struct imx_hdp, connector);
	int ret;
	u8 hpd = 0xf;

	return connector_status_connected;

	ret = imx_hdp_call(hdp, get_hpd_state, &hdp->state, &hpd);
	if (ret > 0)
		return connector_status_unknown;

	if (hpd == 1)
		/* Cable Connected */
		return connector_status_connected;
	else if (hpd == 0)
		/* Cable Disconnedted */
		return connector_status_disconnected;
	else {
		/* Cable status unknown */
		DRM_INFO("Unknow cable status, hdp=%u\n", hpd);
		return connector_status_unknown;
	}
}

static int imx_hdp_connector_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	int num_modes = 0;
	int i;

	struct imx_hdp *hdp = container_of(connector, struct imx_hdp,
					     connector);
	struct edid *edid;

	if (hdp->is_edid == true) {
		edid = drm_do_get_edid(connector, hdp->ops->get_edid_block, &hdp->state);
		if (edid) {
			dev_dbg(hdp->dev, "%x,%x,%x,%x,%x,%x,%x,%x\n",
					edid->header[0], edid->header[1], edid->header[2], edid->header[3],
					edid->header[4], edid->header[5], edid->header[6], edid->header[7]);
			drm_connector_update_edid_property(connector, edid);
			drm_add_edid_modes(connector, edid);
			kfree(edid);
		}
	} else {
		dev_dbg(hdp->dev, "failed to get edid\n");
		for (i = 0; i < ARRAY_SIZE(edid_cea_modes); i++) {
			mode = drm_mode_create(connector->dev);
			if (!mode)
				return -EINVAL;
			drm_mode_copy(mode, &edid_cea_modes[i]);
			mode->type |= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
			drm_mode_probed_add(connector, mode);
		}
		num_modes = i;
	}

	return num_modes;
}

static enum drm_mode_status
imx_hdp_connector_mode_valid(struct drm_connector *connector,
			     struct drm_display_mode *mode)
{
	struct imx_hdp *hdp = container_of(connector, struct imx_hdp,
					     connector);
	enum drm_mode_status mode_status = MODE_OK;
	struct drm_cmdline_mode *cmdline_mode;
	cmdline_mode = &connector->cmdline_mode;

	/* cmdline mode is the max support video mode when edid disabled */
	if (!hdp->is_edid)
		if (cmdline_mode->xres != 0 &&
			cmdline_mode->xres < mode->hdisplay)
			return MODE_BAD_HVALUE;

	if (hdp->is_4kp60 && mode->clock > 594000)
		return MODE_CLOCK_HIGH;
	else if (!hdp->is_4kp60 && mode->clock > 150000)
		return MODE_CLOCK_HIGH;

	/* 4096x2160 is not supported now */
	if (mode->hdisplay > 3840)
		return MODE_BAD_HVALUE;

	if (mode->vdisplay > 2160)
		return MODE_BAD_VVALUE;

	return mode_status;
}

static void imx_hdp_connector_force(struct drm_connector *connector)
{
	struct imx_hdp *hdp = container_of(connector, struct imx_hdp,
					     connector);

	mutex_lock(&hdp->mutex);
	hdp->force = connector->force;
	mutex_unlock(&hdp->mutex);
}

static const struct drm_connector_funcs imx_hdp_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = imx_hdp_connector_detect,
	.destroy = drm_connector_cleanup,
	.force = imx_hdp_connector_force,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs imx_hdp_connector_helper_funcs = {
	.get_modes = imx_hdp_connector_get_modes,
	.mode_valid = imx_hdp_connector_mode_valid,
};

static const struct drm_bridge_funcs imx_hdp_bridge_funcs = {
	.enable = imx_hdp_bridge_enable,
	.disable = imx_hdp_bridge_disable,
	.mode_set = imx_hdp_bridge_mode_set,
};


static void imx_hdp_imx_encoder_disable(struct drm_encoder *encoder)
{
}

static void imx_hdp_imx_encoder_enable(struct drm_encoder *encoder)
{
}

#if 0
static int imx_hdp_imx_encoder_atomic_check(struct drm_encoder *encoder,
				    struct drm_crtc_state *crtc_state,
				    struct drm_connector_state *conn_state)
{
	struct imx_crtc_state *imx_crtc_state = to_imx_crtc_state(crtc_state);

	imx_crtc_state->bus_format = MEDIA_BUS_FMT_RGB101010_1X30;
	return 0;
}
#endif
static const struct drm_encoder_helper_funcs imx_hdp_imx_encoder_helper_funcs = {
	.enable     = imx_hdp_imx_encoder_enable,
	.disable    = imx_hdp_imx_encoder_disable,
};

static const struct drm_encoder_funcs imx_hdp_imx_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int imx8mq_hdp_read(struct hdp_mem *mem, unsigned int addr, unsigned int *value)
{
	unsigned int temp;
	void *tmp_addr = mem->regs_base + addr;
	temp = __raw_readl((volatile unsigned int *)tmp_addr);
	*value = temp;
	return 0;
}

static int imx8mq_hdp_write(struct hdp_mem *mem, unsigned int addr, unsigned int value)
{
	void *tmp_addr = mem->regs_base + addr;

	__raw_writel(value, (volatile unsigned int *)tmp_addr);
	return 0;
}

static int imx8mq_hdp_sread(struct hdp_mem *mem, unsigned int addr, unsigned int *value)
{
	unsigned int temp;
	void *tmp_addr = mem->ss_base + addr;
	temp = __raw_readl((volatile unsigned int *)tmp_addr);
	*value = temp;
	return 0;
}

static int imx8mq_hdp_swrite(struct hdp_mem *mem, unsigned int addr, unsigned int value)
{
	void *tmp_addr = mem->ss_base + addr;
	__raw_writel(value, (volatile unsigned int *)tmp_addr);
	return 0;
}

static struct hdp_rw_func imx8mq_rw = {
	.read_reg = imx8mq_hdp_read,
	.write_reg = imx8mq_hdp_write,
	.sread_reg = imx8mq_hdp_sread,
	.swrite_reg = imx8mq_hdp_swrite,
};

static struct hdp_ops imx8mq_ops = {
	.phy_init = hdmi_phy_init_t28hpc,
	.mode_set = hdmi_mode_set_t28hpc,
	.get_edid_block = hdmi_get_edid_block,
	.get_hpd_state = hdmi_get_hpd_state,
};

static struct hdp_devtype imx8mq_hdmi_devtype = {
	.is_edid = false,
	.is_4kp60 = true,
	.audio_type = CDN_HDMITX_KIRAN,
	.ops = &imx8mq_ops,
	.rw = &imx8mq_rw,
};

static const struct of_device_id imx_hdp_dt_ids[] = {
	{ .compatible = "fsl,imx8mq-hdmi", .data = &imx8mq_hdmi_devtype},
	{ }
};
MODULE_DEVICE_TABLE(of, imx_hdp_dt_ids);

static void hotplug_work_func(struct work_struct *work)
{
	struct imx_hdp *hdp = container_of(work, struct imx_hdp,
								hotplug_work.work);
	struct drm_connector *connector = &hdp->connector;

	drm_helper_hpd_irq_event(connector->dev);

	if (connector->status == connector_status_connected) {
		/* Cable Connected */
		DRM_INFO("HDMI/DP Cable Plug In\n");
		enable_irq(hdp->irq[HPD_IRQ_OUT]);
	} else if (connector->status == connector_status_disconnected) {
		/* Cable Disconnedted  */
		DRM_INFO("HDMI/DP Cable Plug Out\n");
		enable_irq(hdp->irq[HPD_IRQ_IN]);
	}
}

static irqreturn_t imx_hdp_irq_thread(int irq, void *data)
{
	struct imx_hdp *hdp = data;

	disable_irq_nosync(irq);

	mod_delayed_work(system_wq, &hdp->hotplug_work,
			msecs_to_jiffies(HOTPLUG_DEBOUNCE_MS));

	return IRQ_HANDLED;
}

static int imx_hdp_imx_bind(struct device *dev, struct device *master,
			    void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct imx_hdp *hdp;
	const struct of_device_id *of_id =
			of_match_device(imx_hdp_dt_ids, dev);
	const struct hdp_devtype *devtype = of_id->data;
	struct drm_encoder *encoder;
	struct drm_bridge *bridge;
	struct drm_connector *connector;
	struct resource *res;
	u8 hpd;
	int ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	hdp = devm_kzalloc(&pdev->dev, sizeof(*hdp), GFP_KERNEL);
	if (!hdp)
		return -ENOMEM;

	hdp->dev = &pdev->dev;
	encoder = &hdp->encoder;
	bridge = &hdp->bridge;
	connector = &hdp->connector;

	g_hdp = hdp;
	mutex_init(&hdp->mutex);

	hdp->irq[HPD_IRQ_IN] = platform_get_irq_byname(pdev, "plug_in");
	if (hdp->irq[HPD_IRQ_IN] < 0)
		dev_info(&pdev->dev, "No plug_in irq number\n");

	hdp->irq[HPD_IRQ_OUT] = platform_get_irq_byname(pdev, "plug_out");
	if (hdp->irq[HPD_IRQ_OUT] < 0)
		dev_info(&pdev->dev, "No plug_out irq number\n");

	/* register map */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hdp->regs_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(hdp->regs_base)) {
		dev_err(dev, "Failed to get HDP CTRL base register\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	hdp->ss_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(hdp->ss_base)) {
		dev_err(dev, "Failed to get HDP CRS base register\n");
		return -EINVAL;
	}

	hdp->is_edid = devtype->is_edid;
	hdp->is_4kp60 = devtype->is_4kp60;
	hdp->audio_type = devtype->audio_type;
	hdp->ops = devtype->ops;
	hdp->rw = devtype->rw;

	/* HDP controller init */
	imx_hdp_state_init(hdp);

	hdp->link_rate = AFE_LINK_RATE_1_6;

	hdp->dual_mode = false;

	ret = imx_hdp_call(hdp, pixel_link_init, &hdp->state);
	if (ret < 0) {
		DRM_ERROR("Failed to initialize clock %d\n", ret);
		return ret;
	}

	ret = imx_hdp_call(hdp, clock_init, &hdp->clks);
	if (ret < 0) {
		DRM_ERROR("Failed to initialize clock\n");
		return ret;
	}

	imx_hdp_call(hdp, ipg_clock_set_rate, &hdp->clks);

	ret = imx_hdp_call(hdp, ipg_clock_enable, &hdp->clks);
	if (ret < 0) {
		DRM_ERROR("Failed to initialize IPG clock\n");
		return ret;
	}

	/* Pixel Format - 1 RGB, 2 YCbCr 444, 3 YCbCr 420 */
	/* bpp (bits per subpixel) - 8 24bpp, 10 30bpp, 12 36bpp, 16 48bpp */
	imx_hdp_call(hdp, fw_load, &hdp->state);

	imx_hdp_call(hdp, fw_init, &hdp->state);

	/* default set hdmi to 1080p60 mode */
	ret = imx_hdp_call(hdp, phy_init, &hdp->state, 2, 1, 8);
	if (ret < 0) {
		DRM_ERROR("Failed to initialise HDP PHY\n");
		return ret;
	}

	/* encoder */
	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);
	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	/* encoder */
	drm_encoder_helper_add(encoder, &imx_hdp_imx_encoder_helper_funcs);
	drm_encoder_init(drm, encoder, &imx_hdp_imx_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);

	/* bridge */
	bridge->driver_private = hdp;
	bridge->funcs = &imx_hdp_bridge_funcs;
	ret = drm_bridge_attach(encoder, bridge, NULL);
	if (ret) {
		DRM_ERROR("Failed to initialize bridge with drm\n");
		return -EINVAL;
	}

	encoder->bridge = bridge;
	hdp->connector.polled = DRM_CONNECTOR_POLL_HPD;

	/* connector */
	drm_connector_helper_add(connector,
				 &imx_hdp_connector_helper_funcs);

	drm_connector_init(drm, connector,
			   &imx_hdp_connector_funcs,
			   DRM_MODE_CONNECTOR_HDMIA);

	drm_connector_attach_encoder(connector, encoder);

	dev_set_drvdata(dev, hdp);

	INIT_DELAYED_WORK(&hdp->hotplug_work, hotplug_work_func);

	/* Check cable states before enable irq */
	imx_hdp_call(hdp, get_hpd_state, &hdp->state, &hpd);

	/* Enable Hotplug Detect IRQ thread */
	if (hdp->irq[HPD_IRQ_IN] > 0) {
		irq_set_status_flags(hdp->irq[HPD_IRQ_IN], IRQ_NOAUTOEN);
		ret = devm_request_threaded_irq(dev, hdp->irq[HPD_IRQ_IN],
						NULL, imx_hdp_irq_thread,
						IRQF_ONESHOT, dev_name(dev), hdp);
		if (ret) {
			dev_err(&pdev->dev, "can't claim irq %d\n",
							hdp->irq[HPD_IRQ_IN]);
			goto err_irq;
		}
		/* Cable Disconnedted, enable Plug in IRQ */
		if (hpd == 0)
			enable_irq(hdp->irq[HPD_IRQ_IN]);
	}
	if (hdp->irq[HPD_IRQ_OUT] > 0) {
		irq_set_status_flags(hdp->irq[HPD_IRQ_OUT], IRQ_NOAUTOEN);
		ret = devm_request_threaded_irq(dev, hdp->irq[HPD_IRQ_OUT],
						NULL, imx_hdp_irq_thread,
						IRQF_ONESHOT, dev_name(dev), hdp);
		if (ret) {
			dev_err(&pdev->dev, "can't claim irq %d\n",
							hdp->irq[HPD_IRQ_OUT]);
			goto err_irq;
		}
		/* Cable Connected, enable Plug out IRQ */
		if (hpd == 1)
			enable_irq(hdp->irq[HPD_IRQ_OUT]);
	}

	return 0;
err_irq:
	drm_encoder_cleanup(encoder);
	return ret;
}

static void imx_hdp_imx_unbind(struct device *dev, struct device *master,
			       void *data)
{
	struct imx_hdp *hdp = dev_get_drvdata(dev);

	imx_hdp_call(hdp, pixel_link_deinit, &hdp->state);
	//drm_bridge_detach(&hdp->bridge);
}

static const struct component_ops imx_hdp_imx_ops = {
	.bind	= imx_hdp_imx_bind,
	.unbind	= imx_hdp_imx_unbind,
};

static int imx_hdp_imx_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &imx_hdp_imx_ops);
}

static int imx_hdp_imx_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &imx_hdp_imx_ops);

	return 0;
}

static struct platform_driver imx_hdp_imx_platform_driver = {
	.probe  = imx_hdp_imx_probe,
	.remove = imx_hdp_imx_remove,
	.driver = {
		.name = "i.mx8-hdp",
		.of_match_table = imx_hdp_dt_ids,
	},
};

module_platform_driver(imx_hdp_imx_platform_driver);

MODULE_AUTHOR("Sandor Yu <Sandor.yu@nxp.com>");
MODULE_DESCRIPTION("IMX8QM DP Display Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dp-hdmi-imx");
