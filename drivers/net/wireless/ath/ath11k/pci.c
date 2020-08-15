// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2019-2020 The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/msi.h>
#include <linux/pci.h>

#include "pci.h"
#include "core.h"
#include "hif.h"
#include "mhi.h"
#include "debug.h"

#define ATH11K_PCI_BAR_NUM		0
#define ATH11K_PCI_DMA_MASK		32

#define ATH11K_PCI_IRQ_CE0_OFFSET		3

#define WINDOW_ENABLE_BIT		0x40000000
#define WINDOW_REG_ADDRESS		0x310c
#define WINDOW_VALUE_MASK		GENMASK(24, 19)
#define WINDOW_START			0x80000
#define WINDOW_RANGE_MASK		GENMASK(18, 0)

#define QCA6390_DEVICE_ID		0x1101

static const struct pci_device_id ath11k_pci_id_table[] = {
	{ PCI_VDEVICE(QCOM, QCA6390_DEVICE_ID) },
	{0}
};

MODULE_DEVICE_TABLE(pci, ath11k_pci_id_table);

static const struct ath11k_bus_params ath11k_pci_bus_params = {
	.mhi_support = true,
	.m3_fw_support = true,
	.fixed_bdf_addr = false,
	.fixed_mem_region = false,
};

static const struct ath11k_msi_config msi_config = {
	.total_vectors = 32,
	.total_users = 4,
	.users = (struct ath11k_msi_user[]) {
		{ .name = "MHI", .num_vectors = 3, .base_vector = 0 },
		{ .name = "CE", .num_vectors = 10, .base_vector = 3 },
		{ .name = "WAKE", .num_vectors = 1, .base_vector = 13 },
		{ .name = "DP", .num_vectors = 18, .base_vector = 14 },
	},
};

/* Target firmware's Copy Engine configuration. */
static const struct ce_pipe_config target_ce_config_wlan[] = {
	/* CE0: host->target HTC control and raw streams */
	{
		.pipenum = __cpu_to_le32(0),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE1: target->host HTT + HTC control */
	{
		.pipenum = __cpu_to_le32(1),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE2: target->host WMI */
	{
		.pipenum = __cpu_to_le32(2),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE3: host->target WMI */
	{
		.pipenum = __cpu_to_le32(3),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE4: host->target HTT */
	{
		.pipenum = __cpu_to_le32(4),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(256),
		.nbytes_max = __cpu_to_le32(256),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS | CE_ATTR_DIS_INTR),
		.reserved = __cpu_to_le32(0),
	},

	/* CE5: target->host Pktlog */
	{
		.pipenum = __cpu_to_le32(5),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE6: Reserved for target autonomous hif_memcpy */
	{
		.pipenum = __cpu_to_le32(6),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(16384),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE7 used only by Host */
	{
		.pipenum = __cpu_to_le32(7),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT_H2H),
		.nentries = __cpu_to_le32(0),
		.nbytes_max = __cpu_to_le32(0),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS | CE_ATTR_DIS_INTR),
		.reserved = __cpu_to_le32(0),
	},

	/* CE8 target->host used only by IPA */
	{
		.pipenum = __cpu_to_le32(8),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(16384),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},
	/* CE 9, 10, 11 are used by MHI driver */
};

/* Map from service/endpoint to Copy Engine.
 * This table is derived from the CE_PCI TABLE, above.
 * It is passed to the Target at startup for use by firmware.
 */
static const struct service_to_pipe target_service_to_ce_map_wlan[] = {
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VO),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VO),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BK),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BK),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BE),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BE),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VI),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VI),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},

	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_RSVD_CTRL),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(0),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_RSVD_CTRL),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},

	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_HTT_DATA_MSG),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(4),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_HTT_DATA_MSG),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(1),
	},

	/* (Additions here) */

	{ /* must be last */
		__cpu_to_le32(0),
		__cpu_to_le32(0),
		__cpu_to_le32(0),
	},
};

