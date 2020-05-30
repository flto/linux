// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include "dpu_common.h"

#include <linux/of_irq.h>

static void
dpu_encoder_mode_set(struct drm_encoder *encoder,
		     struct drm_display_mode *mode,
		     struct drm_display_mode *adj_mode)
{
	struct msm_drm_private *priv = encoder->dev->dev_private;

	if (encoder->encoder_type == DRM_MODE_ENCODER_TMDS)
		msm_dp_display_mode_set(priv->dp, encoder, mode, adj_mode);
}

static void
dpu_encoder_enable(struct drm_encoder *encoder)
{
	struct msm_drm_private *priv = encoder->dev->dev_private;
	int ret;

	if (encoder->encoder_type == DRM_MODE_ENCODER_TMDS) {
		msleep(100); /* XXX avoids broken state after changing DP mode */
		dpu_write(to_dpu_kms(priv->kms), INTF_TIMING_ENGINE_EN(0), 1);
	} else {
		dpu_write(to_dpu_kms(priv->kms), INTF_TIMING_ENGINE_EN(1), 1);
	}
}

static void
dpu_encoder_disable(struct drm_encoder *encoder)
{
	struct msm_drm_private *priv = encoder->dev->dev_private;
	int ret;

	if (encoder->encoder_type == DRM_MODE_ENCODER_TMDS) {
		dpu_write(to_dpu_kms(priv->kms), INTF_TIMING_ENGINE_EN(0), 0);
	} else {
		dpu_write(to_dpu_kms(priv->kms), INTF_TIMING_ENGINE_EN(1), 0);
	}
}

static const struct drm_encoder_helper_funcs dpu_encoder_helper_funcs = {
	.mode_set = dpu_encoder_mode_set,
	.disable = dpu_encoder_disable,
	.enable = dpu_encoder_enable,
};

static const struct drm_encoder_funcs dpu_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int
dpu_encoder_init(struct dpu_kms *dpu, struct drm_encoder *encoder, int type)
{
	int ret;

	ret = drm_encoder_init(dpu->dev, encoder, &dpu_encoder_funcs, type, NULL);
	if (ret)
		return ret;

	drm_encoder_helper_add(encoder, &dpu_encoder_helper_funcs);
	return 0;
}

static int dpu_kms_mmu_init(struct dpu_kms *dpu_kms)
{
	struct iommu_domain *domain;
	struct msm_gem_address_space *aspace;
	struct msm_mmu *mmu;

	domain = iommu_domain_alloc(&platform_bus_type);
	if (!domain)
		return 0;

	mmu = msm_iommu_new(dpu_kms->dev->dev, domain);
	aspace = msm_gem_address_space_create(mmu, "dpu1", 0x1000, 0xfffffff);
	if (IS_ERR(aspace)) {
		iommu_domain_free(domain);
		return PTR_ERR(aspace);
	}

	dpu_kms->base.aspace = aspace;
	return 0;
}

