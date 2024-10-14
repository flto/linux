// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/firmware.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/iommu.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/soc/qcom/mdt_loader.h>

#include "iris_core.h"
#include "iris_firmware.h"

#define MAX_FIRMWARE_NAME_SIZE	128

#define USE_TZ(core) (IS_ENABLED(QCOM_SCM) && core->fw_dev)

#define IRIS_FW_START_ADDR 0

#define WRAPPER_TZ_XTSS_SW_RESET 	0xc1000
#define WRAPPER_CPA_START_ADDR		0xc1020
#define WRAPPER_CPA_END_ADDR		0xc1024
#define WRAPPER_FW_START_ADDR		0xc1028
#define WRAPPER_FW_END_ADDR		0xc102c
#define WRAPPER_NONPIX_START_ADDR	0xc1030
#define WRAPPER_NONPIX_END_ADDR		0xc1034

// XXX review this function
static int iris_boot_no_tz(struct iris_core *core)
{
	u32 fw_size = core->fw_mem_size;
	void __iomem *wrapper_base = core->reg_base;

	writel(0, wrapper_base + WRAPPER_FW_START_ADDR);
	writel(fw_size, wrapper_base + WRAPPER_FW_END_ADDR);
	writel(0, wrapper_base + WRAPPER_CPA_START_ADDR);
	writel(fw_size, wrapper_base + WRAPPER_CPA_END_ADDR);
	writel(fw_size, wrapper_base + WRAPPER_NONPIX_START_ADDR);
	writel(fw_size, wrapper_base + WRAPPER_NONPIX_END_ADDR);

	/* Bring XTSS out of reset */
	writel(0, wrapper_base + WRAPPER_TZ_XTSS_SW_RESET);

	return 0;
}

static int iris_load_fw_to_memory(struct iris_core *core, const char *fw_name)
{
	u32 pas_id = core->iris_platform_data->pas_id;
	const struct firmware *firmware = NULL;
	struct device *dev = core->dev;
	ssize_t fw_size;
	void *mem_virt;
	int ret;

	if (strlen(fw_name) >= MAX_FIRMWARE_NAME_SIZE - 4)
		return -EINVAL;

	ret = request_firmware(&firmware, fw_name, dev);
	if (ret)
		return ret;

	fw_size = qcom_mdt_get_size(firmware);
	if (fw_size < 0 || core->fw_mem_size < (size_t)fw_size) {
		ret = -EINVAL;
		goto err_release_fw;
	}

	mem_virt = memremap(core->fw_mem_phys, core->fw_mem_size, MEMREMAP_WC);
	if (!mem_virt)
		goto err_release_fw;

	if (USE_TZ(core))
		ret = qcom_mdt_load(dev, firmware, fw_name,
			pas_id, mem_virt, core->fw_mem_phys, core->fw_mem_size, NULL);
	else
		ret = qcom_mdt_load_no_init(dev, firmware, fw_name,
			pas_id, mem_virt, core->fw_mem_phys, core->fw_mem_size, NULL);
	if (ret)
		goto err_mem_unmap;

	if (USE_TZ(core))
		ret = qcom_scm_pas_auth_and_reset(pas_id);
	else
		ret = iris_boot_no_tz(core);

	if (ret)
		goto err_mem_unmap;

	return ret;

err_mem_unmap:
	memunmap(mem_virt);
err_release_fw:
	release_firmware(firmware);
	return ret;
}

int iris_fw_load(struct iris_core *core)
{
	struct tz_cp_config *cp_config = core->iris_platform_data->tz_cp_config_data;
	int ret;

	ret = iris_load_fw_to_memory(core, core->iris_platform_data->fwname);
	if (ret) {
		dev_err(core->dev, "firmware download failed\n");
		return -ENOMEM;
	}

	if (!USE_TZ(core))
		return 0;

	ret = qcom_scm_mem_protect_video_var(cp_config->cp_start,
					     cp_config->cp_size,
					     cp_config->cp_nonpixel_start,
					     cp_config->cp_nonpixel_size);
	if (ret) {
		dev_err(core->dev, "protect memory failed\n");
		qcom_scm_pas_shutdown(core->iris_platform_data->pas_id);
		return ret;
	}

	return ret;
}