static const char *irq_name[ATH11K_IRQ_NUM_MAX] = {
	"bhi",
	"mhi-er0",
	"mhi-er1",
	"ce0",
	"ce1",
	"ce2",
	"ce3",
	"ce4",
	"ce5",
	"ce6",
	"ce7",
	"ce8",
	"ce9",
	"ce10",
	"ce11",
	"host2wbm-desc-feed",
	"host2reo-re-injection",
	"host2reo-command",
	"host2rxdma-monitor-ring3",
	"host2rxdma-monitor-ring2",
	"host2rxdma-monitor-ring1",
	"reo2ost-exception",
	"wbm2host-rx-release",
	"reo2host-status",
	"reo2host-destination-ring4",
	"reo2host-destination-ring3",
	"reo2host-destination-ring2",
	"reo2host-destination-ring1",
	"rxdma2host-monitor-destination-mac3",
	"rxdma2host-monitor-destination-mac2",
	"rxdma2host-monitor-destination-mac1",
	"ppdu-end-interrupts-mac3",
	"ppdu-end-interrupts-mac2",
	"ppdu-end-interrupts-mac1",
	"rxdma2host-monitor-status-ring-mac3",
	"rxdma2host-monitor-status-ring-mac2",
	"rxdma2host-monitor-status-ring-mac1",
	"host2rxdma-host-buf-ring-mac3",
	"host2rxdma-host-buf-ring-mac2",
	"host2rxdma-host-buf-ring-mac1",
	"rxdma2host-destination-ring-mac3",
	"rxdma2host-destination-ring-mac2",
	"rxdma2host-destination-ring-mac1",
	"host2tcl-input-ring4",
	"host2tcl-input-ring3",
	"host2tcl-input-ring2",
	"host2tcl-input-ring1",
	"wbm2host-tx-completions-ring3",
	"wbm2host-tx-completions-ring2",
	"wbm2host-tx-completions-ring1",
	"tcl2host-status-ring",
};

static inline void ath11k_pci_select_window(struct ath11k_pci *ab_pci, u32 offset)
{
	struct ath11k_base *ab = ab_pci->ab;

	u32 window = FIELD_GET(WINDOW_VALUE_MASK, offset);

	lockdep_assert_held(&ab_pci->window_lock);

	if (window != ab_pci->register_window) {
		iowrite32(WINDOW_ENABLE_BIT | window,
			  ab->mem + WINDOW_REG_ADDRESS);
		ab_pci->register_window = window;
	}
}

static void ath11k_pci_write32(struct ath11k_base *ab, u32 offset, u32 value)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);

	if (offset < WINDOW_START) {
		iowrite32(value, ab->mem  + offset);
	} else {
		spin_lock_bh(&ab_pci->window_lock);
		ath11k_pci_select_window(ab_pci, offset);
		iowrite32(value, ab->mem + WINDOW_START + (offset & WINDOW_RANGE_MASK));
		spin_unlock_bh(&ab_pci->window_lock);
	}
}

static u32 ath11k_pci_read32(struct ath11k_base *ab, u32 offset)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);
	u32 val;

	if (offset < WINDOW_START) {
		val = ioread32(ab->mem + offset);
	} else {
		spin_lock_bh(&ab_pci->window_lock);
		ath11k_pci_select_window(ab_pci, offset);
		val = ioread32(ab->mem + WINDOW_START + (offset & WINDOW_RANGE_MASK));
		spin_unlock_bh(&ab_pci->window_lock);
	}

	return val;
}

int ath11k_pci_get_msi_irq(struct device *dev, unsigned int vector)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);

	return pci_irq_vector(pci_dev, vector);
}

static void ath11k_pci_get_msi_address(struct ath11k_base *ab, u32 *msi_addr_lo,
				       u32 *msi_addr_hi)
{
	struct pci_dev *pci_dev = to_pci_dev(ab->dev);

	pci_read_config_dword(pci_dev, pci_dev->msi_cap + PCI_MSI_ADDRESS_LO,
			      msi_addr_lo);

	pci_read_config_dword(pci_dev, pci_dev->msi_cap + PCI_MSI_ADDRESS_HI,
			      msi_addr_hi);
}