static int dpu_kms_hw_init(struct msm_kms *kms)
{
	struct dpu_kms *dpu = to_dpu_kms(kms);
	struct drm_device *dev;
	struct msm_drm_private *priv;
	int i, rc = -EINVAL;

	dev = dpu->dev;
	priv = dev->dev_private;

	dpu->mmio = msm_ioremap(dpu->pdev, "mdp", "mdp");
	if (IS_ERR(dpu->mmio)) {
		rc = PTR_ERR(dpu->mmio);
		DRM_ERROR("mdp register memory map failed: %d\n", rc);
		dpu->mmio = NULL;
		goto error;
	}

	dpu->vbif = msm_ioremap(dpu->pdev, "vbif", "vbif");
	if (IS_ERR(dpu->vbif)) {
		rc = PTR_ERR(dpu->vbif);
		DRM_ERROR("vbif register memory map failed: %d\n", rc);
		dpu->vbif = NULL;
		goto error;
	}

	pm_runtime_get_sync(&dpu->pdev->dev);

	dpu->core_rev = readl_relaxed(dpu->mmio + 0x0);

	printk("dpu->core_rev=%.8x\n", dpu->core_rev);

	dpu->num_blendstages = 11;
	dpu->hbb = 2; /* 3 for DDR5 */
	dpu->ubwc_version = 4;
	dpu->num_sspp = 8;

	dpu->creq_lut_linear = 0x0011222222335777;
	dpu->creq_lut_macrotile = 0x0011223344556677;

	rc = dpu_kms_mmu_init(dpu);
	if (rc) {
		DRM_ERROR("dpu_kms_mmu_init failed: %d\n", rc);
		goto power_error;
	}

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = 2560 * 2; // or 4096 max plane width??
	dev->mode_config.max_height = 4096;
	dev->mode_config.allow_fb_modifiers = true;

	rc = dpu_encoder_init(dpu, &dpu->encoder[0], DRM_MODE_ENCODER_DSI);
	if (rc) {
		DRM_ERROR("encoder init failed, rc = %d\n", rc);
		return rc;
	}

	rc = dpu_encoder_init(dpu, &dpu->encoder[1], DRM_MODE_ENCODER_TMDS);
	if (rc) {
		DRM_ERROR("encoder init failed, rc = %d\n", rc);
		return rc;
	}

	for (i = 0; i < ARRAY_SIZE(priv->dsi); i++) {
		if (!priv->dsi[i])
			continue;

		rc = msm_dsi_modeset_init(priv->dsi[i], dev, &dpu->encoder[0]);
		if (rc) {
			DRM_ERROR("modeset_init failed for dsi[%d], rc = %d\n",
				i, rc);
			return rc; // XXX proper free path
		}
	}

	rc = msm_dp_modeset_init(priv->dp, dev, &dpu->encoder[1]);
	if (rc) {
		DRM_ERROR("modeset_init failed for DP, rc = %d\n", rc);
		return rc; // XXX proper free path
	}

	/* only create 1/2 of the planes - some cases use 2 HW planes as 1 plane */
	for (i = 0; i < 2; i++) { // dpu->num_sspp / 2
		rc = dpu_plane_init(dpu, &dpu->plane[i].base);
		if (rc) {
			DRM_ERROR("plane init failed, rc = %d\n", rc);
			return rc; // XXX proper free path
		}
		dpu->plane[i].id = i;
	}

	rc = dpu_crtc_init(dpu, &dpu->crtc[0].base, &dpu->plane[0].base);
	if (rc) {
		DRM_ERROR("encoder init failed, rc = %d\n", rc);
		return rc;
	}

	rc = dpu_crtc_init(dpu, &dpu->crtc[1].base, &dpu->plane[1].base);
	if (rc) {
		DRM_ERROR("encoder init failed, rc = %d\n", rc);
		return rc;
	}

	dpu->encoder[0].possible_crtcs |= 1;
	dpu->encoder[1].possible_crtcs |= 2;

	init_waitqueue_head(&dpu->crtc[0].pending_kickoff_wq);
	init_waitqueue_head(&dpu->crtc[1].pending_kickoff_wq);

	vbif_write(dpu, VBIF_OUT_AXI_AMEMTYPE_CONF0, 0x33333333);
	vbif_write(dpu, VBIF_OUT_AXI_AMEMTYPE_CONF1, 0x00333333);

	dpu->intr_en = 0;//INTF_VSYNC(0) | INTF_VSYNC(1);
	dpu->intr2_en = 0;//INTR2_CTL_START(0) | INTR2_CTL_START(2);

	spin_lock_init(&dpu->irq_lock);

	dpu_write(dpu, INTR2_EN, dpu->intr2_en);
	dpu_write(dpu, INTR_EN, dpu->intr_en);

	dpu_write(dpu, INTR_CLEAR, ~0u);
	dpu_write(dpu, INTR2_CLEAR, ~0u);

	//reset_ubwc(priv);

	/* assign resources */
	dpu->crtc[0].ctl = 0;
	dpu->crtc[0].intf = 1;
	dpu->crtc[0].lm = 0;
	dpu->crtc[0].is_dual_dsi = true;

	dpu->crtc[1].ctl = 2;
	dpu->crtc[1].intf = 0;
	dpu->crtc[1].lm = 4;
	dpu->crtc[1].is_dp = true;

	/* used by msm_drv.c */
	priv->crtcs[0] = &dpu->crtc[0].base;
	priv->crtcs[1] = &dpu->crtc[1].base;
	priv->num_crtcs = 2;

	//pm_runtime_put_sync(&dpu_kms->pdev->dev);

// ./modetest -s 34:640x480 -s 33:2160x1080 -P 35@67:1080x1080 -P 39@68:640x480
// ./modetest -s 33:2160x1080
// ./modetest -w 33:DPMS:3
// ./modetest -a -s 33:2160x2160 -P 35@67:2160x2160+0+0
// ./modetest -a -s 33:2160x2160 -P 35@51:4320x2160 -s 34:2880x1600 -P 39@52:2880x1600

	return 0;

power_error:
	pm_runtime_put_sync(&dpu->pdev->dev);
error:
	//_dpu_kms_hw_destroy(dpu);

	return rc;
}