int iris_fw_unload(struct iris_core *core)
{
	if (USE_TZ(core))
		return qcom_scm_pas_shutdown(core->iris_platform_data->pas_id);
	return 0;
}

int iris_set_hw_state(struct iris_core *core, bool resume)
{
	if (USE_TZ(core))
		return qcom_scm_set_remote_state(resume, 0);

	if (resume) {
		if (!readl(core->reg_base + WRAPPER_TZ_XTSS_SW_RESET)) {
			dev_err(core->dev, "resume failed: XTSS wasn't reset\n");
			return -EINVAL;
		}

		return iris_boot_no_tz(core);
	}
	return 0;
}


int iris_firmware_init(struct iris_core *core)
{
	struct platform_device_info info;
	struct iommu_domain *iommu_dom;
	struct platform_device *pdev;
	struct device_node *np;
	struct reserved_mem *rmem;
	int ret;

	np = of_parse_phandle(core->dev->of_node, "memory-region", 0);
	if (!np)
		return -EINVAL;

	rmem = of_reserved_mem_lookup(np);
	of_node_put(np);
	if (!rmem)
		return -EINVAL;

	core->fw_mem_phys = rmem->base;
	core->fw_mem_size = rmem->size;

	np = of_get_child_by_name(core->dev->of_node, "video-firmware");
	if (!np) {
		if (!IS_ENABLED(QCOM_SCM))
			return -EINVAL;
		return 0;
	}

	memset(&info, 0, sizeof(info));
	info.fwnode = &np->fwnode;
	info.parent = core->dev;
	info.name = np->name;
	info.dma_mask = DMA_BIT_MASK(32);

	pdev = platform_device_register_full(&info);
	if (IS_ERR(pdev)) {
		of_node_put(np);
		return PTR_ERR(pdev);
	}

	pdev->dev.of_node = np;

	ret = of_dma_configure(&pdev->dev, np, true);
	if (ret) {
		dev_err(core->dev, "dma configure fail\n");
		goto err_unregister;
	}

	core->fw_dev = &pdev->dev;

	iommu_dom = iommu_paging_domain_alloc(core->fw_dev);
	if (IS_ERR(iommu_dom)) {
		dev_err(core->fw_dev, "Failed to allocate iommu domain\n");
		ret = PTR_ERR(iommu_dom);
		goto err_unregister;
	}

	ret = iommu_attach_device(iommu_dom, core->fw_dev);
	if (ret) {
		dev_err(core->fw_dev, "could not attach device\n");
		goto err_iommu_free;
	}

	ret = iommu_map(iommu_dom, IRIS_FW_START_ADDR, core->fw_mem_phys, core->fw_mem_size,
			IOMMU_READ | IOMMU_WRITE | IOMMU_PRIV, GFP_KERNEL);
	if (ret) {
		dev_err(core->fw_dev, "could not map video firmware region\n");
		goto err_iommu_detach;
	}

	core->fw_iommu = iommu_dom;

	of_node_put(np);

	return 0;

err_iommu_detach:
	iommu_detach_device(iommu_dom, core->fw_dev);
err_iommu_free:
	iommu_domain_free(iommu_dom);
err_unregister:
	platform_device_unregister(pdev);
	of_node_put(np);
	return ret;
}

void iris_firmware_deinit(struct iris_core *core)
{
	struct iommu_domain *iommu;
	size_t unmapped;

	if (!core->fw_dev)
		return;

	iommu = core->fw_iommu;

	unmapped = iommu_unmap(iommu, IRIS_FW_START_ADDR, core->fw_mem_size);
	if (unmapped != core->fw_mem_size)
		dev_err(core->fw_dev, "failed to unmap firmware\n");

	iommu_detach_device(iommu, core->fw_dev);

	iommu_domain_free(iommu);
	core->fw_iommu = NULL;

	platform_device_unregister(to_platform_device(core->fw_dev));
	core->fw_dev = NULL;
}