int ath11k_pci_get_user_msi_assignment(struct ath11k_pci *ab_pci, char *user_name,
				       int *num_vectors, u32 *user_base_data,
				       u32 *base_vector)
{
	struct ath11k_base *ab = ab_pci->ab;
	int idx;

	for (idx = 0; idx < msi_config.total_users; idx++) {
		if (strcmp(user_name, msi_config.users[idx].name) == 0) {
			*num_vectors = msi_config.users[idx].num_vectors;
			*user_base_data = msi_config.users[idx].base_vector
				+ ab_pci->msi_ep_base_data;
			*base_vector = msi_config.users[idx].base_vector;

			ath11k_dbg(ab, ATH11K_DBG_PCI, "Assign MSI to user: %s, num_vectors: %d, user_base_data: %u, base_vector: %u\n",
				   user_name, *num_vectors, *user_base_data,
				   *base_vector);

			return 0;
		}
	}

	ath11k_err(ab, "Failed to find MSI assignment for %s!\n", user_name);

	return -EINVAL;
}

static int ath11k_get_user_msi_assignment(struct ath11k_base *ab, char *user_name,
					  int *num_vectors, u32 *user_base_data,
					  u32 *base_vector)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);

	return ath11k_pci_get_user_msi_assignment(ab_pci, user_name,
						  num_vectors, user_base_data,
						  base_vector);
}

static void ath11k_pci_free_irq(struct ath11k_base *ab)
{
	int i, irq_idx;

	for (i = 0; i < CE_COUNT; i++) {
		if (ath11k_ce_get_attr_flags(i) & CE_ATTR_DIS_INTR)
			continue;
		irq_idx = ATH11K_PCI_IRQ_CE0_OFFSET + i;
		free_irq(ab->irq_num[irq_idx], &ab->ce.ce_pipe[i]);
	}
}

static void ath11k_pci_ce_irq_disable(struct ath11k_base *ab, u16 ce_id)
{
	u32 irq_idx;

	irq_idx = ATH11K_PCI_IRQ_CE0_OFFSET + ce_id;
	disable_irq_nosync(ab->irq_num[irq_idx]);
}

static irqreturn_t ath11k_pci_ce_interrupt_handler(int irq, void *arg)
{
	struct ath11k_ce_pipe *ce_pipe = arg;

	ath11k_pci_ce_irq_disable(ce_pipe->ab, ce_pipe->pipe_num);

	return IRQ_HANDLED;
}

static int ath11k_pci_config_irq(struct ath11k_base *ab)
{
	struct ath11k_ce_pipe *ce_pipe;
	u32 msi_data_start;
	u32 msi_data_count;
	u32 msi_irq_start;
	unsigned int msi_data;
	int irq, i, ret, irq_idx;

	ret = ath11k_pci_get_user_msi_assignment(ath11k_pci_priv(ab),
						 "CE", &msi_data_count,
						 &msi_data_start, &msi_irq_start);
	if (ret)
		return ret;

	/* Configure CE irqs */
	for (i = 0; i < CE_COUNT; i++) {
		msi_data = (i % msi_data_count) + msi_irq_start;
		irq = ath11k_pci_get_msi_irq(ab->dev, msi_data);
		ce_pipe = &ab->ce.ce_pipe[i];

		if (ath11k_ce_get_attr_flags(i) & CE_ATTR_DIS_INTR)
			continue;

		irq_idx = ATH11K_PCI_IRQ_CE0_OFFSET + i;

		ret = request_irq(irq, ath11k_pci_ce_interrupt_handler,
				  IRQF_SHARED, irq_name[irq_idx],
				  ce_pipe);
		if (ret) {
			ath11k_err(ab, "failed to request irq %d: %d\n",
				   irq_idx, ret);
			return ret;
		}

		ab->irq_num[irq_idx] = irq;
	}

	return 0;
}