static void dpu_irq_preinstall(struct msm_kms *kms)
{
}

static void dpu_irq_uninstall(struct msm_kms *kms)
{
}

void dpu_complete_flip(struct dpu_kms *dpu, struct dpu_crtc *c)
{
	unsigned long flags;

	spin_lock_irqsave(&dpu->dev->event_lock, flags);
	if (c->event) {
		drm_crtc_send_vblank_event(&c->base, c->event);
		c->event = NULL;
	}
	spin_unlock_irqrestore(&dpu->dev->event_lock, flags);
}

void dpu_vblank(struct dpu_kms *dpu, struct dpu_crtc *c)
{
	drm_crtc_handle_vblank(&c->base);
}

static irqreturn_t dpu_irq(struct msm_kms *kms)
{
	struct dpu_kms *dpu = to_dpu_kms(kms);
	u32 status1, status2;

	status1 = dpu_read(dpu, INTR_STATUS);
	status2 = dpu_read(dpu, INTR2_STATUS);
	dpu_write(dpu, INTR_CLEAR, status1);
	dpu_write(dpu, INTR2_CLEAR, status2);

	//printk("dpu_irq: %.8x %.8x\n", status1, status2);

	if (status1 & INTF_VSYNC(0)) {
		dpu_vblank(dpu, &dpu->crtc[1]);
	}

	if (status1 & INTF_VSYNC(1)) {
		dpu_vblank(dpu, &dpu->crtc[0]);
	}

	if (status2 & INTR2_CTL_START(0)) {
		wake_up_all(&dpu->crtc[0].pending_kickoff_wq);
		dpu_toggle_start_irq(dpu, 0, false);
		dpu_complete_flip(dpu, &dpu->crtc[0]);
	}

	if (status2 & INTR2_CTL_START(2)) {
		wake_up_all(&dpu->crtc[1].pending_kickoff_wq);
		dpu_toggle_start_irq(dpu, 2, false);
		dpu_complete_flip(dpu, &dpu->crtc[1]);
	}

	return IRQ_HANDLED;
}

static void dpu_kms_enable_commit(struct msm_kms *kms)
{
	/* nothing to do */
}

static void dpu_kms_disable_commit(struct msm_kms *kms)
{
	/* nothing to do */
}

static ktime_t
dpu_kms_vsync_time(struct msm_kms *kms, struct drm_crtc *crtc)
{
	struct dpu_kms *dpu = to_dpu_kms(kms);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct dpu_crtc *c = to_dpu_crtc(crtc);
	u64 tmp;
	u32 line;

	line = dpu_read(dpu, INTF_LINE_COUNT(c->intf));

	if (line >= mode->vtotal) {
		DRM_WARN("line (%d) > vtotal (%d)\n", line, mode->vtotal);
		line = 0;
	}

	tmp = (u64) mode->htotal * (mode->vtotal - line) * 1000000000;
	tmp = DIV_ROUND_UP_ULL(tmp, mode->clock * 1000);

	return ktime_add_ns(ktime_get(), tmp);
}

static void
dpu_kms_prepare_commit(struct msm_kms *kms, struct drm_atomic_state *state)
{
	/* nothing */
}

static void
dpu_kms_flush_commit(struct msm_kms *kms, unsigned crtc_mask)
{
	/* nothing */
}

