#include <drm/drm_drv.h>
#include <drm/drm_vblank.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "dpu_io_util.h"

#include "msm_drv.h"
#include "msm_kms.h"
#include "msm_mmu.h"
#include "msm_gem.h"

#include "dpu_hw_regs.h"

struct dpu_crtc {
	struct drm_crtc base;

	struct drm_pending_vblank_event *event;

	wait_queue_head_t pending_kickoff_wq;

	/* resource configuration */
	u8 intf; /* master intf */
	u8 ctl;  /* ctl */
	u8 lm; /* first mixer, also pp */

	bool is_dual_dsi;
	bool is_dp;
};
#define to_dpu_crtc(x) container_of(x, struct dpu_crtc, base)

struct dpu_plane {
	struct drm_plane base;
	u8 id;

};
#define to_dpu_plane(x) container_of(x, struct dpu_plane, base)

struct dpu_kms {
	struct msm_kms base;
	struct drm_device *dev;
	int core_rev;

	void __iomem *mmio, *vbif;

	struct platform_device *pdev;
	struct dss_module_power mp;

	/* irq state */
	spinlock_t irq_lock;
	u32 intr_en;
	u32 intr2_en;

	/* drm state */
	struct dpu_plane plane[8];
	struct dpu_crtc crtc[2];
	struct drm_encoder encoder[2];

	/* hw parameters (derived from DPU version) */
	u8 num_blendstages, num_sspp;
	u8 ubwc_version, hbb;
	u64 creq_lut_linear;
	u64 creq_lut_macrotile;
};
#define to_dpu_kms(x) container_of(x, struct dpu_kms, base)

static inline void dpu_write(struct dpu_kms *dpu, u32 reg, u32 val, const char *name)
{
	//if (reg != 0x18 && reg != 0x2c)
	//	printk("dpu_write: %.5x %s: 0x%X -> 0x%X\n", reg, name, readl_relaxed(dpu->mmio + reg), val);
	writel_relaxed(val, dpu->mmio + reg);
}
#define dpu_write(dpu, reg, val) dpu_write(dpu, reg, val, #reg)

static inline u32 dpu_read(struct dpu_kms *dpu, u32 reg)
{
	return readl_relaxed(dpu->mmio + reg);
}

static inline void vbif_write(struct dpu_kms *dpu, u32 reg, u32 val)
{
	writel_relaxed(val, dpu->vbif + reg);
}

void dpu_toggle_start_irq(struct dpu_kms *dpu, u32 ctl, bool enable);

void dpu_plane_update(struct dpu_kms *dpu, u32 i, struct drm_plane_state *state);
void dpu_complete_flip(struct dpu_kms *dpu, struct dpu_crtc *c);

int dpu_crtc_init(struct dpu_kms *dpu, struct drm_crtc *crtc, struct drm_plane *plane);
int dpu_plane_init(struct dpu_kms *dpu, struct drm_plane *plane);