static void ath11k_pci_init_qmi_ce_config(struct ath11k_base *ab)
{
	struct ath11k_qmi_ce_cfg *cfg = &ab->qmi.ce_cfg;

	cfg->tgt_ce = target_ce_config_wlan;
	cfg->tgt_ce_len = ARRAY_SIZE(target_ce_config_wlan);

	cfg->svc_to_ce_map = target_service_to_ce_map_wlan;
	cfg->svc_to_ce_map_len = ARRAY_SIZE(target_service_to_ce_map_wlan);
	ab->qmi.service_ins_id = ATH11K_QMI_WLFW_SERVICE_INS_ID_V01_QCA6390;
}

static void ath11k_pci_ce_irq_enable(struct ath11k_base *ab, u16 ce_id)
{
	u32 irq_idx;

	irq_idx = ATH11K_PCI_IRQ_CE0_OFFSET + ce_id;
	enable_irq(ab->irq_num[irq_idx]);
}

static void ath11k_pci_ce_irqs_enable(struct ath11k_base *ab)
{
	int i;

	for (i = 0; i < CE_COUNT; i++) {
		if (ath11k_ce_get_attr_flags(i) & CE_ATTR_DIS_INTR)
			continue;
		ath11k_pci_ce_irq_enable(ab, i);
	}
}

static int ath11k_pci_enable_msi(struct ath11k_pci *ab_pci)
{
	struct ath11k_base *ab = ab_pci->ab;
	struct msi_desc *msi_desc;
	int num_vectors;
	int ret;

	num_vectors = pci_alloc_irq_vectors(ab_pci->pdev,
					    msi_config.total_vectors,
					    msi_config.total_vectors,
					    PCI_IRQ_MSI);
	if (num_vectors != msi_config.total_vectors) {
		ath11k_err(ab, "failed to get %d MSI vectors, only %d available",
			   msi_config.total_vectors, num_vectors);

		if (num_vectors >= 0)
			return -EINVAL;
		else
			return num_vectors;
	}

	msi_desc = irq_get_msi_desc(ab_pci->pdev->irq);
	if (!msi_desc) {
		ath11k_err(ab, "msi_desc is NULL!\n");
		ret = -EINVAL;
		goto free_msi_vector;
	}

	ab_pci->msi_ep_base_data = msi_desc->msg.data;

	ath11k_dbg(ab, ATH11K_DBG_PCI, "msi base data is %d\n", ab_pci->msi_ep_base_data);

	return 0;

free_msi_vector:
	pci_free_irq_vectors(ab_pci->pdev);

	return ret;
}

static void ath11k_pci_disable_msi(struct ath11k_pci *ab_pci)
{
	pci_free_irq_vectors(ab_pci->pdev);
}

static int ath11k_pci_claim(struct ath11k_pci *ab_pci, struct pci_dev *pdev)
{
	struct ath11k_base *ab = ab_pci->ab;
	u16 device_id;
	int ret = 0;

	pci_read_config_word(pdev, PCI_DEVICE_ID, &device_id);
	if (device_id != ab_pci->dev_id)  {
		ath11k_err(ab, "pci device id mismatch: 0x%x 0x%x\n",
			   device_id, ab_pci->dev_id);
		ret = -EIO;
		goto out;
	}

	ret = pci_assign_resource(pdev, ATH11K_PCI_BAR_NUM);
	if (ret) {
		ath11k_err(ab, "failed to assign pci resource: %d\n", ret);
		goto out;
	}

	ret = pci_enable_device(pdev);
	if (ret) {
		ath11k_err(ab, "failed to enable pci device: %d\n", ret);
		goto out;
	}

	ret = pci_request_region(pdev, ATH11K_PCI_BAR_NUM, "ath11k_pci");
	if (ret) {
		ath11k_err(ab, "failed to request pci region: %d\n", ret);
		goto disable_device;
	}

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(ATH11K_PCI_DMA_MASK));
	if (ret) {
		ath11k_err(ab, "failed to set pci dma mask to %d: %d\n",
			   ATH11K_PCI_DMA_MASK, ret);
		goto release_region;
	}

	ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(ATH11K_PCI_DMA_MASK));
	if (ret) {
		ath11k_err(ab, "failed to set pci consistent dma mask to %d: %d\n",
			   ATH11K_PCI_DMA_MASK, ret);
		goto release_region;
	}

	pci_set_master(pdev);

	ab->mem_len = pci_resource_len(pdev, ATH11K_PCI_BAR_NUM);
	ab->mem = pci_iomap(pdev, ATH11K_PCI_BAR_NUM, 0);
	if (!ab->mem) {
		ath11k_err(ab, "failed to map pci bar %d\n", ATH11K_PCI_BAR_NUM);
		ret = -EIO;
		goto clear_master;
	}

	ath11k_dbg(ab, ATH11K_DBG_BOOT, "boot pci_mem 0x%pK\n", ab->mem);
	return 0;