static void dpu_kms_wait_flush(struct msm_kms *kms, unsigned crtc_mask)
{
	struct dpu_kms *dpu = to_dpu_kms(kms);
	struct drm_crtc *crtc;
	long ret;

	for_each_crtc_mask(dpu->dev, crtc, crtc_mask) {
		struct dpu_crtc *c = to_dpu_crtc(crtc);

		ret = wait_event_timeout(c->pending_kickoff_wq,
			(dpu_read(dpu, CTL_FLUSH(c->ctl)) == 0),
			msecs_to_jiffies(50));
		if (!ret) {
			DRM_ERROR("flush timeout\n");
			dpu_complete_flip(dpu, c); // XXX
			break;
		}
	}

	return;
}

static void dpu_kms_complete_commit(struct msm_kms *kms, unsigned crtc_mask)
{
	/* nothing to do */
}

void dpu_toggle_start_irq(struct dpu_kms *dpu, u32 ctl, bool enable)
{
	unsigned long flags;

	spin_lock_irqsave(&dpu->irq_lock, flags);

	if (enable) {
		dpu->intr2_en |= INTR2_CTL_START(ctl);
		dpu_write(dpu, INTR2_CLEAR, INTR2_CTL_START(ctl));
	} else
		dpu->intr2_en &= ~INTR2_CTL_START(ctl);
	dpu_write(dpu, INTR2_EN, dpu->intr2_en);

	spin_unlock_irqrestore(&dpu->irq_lock, flags);
}

static int dpu_kms_enable_vblank(struct msm_kms *kms, struct drm_crtc *crtc)
{
	struct dpu_kms *dpu = to_dpu_kms(kms);
	struct dpu_crtc *c = to_dpu_crtc(crtc);
	unsigned long flags;

	spin_lock_irqsave(&dpu->irq_lock, flags);

	dpu->intr_en |= INTF_VSYNC(c->intf);
	dpu_write(dpu, INTR_CLEAR, INTF_VSYNC(c->intf));
	dpu_write(dpu, INTR_EN, dpu->intr_en);

	spin_unlock_irqrestore(&dpu->irq_lock, flags);

	return 0;
}

static void dpu_kms_disable_vblank(struct msm_kms *kms, struct drm_crtc *crtc)
{
	struct dpu_kms *dpu = to_dpu_kms(kms);
	struct dpu_crtc *c = to_dpu_crtc(crtc);
	unsigned long flags;

	spin_lock_irqsave(&dpu->irq_lock, flags);

	dpu->intr_en &= ~INTF_VSYNC(c->intf);
	dpu_write(dpu, INTR_EN, dpu->intr_en);
	wmb();

	spin_unlock_irqrestore(&dpu->irq_lock, flags);
}

static const struct msm_format dummy_format;

static const struct msm_format *
dpu_get_msm_format(struct msm_kms *kms, uint32_t format, uint64_t modifier)
{
	/* return non-NULL to say format is supported */
	return &dummy_format;
}

static long
dpu_kms_round_pixclk(struct msm_kms *kms,
		     unsigned long rate,
		     struct drm_encoder *encoder)
{
	return rate;
}

static void dpu_kms_destroy(struct msm_kms *kms)
{
	DRM_ERROR("TODO KMS DESTROY");
}

static void
dpu_kms_set_encoder_mode(struct msm_kms *kms,
			 struct drm_encoder *encoder,
			 bool cmd_mode)
{
	/* TODO: cmd_mode not supported */
}

static int dpu_kms_debugfs_init(struct msm_kms *kms, struct drm_minor *minor)
{
	return 0;
}

static const struct msm_kms_funcs kms_funcs = {
	.hw_init         = dpu_kms_hw_init,
	.irq_preinstall  = dpu_irq_preinstall,
	.irq_uninstall   = dpu_irq_uninstall,
	.irq             = dpu_irq,
	.enable_commit   = dpu_kms_enable_commit,
	.disable_commit  = dpu_kms_disable_commit,
	// .vsync_time      = dpu_kms_vsync_time, enable for async commit
	.prepare_commit  = dpu_kms_prepare_commit,
	.flush_commit    = dpu_kms_flush_commit,
	.wait_flush      = dpu_kms_wait_flush,
	.complete_commit = dpu_kms_complete_commit,
	.enable_vblank   = dpu_kms_enable_vblank,
	.disable_vblank  = dpu_kms_disable_vblank,
	.get_format      = dpu_get_msm_format,
	.round_pixclk    = dpu_kms_round_pixclk,
	.destroy         = dpu_kms_destroy,
	.set_encoder_mode = dpu_kms_set_encoder_mode,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init    = dpu_kms_debugfs_init,
#endif
};

struct msm_kms *dpu_kms_init(struct drm_device *dev)
{
	struct msm_drm_private *priv;
	struct dpu_kms *dpu_kms;
	int irq;

	priv = dev->dev_private;
	dpu_kms = to_dpu_kms(priv->kms);

	irq = irq_of_parse_and_map(dpu_kms->pdev->dev.of_node, 0);
	if (irq < 0) {
		DRM_ERROR("failed to get irq: %d\n", irq);
		return ERR_PTR(irq);
	}
	dpu_kms->base.irq = irq;

	return &dpu_kms->base;
}

static int dpu_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *ddev = dev_get_drvdata(master);
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_drm_private *priv = ddev->dev_private;
	struct dpu_kms *dpu_kms;
	struct dss_module_power *mp;
	int ret;

	dpu_kms = devm_kzalloc(&pdev->dev, sizeof(*dpu_kms), GFP_KERNEL);
	if (!dpu_kms)
		return -ENOMEM;

	mp = &dpu_kms->mp;
	ret = msm_dss_parse_clock(pdev, mp);
	if (ret) {
		DRM_ERROR("failed to parse clocks, ret=%d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, dpu_kms);

	msm_kms_init(&dpu_kms->base, &kms_funcs);
	dpu_kms->dev = ddev;
	dpu_kms->pdev = pdev;

	pm_runtime_enable(&pdev->dev);

	priv->kms = &dpu_kms->base;
	return 0;
}

static void dpu_unbind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dpu_kms *dpu_kms = platform_get_drvdata(pdev);
	struct dss_module_power *mp = &dpu_kms->mp;

	msm_dss_put_clk(mp->clk_config, mp->num_clk);
	devm_kfree(&pdev->dev, mp->clk_config);
	mp->num_clk = 0;
}

static const struct component_ops dpu_ops = {
	.bind   = dpu_bind,
	.unbind = dpu_unbind,
};

static int dpu_dev_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &dpu_ops);
}

static int dpu_dev_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dpu_ops);
	return 0;
}

static int __maybe_unused dpu_runtime_suspend(struct device *dev)
{
	int rc = -1;
	struct platform_device *pdev = to_platform_device(dev);
	struct dpu_kms *dpu_kms = platform_get_drvdata(pdev);
	struct dss_module_power *mp = &dpu_kms->mp;

	rc = msm_dss_enable_clk(mp->clk_config, mp->num_clk, false);
	if (rc)
		DRM_ERROR("clock disable failed rc:%d\n", rc);

	return rc;
}

static int __maybe_unused dpu_runtime_resume(struct device *dev)
{
	int rc = -1;
	struct platform_device *pdev = to_platform_device(dev);
	struct dpu_kms *dpu_kms = platform_get_drvdata(pdev);
	struct drm_device *ddev;
	struct dss_module_power *mp = &dpu_kms->mp;

	ddev = dpu_kms->dev;
	rc = msm_dss_enable_clk(mp->clk_config, mp->num_clk, true);
	if (rc) {
		DRM_ERROR("clock enable failed rc:%d\n", rc);
		return rc;
	}

	// dpu_vbif_init_memtypes(dpu_kms);

	return rc;
}

static const struct dev_pm_ops dpu_pm_ops = {
	SET_RUNTIME_PM_OPS(dpu_runtime_suspend, dpu_runtime_resume, NULL)
};

static const struct of_device_id dpu_dt_match[] = {
	{ .compatible = "qcom,sm8150-dpu", },
	{ .compatible = "qcom,sm8250-dpu", },
	{}
};
MODULE_DEVICE_TABLE(of, dpu_dt_match);

static struct platform_driver dpu_driver = {
	.probe = dpu_dev_probe,
	.remove = dpu_dev_remove,
	.driver = {
		.name = "msm_dpu",
		.of_match_table = dpu_dt_match,
		.pm = &dpu_pm_ops,
	},
};

void __init msm_dpu_register(void)
{
	platform_driver_register(&dpu_driver);
}

void __exit msm_dpu_unregister(void)
{
	platform_driver_unregister(&dpu_driver);
}