clear_master:
	pci_clear_master(pdev);
release_region:
	pci_release_region(pdev, ATH11K_PCI_BAR_NUM);
disable_device:
	pci_disable_device(pdev);
out:
	return ret;
}

static void ath11k_pci_free_region(struct ath11k_pci *ab_pci)
{
	struct ath11k_base *ab = ab_pci->ab;
	struct pci_dev *pci_dev = ab_pci->pdev;

	pci_iounmap(pci_dev, ab->mem);
	ab->mem = NULL;
	pci_clear_master(pci_dev);
	pci_release_region(pci_dev, ATH11K_PCI_BAR_NUM);
	if (pci_is_enabled(pci_dev))
		pci_disable_device(pci_dev);
}

static int ath11k_pci_power_up(struct ath11k_base *ab)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);
	int ret;

	ret = ath11k_mhi_start(ab_pci);
	if (ret) {
		ath11k_err(ab, "failed to start mhi: %d\n", ret);
		return ret;
	}

	return 0;
}

static void ath11k_pci_power_down(struct ath11k_base *ab)
{
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);

	ath11k_mhi_stop(ab_pci);
}

static void ath11k_pci_stop(struct ath11k_base *ab)
{
	ath11k_ce_cleanup_pipes(ab);
}

static int ath11k_pci_start(struct ath11k_base *ab)
{
	ath11k_pci_ce_irqs_enable(ab);

	return 0;
}

static const struct ath11k_hif_ops ath11k_pci_hif_ops = {
	.start = ath11k_pci_start,
	.stop = ath11k_pci_stop,
	.read32 = ath11k_pci_read32,
	.write32 = ath11k_pci_write32,
	.power_down = ath11k_pci_power_down,
	.power_up = ath11k_pci_power_up,
	.get_msi_address =  ath11k_pci_get_msi_address,
	.get_user_msi_vector = ath11k_get_user_msi_assignment,
};

static int ath11k_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *pci_dev)
{
	struct ath11k_base *ab;
	struct ath11k_pci *ab_pci;
	enum ath11k_hw_rev hw_rev;
	int ret;

	dev_warn(&pdev->dev, "WARNING: ath11k PCI support is experimental!\n");

	switch (pci_dev->device) {
	case QCA6390_DEVICE_ID:
		hw_rev = ATH11K_HW_QCA6390_HW20;
		break;
	default:
		dev_err(&pdev->dev, "Unknown PCI device found: 0x%x\n",
			pci_dev->device);
		return -ENOTSUPP;
	}

	ab = ath11k_core_alloc(&pdev->dev, sizeof(*ab_pci), ATH11K_BUS_PCI,
			       &ath11k_pci_bus_params);
	if (!ab) {
		dev_err(&pdev->dev, "failed to allocate ath11k base\n");
		return -ENOMEM;
	}

	ab->dev = &pdev->dev;
	ab->hw_rev = hw_rev;
	pci_set_drvdata(pdev, ab);
	ab_pci = ath11k_pci_priv(ab);
	ab_pci->dev_id = pci_dev->device;
	ab_pci->ab = ab;
	ab_pci->pdev = pdev;
	ab->hif.ops = &ath11k_pci_hif_ops;
	pci_set_drvdata(pdev, ab);
	spin_lock_init(&ab_pci->window_lock);

	ret = ath11k_pci_claim(ab_pci, pdev);
	if (ret) {
		ath11k_err(ab, "failed to claim device: %d\n", ret);
		goto err_free_core;
	}

	ret = ath11k_pci_enable_msi(ab_pci);
	if (ret) {
		ath11k_err(ab, "failed to enable msi: %d\n", ret);
		goto err_pci_free_region;
	}

	ret = ath11k_core_pre_init(ab);
	if (ret)
		goto err_pci_disable_msi;

	ret = ath11k_mhi_register(ab_pci);
	if (ret) {
		ath11k_err(ab, "failed to register mhi: %d\n", ret);
		goto err_pci_disable_msi;
	}

	ret = ath11k_hal_srng_init(ab);
	if (ret)
		goto err_mhi_unregister;

	ret = ath11k_ce_alloc_pipes(ab);
	if (ret) {
		ath11k_err(ab, "failed to allocate ce pipes: %d\n", ret);
		goto err_hal_srng_deinit;
	}

	ath11k_pci_init_qmi_ce_config(ab);

	ret = ath11k_pci_config_irq(ab);
	if (ret) {
		ath11k_err(ab, "failed to config irq: %d\n", ret);
		goto err_ce_free;
	}

	ret = ath11k_core_init(ab);
	if (ret) {
		ath11k_err(ab, "failed to init core: %d\n", ret);
		goto err_free_irq;
	}
	return 0;

err_free_irq:
	ath11k_pci_free_irq(ab);

err_ce_free:
	ath11k_ce_free_pipes(ab);

err_hal_srng_deinit:
	ath11k_hal_srng_deinit(ab);

err_mhi_unregister:
	ath11k_mhi_unregister(ab_pci);

err_pci_disable_msi:
	ath11k_pci_disable_msi(ab_pci);

err_pci_free_region:
	ath11k_pci_free_region(ab_pci);

err_free_core:
	ath11k_core_free(ab);

	return ret;
}

static void ath11k_pci_remove(struct pci_dev *pdev)
{
	struct ath11k_base *ab = pci_get_drvdata(pdev);
	struct ath11k_pci *ab_pci = ath11k_pci_priv(ab);

	set_bit(ATH11K_FLAG_UNREGISTERING, &ab->dev_flags);
	ath11k_mhi_unregister(ab_pci);
	ath11k_pci_disable_msi(ab_pci);
	ath11k_pci_free_region(ab_pci);
	ath11k_pci_free_irq(ab);
	ath11k_core_free(ab);
}

static void ath11k_pci_shutdown(struct pci_dev *pdev)
{
	struct ath11k_base *ab = pci_get_drvdata(pdev);

	ath11k_pci_power_down(ab);
}

static struct pci_driver ath11k_pci_driver = {
	.name = "ath11k_pci",
	.id_table = ath11k_pci_id_table,
	.probe = ath11k_pci_probe,
	.remove = ath11k_pci_remove,
	.shutdown = ath11k_pci_shutdown,
};

static int ath11k_pci_init(void)
{
	int ret;

	ret = pci_register_driver(&ath11k_pci_driver);
	if (ret)
		pr_err("failed to register ath11k pci driver: %d\n",
		       ret);

	return ret;
}
module_init(ath11k_pci_init);

static void ath11k_pci_exit(void)
{
	pci_unregister_driver(&ath11k_pci_driver);
}

module_exit(ath11k_pci_exit);

MODULE_DESCRIPTION("Driver support for Qualcomm Technologies 802.11ax WLAN PCIe devices");
MODULE_LICENSE("Dual BSD/GPL");
