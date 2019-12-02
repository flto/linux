// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 */

#include <asm/div64.h>
#include <dt-bindings/interconnect/qcom,sm8150.h>
#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sort.h>

#include "icc-rpmh.h"
#include "bcm-voter.h"

DEFINE_QNODE(qhm_a1noc_cfg, MASTER_A1NOC_CFG, 1, 4, 1, SLAVE_SERVICE_A1NOC);
DEFINE_QNODE(qhm_qup0, MASTER_QUP_0, 1, 4, 1, A1NOC_SNOC_SLV);
DEFINE_QNODE(xm_emac, MASTER_EMAC, 1, 8, 1, A1NOC_SNOC_SLV);
DEFINE_QNODE(xm_ufs_mem, MASTER_UFS_MEM, 1, 8, 1, A1NOC_SNOC_SLV);
DEFINE_QNODE(xm_usb3_0, MASTER_USB3, 1, 8, 1, A1NOC_SNOC_SLV);
DEFINE_QNODE(xm_usb3_1, MASTER_USB3_1, 1, 8, 1, A1NOC_SNOC_SLV);
DEFINE_QNODE(qhm_a2noc_cfg, MASTER_A2NOC_CFG, 1, 4, 1, SLAVE_SERVICE_A2NOC);
DEFINE_QNODE(qhm_qdss_bam, MASTER_QDSS_BAM, 1, 4, 1, A2NOC_SNOC_SLV);
DEFINE_QNODE(qhm_qspi, MASTER_QSPI, 1, 4, 1, A2NOC_SNOC_SLV);
DEFINE_QNODE(qhm_qup1, MASTER_QUP_1, 1, 4, 1, A2NOC_SNOC_SLV);
DEFINE_QNODE(qhm_qup2, MASTER_QUP_2, 1, 4, 1, A2NOC_SNOC_SLV);
DEFINE_QNODE(qhm_sensorss_ahb, MASTER_SENSORS_AHB, 1, 4, 1, A2NOC_SNOC_SLV);
DEFINE_QNODE(qhm_tsif, MASTER_TSIF, 1, 4, 1, A2NOC_SNOC_SLV);
DEFINE_QNODE(qnm_cnoc, MASTER_CNOC_A2NOC, 1, 8, 1, A2NOC_SNOC_SLV);
DEFINE_QNODE(qxm_crypto, MASTER_CRYPTO_CORE_0, 1, 8, 1, A2NOC_SNOC_SLV);
DEFINE_QNODE(qxm_ipa, MASTER_IPA, 1, 8, 1, A2NOC_SNOC_SLV);
DEFINE_QNODE(xm_pcie3_0, MASTER_PCIE, 1, 8, 1, SLAVE_ANOC_PCIE_GEM_NOC);
DEFINE_QNODE(xm_pcie3_1, MASTER_PCIE_1, 1, 8, 1, SLAVE_ANOC_PCIE_GEM_NOC);
DEFINE_QNODE(xm_qdss_etr, MASTER_QDSS_ETR, 1, 8, 1, A2NOC_SNOC_SLV);
DEFINE_QNODE(xm_sdc2, MASTER_SDCC_2, 1, 8, 1, A2NOC_SNOC_SLV);
DEFINE_QNODE(xm_sdc4, MASTER_SDCC_4, 1, 8, 1, A2NOC_SNOC_SLV);
DEFINE_QNODE(qxm_camnoc_hf0_uncomp, MASTER_CAMNOC_HF0_UNCOMP, 1, 32, 1, SLAVE_CAMNOC_UNCOMP);
DEFINE_QNODE(qxm_camnoc_hf1_uncomp, MASTER_CAMNOC_HF1_UNCOMP, 1, 32, 1, SLAVE_CAMNOC_UNCOMP);
DEFINE_QNODE(qxm_camnoc_sf_uncomp, MASTER_CAMNOC_SF_UNCOMP, 1, 32, 1, SLAVE_CAMNOC_UNCOMP);
DEFINE_QNODE(qnm_npu, MASTER_NPU, 1, 32, 1, SLAVE_CDSP_MEM_NOC);
DEFINE_QNODE(qhm_spdm, MASTER_SPDM, 1, 4, 1, SLAVE_CNOC_A2NOC);
DEFINE_QNODE(qnm_snoc, SNOC_CNOC_MAS, 1, 8, 50, SLAVE_TLMM_SOUTH, SLAVE_CDSP_CFG, SLAVE_SPSS_CFG, SLAVE_CAMERA_CFG, SLAVE_SDCC_4, SLAVE_SDCC_2, SLAVE_CNOC_MNOC_CFG, SLAVE_EMAC_CFG, SLAVE_UFS_MEM_CFG, SLAVE_TLMM_EAST, SLAVE_SSC_CFG, SLAVE_SNOC_CFG, SLAVE_NORTH_PHY_CFG, SLAVE_QUP_0, SLAVE_GLM, SLAVE_PCIE_1_CFG, SLAVE_A2NOC_CFG, SLAVE_QDSS_CFG, SLAVE_DISPLAY_CFG, SLAVE_TCSR, SLAVE_CNOC_DDRSS, SLAVE_RBCPR_MMCX_CFG, SLAVE_NPU_CFG, SLAVE_PCIE_0_CFG, SLAVE_GRAPHICS_3D_CFG, SLAVE_VENUS_CFG, SLAVE_TSIF, SLAVE_IPA_CFG, SLAVE_CLK_CTL, SLAVE_AOP, SLAVE_QUP_1, SLAVE_AHB2PHY_SOUTH, SLAVE_USB3_1, SLAVE_SERVICE_CNOC, SLAVE_UFS_CARD_CFG, SLAVE_QUP_2, SLAVE_RBCPR_CX_CFG, SLAVE_TLMM_WEST, SLAVE_A1NOC_CFG, SLAVE_AOSS, SLAVE_PRNG, SLAVE_VSENSE_CTRL_CFG, SLAVE_QSPI, SLAVE_USB3, SLAVE_SPDM_WRAPPER, SLAVE_CRYPTO_0_CFG, SLAVE_PIMEM_CFG, SLAVE_TLMM_NORTH, SLAVE_RBCPR_MX_CFG, SLAVE_IMEM_CFG);
DEFINE_QNODE(xm_qdss_dap, MASTER_QDSS_DAP, 1, 8, 51, SLAVE_TLMM_SOUTH, SLAVE_CDSP_CFG, SLAVE_SPSS_CFG, SLAVE_CAMERA_CFG, SLAVE_SDCC_4, SLAVE_SDCC_2, SLAVE_CNOC_MNOC_CFG, SLAVE_EMAC_CFG, SLAVE_UFS_MEM_CFG, SLAVE_TLMM_EAST, SLAVE_SSC_CFG, SLAVE_SNOC_CFG, SLAVE_NORTH_PHY_CFG, SLAVE_QUP_0, SLAVE_GLM, SLAVE_PCIE_1_CFG, SLAVE_A2NOC_CFG, SLAVE_QDSS_CFG, SLAVE_DISPLAY_CFG, SLAVE_TCSR, SLAVE_CNOC_DDRSS, SLAVE_CNOC_A2NOC, SLAVE_RBCPR_MMCX_CFG, SLAVE_NPU_CFG, SLAVE_PCIE_0_CFG, SLAVE_GRAPHICS_3D_CFG, SLAVE_VENUS_CFG, SLAVE_TSIF, SLAVE_IPA_CFG, SLAVE_CLK_CTL, SLAVE_AOP, SLAVE_QUP_1, SLAVE_AHB2PHY_SOUTH, SLAVE_USB3_1, SLAVE_SERVICE_CNOC, SLAVE_UFS_CARD_CFG, SLAVE_QUP_2, SLAVE_RBCPR_CX_CFG, SLAVE_TLMM_WEST, SLAVE_A1NOC_CFG, SLAVE_AOSS, SLAVE_PRNG, SLAVE_VSENSE_CTRL_CFG, SLAVE_QSPI, SLAVE_USB3, SLAVE_SPDM_WRAPPER, SLAVE_CRYPTO_0_CFG, SLAVE_PIMEM_CFG, SLAVE_TLMM_NORTH, SLAVE_RBCPR_MX_CFG, SLAVE_IMEM_CFG);
DEFINE_QNODE(qhm_cnoc_dc_noc, MASTER_CNOC_DC_NOC, 1, 4, 2, SLAVE_GEM_NOC_CFG, SLAVE_LLCC_CFG);
DEFINE_QNODE(acm_apps, MASTER_AMPSS_M0, 2, 32, 3, SLAVE_ECC, SLAVE_LLCC, SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(acm_gpu_tcu, MASTER_GPU_TCU, 1, 8, 2, SLAVE_LLCC, SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(acm_sys_tcu, MASTER_SYS_TCU, 1, 8, 2, SLAVE_LLCC, SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(qhm_gemnoc_cfg, MASTER_GEM_NOC_CFG, 1, 4, 2, SLAVE_SERVICE_GEM_NOC, SLAVE_MSS_PROC_MS_MPU_CFG);
DEFINE_QNODE(qnm_cmpnoc, MASTER_COMPUTE_NOC, 2, 32, 3, SLAVE_ECC, SLAVE_LLCC, SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(qnm_gpu, MASTER_GRAPHICS_3D, 2, 32, 2, SLAVE_LLCC, SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(qnm_mnoc_hf, MASTER_MNOC_HF_MEM_NOC, 2, 32, 1, SLAVE_LLCC);
DEFINE_QNODE(qnm_mnoc_sf, MASTER_MNOC_SF_MEM_NOC, 1, 32, 2, SLAVE_LLCC, SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(qnm_pcie, MASTER_GEM_NOC_PCIE_SNOC, 1, 16, 2, SLAVE_LLCC, SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(qnm_snoc_gc, MASTER_SNOC_GC_MEM_NOC, 1, 8, 1, SLAVE_LLCC);
DEFINE_QNODE(qnm_snoc_sf, MASTER_SNOC_SF_MEM_NOC, 1, 16, 1, SLAVE_LLCC);
DEFINE_QNODE(qxm_ecc, MASTER_ECC, 2, 32, 1, SLAVE_LLCC);
DEFINE_QNODE(ipa_core_master, MASTER_IPA_CORE, 1, 8, 1, SLAVE_IPA_CORE);
DEFINE_QNODE(llcc_mc, MASTER_LLCC, 4, 4, 1, SLAVE_EBI_CH0);
DEFINE_QNODE(qhm_mnoc_cfg, MASTER_CNOC_MNOC_CFG, 1, 4, 1, SLAVE_SERVICE_MNOC);
DEFINE_QNODE(qxm_camnoc_hf0, MASTER_CAMNOC_HF0, 1, 32, 1, SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qxm_camnoc_hf1, MASTER_CAMNOC_HF1, 1, 32, 1, SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qxm_camnoc_sf, MASTER_CAMNOC_SF, 1, 32, 1, SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qxm_mdp0, MASTER_MDP_PORT0, 1, 32, 1, SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qxm_mdp1, MASTER_MDP_PORT1, 1, 32, 1, SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qxm_rot, MASTER_ROTATOR, 1, 32, 1, SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qxm_venus0, MASTER_VIDEO_P0, 1, 32, 1, SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qxm_venus1, MASTER_VIDEO_P1, 1, 32, 1, SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qxm_venus_arm9, MASTER_VIDEO_PROC, 1, 8, 1, SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qhm_snoc_cfg, MASTER_SNOC_CFG, 1, 4, 1, SLAVE_SERVICE_SNOC);
DEFINE_QNODE(qnm_aggre1_noc, A1NOC_SNOC_MAS, 1, 16, 6, SLAVE_SNOC_GEM_NOC_SF, SLAVE_PIMEM, SLAVE_OCIMEM, SLAVE_APPSS, SNOC_CNOC_SLV, SLAVE_QDSS_STM);
DEFINE_QNODE(qnm_aggre2_noc, A2NOC_SNOC_MAS, 1, 16, 9, SLAVE_SNOC_GEM_NOC_SF, SLAVE_PIMEM, SLAVE_OCIMEM, SLAVE_APPSS, SNOC_CNOC_SLV, SLAVE_PCIE_0, SLAVE_PCIE_1, SLAVE_TCU, SLAVE_QDSS_STM);
DEFINE_QNODE(qnm_gemnoc, MASTER_GEM_NOC_SNOC, 1, 8, 6, SLAVE_PIMEM, SLAVE_OCIMEM, SLAVE_APPSS, SNOC_CNOC_SLV, SLAVE_TCU, SLAVE_QDSS_STM);
DEFINE_QNODE(qxm_pimem, MASTER_PIMEM, 1, 8, 2, SLAVE_SNOC_GEM_NOC_GC, SLAVE_OCIMEM);
DEFINE_QNODE(xm_gic, MASTER_GIC, 1, 8, 2, SLAVE_SNOC_GEM_NOC_GC, SLAVE_OCIMEM);
DEFINE_QNODE(alc, MASTER_ALC, 1, 1, 0);
DEFINE_QNODE(qnm_mnoc_hf_display, MASTER_MNOC_HF_MEM_NOC_DISPLAY, 2, 32, 1, SLAVE_LLCC_DISPLAY);
DEFINE_QNODE(qnm_mnoc_sf_display, MASTER_MNOC_SF_MEM_NOC_DISPLAY, 1, 32, 1, SLAVE_LLCC_DISPLAY);
DEFINE_QNODE(llcc_mc_display, MASTER_LLCC_DISPLAY, 4, 4, 1, SLAVE_EBI_CH0_DISPLAY);
DEFINE_QNODE(qxm_mdp0_display, MASTER_MDP_PORT0_DISPLAY, 1, 32, 1, SLAVE_MNOC_HF_MEM_NOC_DISPLAY);
DEFINE_QNODE(qxm_mdp1_display, MASTER_MDP_PORT1_DISPLAY, 1, 32, 1, SLAVE_MNOC_HF_MEM_NOC_DISPLAY);
DEFINE_QNODE(qxm_rot_display, MASTER_ROTATOR_DISPLAY, 1, 32, 1, SLAVE_MNOC_SF_MEM_NOC_DISPLAY);
DEFINE_QNODE(qns_a1noc_snoc, A1NOC_SNOC_SLV, 1, 16, 1, A1NOC_SNOC_MAS);
DEFINE_QNODE(srvc_aggre1_noc, SLAVE_SERVICE_A1NOC, 1, 4, 0);
DEFINE_QNODE(qns_a2noc_snoc, A2NOC_SNOC_SLV, 1, 16, 1, A2NOC_SNOC_MAS);
DEFINE_QNODE(qns_pcie_mem_noc, SLAVE_ANOC_PCIE_GEM_NOC, 1, 16, 1, MASTER_GEM_NOC_PCIE_SNOC);
DEFINE_QNODE(srvc_aggre2_noc, SLAVE_SERVICE_A2NOC, 1, 4, 0);
DEFINE_QNODE(qns_camnoc_uncomp, SLAVE_CAMNOC_UNCOMP, 1, 32, 0);
DEFINE_QNODE(qns_cdsp_mem_noc, SLAVE_CDSP_MEM_NOC, 2, 32, 1, MASTER_COMPUTE_NOC);
DEFINE_QNODE(qhs_a1_noc_cfg, SLAVE_A1NOC_CFG, 1, 4, 1, MASTER_A1NOC_CFG);
DEFINE_QNODE(qhs_a2_noc_cfg, SLAVE_A2NOC_CFG, 1, 4, 1, MASTER_A2NOC_CFG);
DEFINE_QNODE(qhs_ahb2phy_south, SLAVE_AHB2PHY_SOUTH, 1, 4, 0);
DEFINE_QNODE(qhs_aop, SLAVE_AOP, 1, 4, 0);
DEFINE_QNODE(qhs_aoss, SLAVE_AOSS, 1, 4, 0);
DEFINE_QNODE(qhs_camera_cfg, SLAVE_CAMERA_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_clk_ctl, SLAVE_CLK_CTL, 1, 4, 0);
DEFINE_QNODE(qhs_compute_dsp, SLAVE_CDSP_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_cpr_cx, SLAVE_RBCPR_CX_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_cpr_mmcx, SLAVE_RBCPR_MMCX_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_cpr_mx, SLAVE_RBCPR_MX_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_crypto0_cfg, SLAVE_CRYPTO_0_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_ddrss_cfg, SLAVE_CNOC_DDRSS, 1, 4, 1, MASTER_CNOC_DC_NOC);
DEFINE_QNODE(qhs_display_cfg, SLAVE_DISPLAY_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_emac_cfg, SLAVE_EMAC_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_glm, SLAVE_GLM, 1, 4, 0);
DEFINE_QNODE(qhs_gpuss_cfg, SLAVE_GRAPHICS_3D_CFG, 1, 8, 0);
DEFINE_QNODE(qhs_imem_cfg, SLAVE_IMEM_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_ipa, SLAVE_IPA_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_mnoc_cfg, SLAVE_CNOC_MNOC_CFG, 1, 4, 1, MASTER_CNOC_MNOC_CFG);
DEFINE_QNODE(qhs_npu_cfg, SLAVE_NPU_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_pcie0_cfg, SLAVE_PCIE_0_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_pcie1_cfg, SLAVE_PCIE_1_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_phy_refgen_north, SLAVE_NORTH_PHY_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_pimem_cfg, SLAVE_PIMEM_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_prng, SLAVE_PRNG, 1, 4, 0);
DEFINE_QNODE(qhs_qdss_cfg, SLAVE_QDSS_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_qspi, SLAVE_QSPI, 1, 4, 0);
DEFINE_QNODE(qhs_qupv3_east, SLAVE_QUP_2, 1, 4, 0);
DEFINE_QNODE(qhs_qupv3_north, SLAVE_QUP_1, 1, 4, 0);
DEFINE_QNODE(qhs_qupv3_south, SLAVE_QUP_0, 1, 4, 0);
DEFINE_QNODE(qhs_sdc2, SLAVE_SDCC_2, 1, 4, 0);
DEFINE_QNODE(qhs_sdc4, SLAVE_SDCC_4, 1, 4, 0);
DEFINE_QNODE(qhs_snoc_cfg, SLAVE_SNOC_CFG, 1, 4, 1, MASTER_SNOC_CFG);
DEFINE_QNODE(qhs_spdm, SLAVE_SPDM_WRAPPER, 1, 4, 0);
DEFINE_QNODE(qhs_spss_cfg, SLAVE_SPSS_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_ssc_cfg, SLAVE_SSC_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_tcsr, SLAVE_TCSR, 1, 4, 0);
DEFINE_QNODE(qhs_tlmm_east, SLAVE_TLMM_EAST, 1, 4, 0);
DEFINE_QNODE(qhs_tlmm_north, SLAVE_TLMM_NORTH, 1, 4, 0);
DEFINE_QNODE(qhs_tlmm_south, SLAVE_TLMM_SOUTH, 1, 4, 0);
DEFINE_QNODE(qhs_tlmm_west, SLAVE_TLMM_WEST, 1, 4, 0);
DEFINE_QNODE(qhs_tsif, SLAVE_TSIF, 1, 4, 0);
DEFINE_QNODE(qhs_ufs_card_cfg, SLAVE_UFS_CARD_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_ufs_mem_cfg, SLAVE_UFS_MEM_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_usb3_0, SLAVE_USB3, 1, 4, 0);
DEFINE_QNODE(qhs_usb3_1, SLAVE_USB3_1, 1, 4, 0);
DEFINE_QNODE(qhs_venus_cfg, SLAVE_VENUS_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_vsense_ctrl_cfg, SLAVE_VSENSE_CTRL_CFG, 1, 4, 0);
DEFINE_QNODE(qns_cnoc_a2noc, SLAVE_CNOC_A2NOC, 1, 8, 1, MASTER_CNOC_A2NOC);
DEFINE_QNODE(srvc_cnoc, SLAVE_SERVICE_CNOC, 1, 4, 0);
DEFINE_QNODE(qhs_llcc, SLAVE_LLCC_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_memnoc, SLAVE_GEM_NOC_CFG, 1, 4, 1, MASTER_GEM_NOC_CFG);
DEFINE_QNODE(qhs_mdsp_ms_mpu_cfg, SLAVE_MSS_PROC_MS_MPU_CFG, 1, 4, 0);
DEFINE_QNODE(qns_ecc, SLAVE_ECC, 1, 32, 0);
DEFINE_QNODE(qns_gem_noc_snoc, SLAVE_GEM_NOC_SNOC, 1, 8, 1, MASTER_GEM_NOC_SNOC);
DEFINE_QNODE(qns_llcc, SLAVE_LLCC, 4, 16, 1, MASTER_LLCC);
DEFINE_QNODE(srvc_gemnoc, SLAVE_SERVICE_GEM_NOC, 1, 4, 0);
DEFINE_QNODE(ipa_core_slave, SLAVE_IPA_CORE, 1, 8, 0);
DEFINE_QNODE(ebi, SLAVE_EBI_CH0, 4, 4, 0);
DEFINE_QNODE(qns2_mem_noc, SLAVE_MNOC_SF_MEM_NOC, 1, 32, 1, MASTER_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qns_mem_noc_hf, SLAVE_MNOC_HF_MEM_NOC, 2, 32, 1, MASTER_MNOC_HF_MEM_NOC);
DEFINE_QNODE(srvc_mnoc, SLAVE_SERVICE_MNOC, 1, 4, 0);
DEFINE_QNODE(qhs_apss, SLAVE_APPSS, 1, 8, 0);
DEFINE_QNODE(qns_cnoc, SNOC_CNOC_SLV, 1, 8, 1, SNOC_CNOC_MAS);
DEFINE_QNODE(qns_gemnoc_gc, SLAVE_SNOC_GEM_NOC_GC, 1, 8, 1, MASTER_SNOC_GC_MEM_NOC);
DEFINE_QNODE(qns_gemnoc_sf, SLAVE_SNOC_GEM_NOC_SF, 1, 16, 1, MASTER_SNOC_SF_MEM_NOC);
DEFINE_QNODE(qxs_imem, SLAVE_OCIMEM, 1, 8, 0);
DEFINE_QNODE(qxs_pimem, SLAVE_PIMEM, 1, 8, 0);
DEFINE_QNODE(srvc_snoc, SLAVE_SERVICE_SNOC, 1, 4, 0);
DEFINE_QNODE(xs_pcie_0, SLAVE_PCIE_0, 1, 8, 0);
DEFINE_QNODE(xs_pcie_1, SLAVE_PCIE_1, 1, 8, 0);
DEFINE_QNODE(xs_qdss_stm, SLAVE_QDSS_STM, 1, 4, 0);
DEFINE_QNODE(xs_sys_tcu_cfg, SLAVE_TCU, 1, 8, 0);
DEFINE_QNODE(qns_llcc_display, SLAVE_LLCC_DISPLAY, 4, 16, 1, MASTER_LLCC_DISPLAY);
DEFINE_QNODE(ebi_display, SLAVE_EBI_CH0_DISPLAY, 4, 4, 0);
DEFINE_QNODE(qns2_mem_noc_display, SLAVE_MNOC_SF_MEM_NOC_DISPLAY, 1, 32, 1, MASTER_MNOC_SF_MEM_NOC_DISPLAY);
DEFINE_QNODE(qns_mem_noc_hf_display, SLAVE_MNOC_HF_MEM_NOC_DISPLAY, 2, 32, 1, MASTER_MNOC_HF_MEM_NOC_DISPLAY);
DEFINE_QBCM(bcm_acv, "ACV", false, 1, &ebi);
DEFINE_QBCM(bcm_alc, "ALC", false, 1, &alc);
DEFINE_QBCM(bcm_mc0, "MC0", false, 1, &ebi);
DEFINE_QBCM(bcm_sh0, "SH0", false, 1, &qns_llcc);
DEFINE_QBCM(bcm_mm0, "MM0", false, 1, &qns_mem_noc_hf);
DEFINE_QBCM(bcm_mm1, "MM1", false, 7, &qxm_camnoc_hf0_uncomp, &qxm_camnoc_hf1_uncomp, &qxm_camnoc_sf_uncomp, &qxm_camnoc_hf0, &qxm_camnoc_hf1, &qxm_mdp0, &qxm_mdp1);
DEFINE_QBCM(bcm_sh2, "SH2", false, 1, &qns_gem_noc_snoc);
DEFINE_QBCM(bcm_mm2, "MM2", false, 2, &qxm_camnoc_sf, &qns2_mem_noc);
DEFINE_QBCM(bcm_sh3, "SH3", false, 2, &acm_gpu_tcu, &acm_sys_tcu);
DEFINE_QBCM(bcm_mm3, "MM3", false, 4, &qxm_rot, &qxm_venus0, &qxm_venus1, &qxm_venus_arm9);
DEFINE_QBCM(bcm_sh4, "SH4", false, 1, &qnm_cmpnoc);
DEFINE_QBCM(bcm_sh5, "SH5", false, 1, &acm_apps);
DEFINE_QBCM(bcm_sn0, "SN0", false, 1, &qns_gemnoc_sf);
DEFINE_QBCM(bcm_co0, "CO0", false, 1, &qns_cdsp_mem_noc);
DEFINE_QBCM(bcm_ce0, "CE0", false, 1, &qxm_crypto);
DEFINE_QBCM(bcm_sn1, "SN1", false, 1, &qxs_imem);
DEFINE_QBCM(bcm_co1, "CO1", false, 1, &qnm_npu);
DEFINE_QBCM(bcm_ip0, "IP0", false, 1, &ipa_core_slave);
DEFINE_QBCM(bcm_cn0, "CN0", false, 53, &qhm_spdm, &qnm_snoc, &qhs_a1_noc_cfg, &qhs_a2_noc_cfg, &qhs_ahb2phy_south, &qhs_aop, &qhs_aoss, &qhs_camera_cfg, &qhs_clk_ctl, &qhs_compute_dsp, &qhs_cpr_cx, &qhs_cpr_mmcx, &qhs_cpr_mx, &qhs_crypto0_cfg, &qhs_ddrss_cfg, &qhs_display_cfg, &qhs_emac_cfg, &qhs_glm, &qhs_gpuss_cfg, &qhs_imem_cfg, &qhs_ipa, &qhs_mnoc_cfg, &qhs_npu_cfg, &qhs_pcie0_cfg, &qhs_pcie1_cfg, &qhs_phy_refgen_north, &qhs_pimem_cfg, &qhs_prng, &qhs_qdss_cfg, &qhs_qspi, &qhs_qupv3_east, &qhs_qupv3_north, &qhs_qupv3_south, &qhs_sdc2, &qhs_sdc4, &qhs_snoc_cfg, &qhs_spdm, &qhs_spss_cfg, &qhs_ssc_cfg, &qhs_tcsr, &qhs_tlmm_east, &qhs_tlmm_north, &qhs_tlmm_south, &qhs_tlmm_west, &qhs_tsif, &qhs_ufs_card_cfg, &qhs_ufs_mem_cfg, &qhs_usb3_0, &qhs_usb3_1, &qhs_venus_cfg, &qhs_vsense_ctrl_cfg, &qns_cnoc_a2noc, &srvc_cnoc);
DEFINE_QBCM(bcm_qup0, "QUP0", false, 3, &qhm_qup0, &qhm_qup1, &qhm_qup2);
DEFINE_QBCM(bcm_sn2, "SN2", false, 1, &qns_gemnoc_gc);
DEFINE_QBCM(bcm_sn3, "SN3", false, 3, &srvc_aggre1_noc, &srvc_aggre2_noc, &qns_cnoc);
DEFINE_QBCM(bcm_sn4, "SN4", false, 1, &qxs_pimem);
DEFINE_QBCM(bcm_sn5, "SN5", false, 1, &xs_qdss_stm);
DEFINE_QBCM(bcm_sn8, "SN8", false, 2, &xs_pcie_0, &xs_pcie_1);
DEFINE_QBCM(bcm_sn9, "SN9", false, 1, &qnm_aggre1_noc);
DEFINE_QBCM(bcm_sn11, "SN11", false, 1, &qnm_aggre2_noc);
DEFINE_QBCM(bcm_sn12, "SN12", false, 2, &qxm_pimem, &xm_gic);
DEFINE_QBCM(bcm_sn14, "SN14", false, 1, &qns_pcie_mem_noc);
DEFINE_QBCM(bcm_sn15, "SN15", false, 1, &qnm_gemnoc);
DEFINE_QBCM(bcm_acv_display, "ACV", false, 1, &ebi_display);
DEFINE_QBCM(bcm_alc_display, "ALC", false, 0);
DEFINE_QBCM(bcm_mc0_display, "MC0", false, 1, &ebi_display);
DEFINE_QBCM(bcm_sh0_display, "SH0", false, 1, &qns_llcc_display);
DEFINE_QBCM(bcm_mm0_display, "MM0", false, 1, &qns_mem_noc_hf_display);
DEFINE_QBCM(bcm_mm1_display, "MM1", false, 2, &qxm_mdp0_display, &qxm_mdp1_display);
DEFINE_QBCM(bcm_mm2_display, "MM2", false, 1, &qns2_mem_noc_display);
DEFINE_QBCM(bcm_mm3_display, "MM3", false, 1, &qxm_rot_display);

static struct qcom_icc_bcm *aggre1_noc_bcms[] = {
	&bcm_qup0,
	&bcm_sn3,
};
static struct qcom_icc_node *aggre1_noc_nodes[] = {
	[MASTER_A1NOC_CFG] = &qhm_a1noc_cfg,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_EMAC] = &xm_emac,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB3] = &xm_usb3_0,
	[MASTER_USB3_1] = &xm_usb3_1,
	[A1NOC_SNOC_SLV] = &qns_a1noc_snoc,
	[SLAVE_SERVICE_A1NOC] = &srvc_aggre1_noc,
};
static struct qcom_icc_desc sm8150_aggre1_noc = {
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
};
static struct qcom_icc_bcm *aggre2_noc_bcms[] = {
	&bcm_ce0,
	&bcm_qup0,
	&bcm_sn14,
	&bcm_sn3,
};
static struct qcom_icc_node *aggre2_noc_nodes[] = {
	[MASTER_A2NOC_CFG] = &qhm_a2noc_cfg,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QSPI] = &qhm_qspi,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_SENSORS_AHB] = &qhm_sensorss_ahb,
	[MASTER_TSIF] = &qhm_tsif,
	[MASTER_CNOC_A2NOC] = &qnm_cnoc,
	[MASTER_CRYPTO_CORE_0] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_PCIE] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_SDCC_4] = &xm_sdc4,
	[A2NOC_SNOC_SLV] = &qns_a2noc_snoc,
	[SLAVE_ANOC_PCIE_GEM_NOC] = &qns_pcie_mem_noc,
	[SLAVE_SERVICE_A2NOC] = &srvc_aggre2_noc,
};
static struct qcom_icc_desc sm8150_aggre2_noc = {
	.nodes = aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_nodes),
	.bcms = aggre2_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_noc_bcms),
};
static struct qcom_icc_bcm *camnoc_virt_bcms[] = {
	&bcm_mm1,
};
static struct qcom_icc_node *camnoc_virt_nodes[] = {
	[MASTER_CAMNOC_HF0_UNCOMP] = &qxm_camnoc_hf0_uncomp,
	[MASTER_CAMNOC_HF1_UNCOMP] = &qxm_camnoc_hf1_uncomp,
	[MASTER_CAMNOC_SF_UNCOMP] = &qxm_camnoc_sf_uncomp,
	[SLAVE_CAMNOC_UNCOMP] = &qns_camnoc_uncomp,
};
static struct qcom_icc_desc sm8150_camnoc_virt = {
	.nodes = camnoc_virt_nodes,
	.num_nodes = ARRAY_SIZE(camnoc_virt_nodes),
	.bcms = camnoc_virt_bcms,
	.num_bcms = ARRAY_SIZE(camnoc_virt_bcms),
};
static struct qcom_icc_bcm *compute_noc_bcms[] = {
	&bcm_co0,
	&bcm_co1,
};
static struct qcom_icc_node *compute_noc_nodes[] = {
	[MASTER_NPU] = &qnm_npu,
	[SLAVE_CDSP_MEM_NOC] = &qns_cdsp_mem_noc,
};
static struct qcom_icc_desc sm8150_compute_noc = {
	.nodes = compute_noc_nodes,
	.num_nodes = ARRAY_SIZE(compute_noc_nodes),
	.bcms = compute_noc_bcms,
	.num_bcms = ARRAY_SIZE(compute_noc_bcms),
};
static struct qcom_icc_bcm *config_noc_bcms[] = {
	&bcm_cn0,
};
static struct qcom_icc_node *config_noc_nodes[] = {
	[MASTER_SPDM] = &qhm_spdm,
	[SNOC_CNOC_MAS] = &qnm_snoc,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[SLAVE_A1NOC_CFG] = &qhs_a1_noc_cfg,
	[SLAVE_A2NOC_CFG] = &qhs_a2_noc_cfg,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy_south,
	[SLAVE_AOP] = &qhs_aop,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CDSP_CFG] = &qhs_compute_dsp,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MMCX_CFG] = &qhs_cpr_mmcx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CNOC_DDRSS] = &qhs_ddrss_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_EMAC_CFG] = &qhs_emac_cfg,
	[SLAVE_GLM] = &qhs_glm,
	[SLAVE_GRAPHICS_3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_CNOC_MNOC_CFG] = &qhs_mnoc_cfg,
	[SLAVE_NPU_CFG] = &qhs_npu_cfg,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie1_cfg,
	[SLAVE_NORTH_PHY_CFG] = &qhs_phy_refgen_north,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QSPI] = &qhs_qspi,
	[SLAVE_QUP_2] = &qhs_qupv3_east,
	[SLAVE_QUP_1] = &qhs_qupv3_north,
	[SLAVE_QUP_0] = &qhs_qupv3_south,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SDCC_4] = &qhs_sdc4,
	[SLAVE_SNOC_CFG] = &qhs_snoc_cfg,
	[SLAVE_SPDM_WRAPPER] = &qhs_spdm,
	[SLAVE_SPSS_CFG] = &qhs_spss_cfg,
	[SLAVE_SSC_CFG] = &qhs_ssc_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM_EAST] = &qhs_tlmm_east,
	[SLAVE_TLMM_NORTH] = &qhs_tlmm_north,
	[SLAVE_TLMM_SOUTH] = &qhs_tlmm_south,
	[SLAVE_TLMM_WEST] = &qhs_tlmm_west,
	[SLAVE_TSIF] = &qhs_tsif,
	[SLAVE_UFS_CARD_CFG] = &qhs_ufs_card_cfg,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3] = &qhs_usb3_0,
	[SLAVE_USB3_1] = &qhs_usb3_1,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_CNOC_A2NOC] = &qns_cnoc_a2noc,
	[SLAVE_SERVICE_CNOC] = &srvc_cnoc,
};
static struct qcom_icc_desc sm8150_config_noc = {
	.nodes = config_noc_nodes,
	.num_nodes = ARRAY_SIZE(config_noc_nodes),
	.bcms = config_noc_bcms,
	.num_bcms = ARRAY_SIZE(config_noc_bcms),
};
static struct qcom_icc_bcm *dc_noc_bcms[] = {
};
static struct qcom_icc_node *dc_noc_nodes[] = {
	[MASTER_CNOC_DC_NOC] = &qhm_cnoc_dc_noc,
	[SLAVE_LLCC_CFG] = &qhs_llcc,
	[SLAVE_GEM_NOC_CFG] = &qhs_memnoc,
};
static struct qcom_icc_desc sm8150_dc_noc = {
	.nodes = dc_noc_nodes,
	.num_nodes = ARRAY_SIZE(dc_noc_nodes),
	.bcms = dc_noc_bcms,
	.num_bcms = ARRAY_SIZE(dc_noc_bcms),
};
static struct qcom_icc_bcm *gem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh2,
	&bcm_sh3,
	&bcm_sh4,
	&bcm_sh5,
};
static struct qcom_icc_node *gem_noc_nodes[] = {
	[MASTER_AMPSS_M0] = &acm_apps,
	[MASTER_GPU_TCU] = &acm_gpu_tcu,
	[MASTER_SYS_TCU] = &acm_sys_tcu,
	[MASTER_GEM_NOC_CFG] = &qhm_gemnoc_cfg,
	[MASTER_COMPUTE_NOC] = &qnm_cmpnoc,
	[MASTER_GRAPHICS_3D] = &qnm_gpu,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_GEM_NOC_PCIE_SNOC] = &qnm_pcie,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[MASTER_ECC] = &qxm_ecc,
	[SLAVE_MSS_PROC_MS_MPU_CFG] = &qhs_mdsp_ms_mpu_cfg,
	[SLAVE_ECC] = &qns_ecc,
	[SLAVE_GEM_NOC_SNOC] = &qns_gem_noc_snoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_SERVICE_GEM_NOC] = &srvc_gemnoc,
};
static struct qcom_icc_desc sm8150_gem_noc = {
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};
static struct qcom_icc_bcm *ipa_virt_bcms[] = {
	&bcm_ip0,
};
static struct qcom_icc_node *ipa_virt_nodes[] = {
	[MASTER_IPA_CORE] = &ipa_core_master,
	[SLAVE_IPA_CORE] = &ipa_core_slave,
};
static struct qcom_icc_desc sm8150_ipa_virt = {
	.nodes = ipa_virt_nodes,
	.num_nodes = ARRAY_SIZE(ipa_virt_nodes),
	.bcms = ipa_virt_bcms,
	.num_bcms = ARRAY_SIZE(ipa_virt_bcms),
};
static struct qcom_icc_bcm *mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_alc,
	&bcm_mc0,
};
static struct qcom_icc_node *mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[MASTER_ALC] = &alc,
	[SLAVE_EBI_CH0] = &ebi,
};
static struct qcom_icc_desc sm8150_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};
static struct qcom_icc_bcm *mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm2,
	&bcm_mm3,
};
static struct qcom_icc_node *mmss_noc_nodes[] = {
	[MASTER_CNOC_MNOC_CFG] = &qhm_mnoc_cfg,
	[MASTER_CAMNOC_HF0] = &qxm_camnoc_hf0,
	[MASTER_CAMNOC_HF1] = &qxm_camnoc_hf1,
	[MASTER_CAMNOC_SF] = &qxm_camnoc_sf,
	[MASTER_MDP_PORT0] = &qxm_mdp0,
	[MASTER_MDP_PORT1] = &qxm_mdp1,
	[MASTER_ROTATOR] = &qxm_rot,
	[MASTER_VIDEO_P0] = &qxm_venus0,
	[MASTER_VIDEO_P1] = &qxm_venus1,
	[MASTER_VIDEO_PROC] = &qxm_venus_arm9,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns2_mem_noc,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_SERVICE_MNOC] = &srvc_mnoc,
};
static struct qcom_icc_desc sm8150_mmss_noc = {
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
};
static struct qcom_icc_bcm *system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn11,
	&bcm_sn12,
	&bcm_sn15,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn5,
	&bcm_sn8,
	&bcm_sn9,
};
static struct qcom_icc_node *system_noc_nodes[] = {
	[MASTER_SNOC_CFG] = &qhm_snoc_cfg,
	[A1NOC_SNOC_MAS] = &qnm_aggre1_noc,
	[A2NOC_SNOC_MAS] = &qnm_aggre2_noc,
	[MASTER_GEM_NOC_SNOC] = &qnm_gemnoc,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_APPSS] = &qhs_apss,
	[SNOC_CNOC_SLV] = &qns_cnoc,
	[SLAVE_SNOC_GEM_NOC_GC] = &qns_gemnoc_gc,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
	[SLAVE_OCIMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};
static struct qcom_icc_desc sm8150_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static struct qcom_icc_bcm *gem_noc_display_bcms[] = {
	&bcm_sh0_display,
};
static struct qcom_icc_node *gem_noc_display_nodes[] = {
	[MASTER_MNOC_HF_MEM_NOC_DISPLAY] = &qnm_mnoc_hf_display,
	[MASTER_MNOC_SF_MEM_NOC_DISPLAY] = &qnm_mnoc_sf_display,
	[SLAVE_LLCC_DISPLAY] = &qns_llcc_display,
};
static struct qcom_icc_desc sm8150_gem_noc_display = {
	.nodes = gem_noc_display_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_display_nodes),
	.bcms = gem_noc_display_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_display_bcms),
};
static struct qcom_icc_bcm *mc_virt_display_bcms[] = {
	&bcm_acv_display,
	&bcm_mc0_display,
};
static struct qcom_icc_node *mc_virt_display_nodes[] = {
	[MASTER_LLCC_DISPLAY] = &llcc_mc_display,
	[SLAVE_EBI_CH0_DISPLAY] = &ebi_display,
};
static struct qcom_icc_desc sm8150_mc_virt_display = {
	.nodes = mc_virt_display_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_display_nodes),
	.bcms = mc_virt_display_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_display_bcms),
};
static struct qcom_icc_bcm *mmss_noc_display_bcms[] = {
	&bcm_mm0_display,
	&bcm_mm1_display,
	&bcm_mm2_display,
	&bcm_mm3_display,
};
static struct qcom_icc_node *mmss_noc_display_nodes[] = {
	[MASTER_MDP_PORT0_DISPLAY] = &qxm_mdp0_display,
	[MASTER_MDP_PORT1_DISPLAY] = &qxm_mdp1_display,
	[MASTER_ROTATOR_DISPLAY] = &qxm_rot_display,
	[SLAVE_MNOC_SF_MEM_NOC_DISPLAY] = &qns2_mem_noc_display,
	[SLAVE_MNOC_HF_MEM_NOC_DISPLAY] = &qns_mem_noc_hf_display,
};
static struct qcom_icc_desc sm8150_mmss_noc_display = {
	.nodes = mmss_noc_display_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_display_nodes),
	.bcms = mmss_noc_display_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_display_bcms),
};

static int qnoc_probe(struct platform_device *pdev)
{
	const struct qcom_icc_desc *desc;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	struct qcom_icc_node **qnodes;
	struct qcom_icc_provider *qp;
	struct icc_node *node;
	size_t num_nodes, i;
	int ret;

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	qnodes = desc->nodes;
	num_nodes = desc->num_nodes;

	qp = devm_kzalloc(&pdev->dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -ENOMEM;

	data = devm_kcalloc(&pdev->dev, num_nodes, sizeof(*node), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	provider = &qp->provider;
	provider->dev = &pdev->dev;
	provider->set = qcom_icc_set;
	provider->aggregate = qcom_icc_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	INIT_LIST_HEAD(&provider->nodes);
	provider->data = data;

	qp->dev = &pdev->dev;
	qp->bcms = desc->bcms;
	qp->num_bcms = desc->num_bcms;

	qp->voter = of_bcm_voter_get(qp->dev, NULL);
	if (IS_ERR(qp->voter))
		return PTR_ERR(qp->voter);

	ret = icc_provider_add(provider);
	if (ret) {
		dev_err(&pdev->dev, "error adding interconnect provider\n");
		return ret;
	}

	for (i = 0; i < num_nodes; i++) {
		size_t j;

		if (!qnodes[i])
			continue;

		node = icc_node_create(qnodes[i]->id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err;
		}

		node->name = qnodes[i]->name;
		node->data = qnodes[i];
		icc_node_add(node, provider);

		dev_dbg(&pdev->dev, "registered node %pK %s %d\n", node,
			qnodes[i]->name, node->id);

		/* populate links */
		for (j = 0; j < qnodes[i]->num_links; j++)
			icc_link_create(node, qnodes[i]->links[j]);

		data->nodes[i] = node;
	}
	data->num_nodes = num_nodes;

	for (i = 0; i < qp->num_bcms; i++)
		qcom_icc_bcm_init(qp->bcms[i], &pdev->dev);

	platform_set_drvdata(pdev, qp);

	dev_dbg(&pdev->dev, "Registered sm8150 ICC\n");

	return ret;
err:
	list_for_each_entry(node, &provider->nodes, node_list) {
		icc_node_del(node);
		icc_node_destroy(node->id);
	}

	icc_provider_del(provider);
	return ret;
}

static int qnoc_remove(struct platform_device *pdev)
{
	struct qcom_icc_provider *qp = platform_get_drvdata(pdev);
	struct icc_provider *provider = &qp->provider;
	struct icc_node *n;

	list_for_each_entry(n, &provider->nodes, node_list) {
		icc_node_del(n);
		icc_node_destroy(n->id);
	}

	return icc_provider_del(provider);
}
static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sm8150-aggre1_noc", .data = &sm8150_aggre1_noc},
	{ .compatible = "qcom,sm8150-aggre2_noc", .data = &sm8150_aggre2_noc},
	{ .compatible = "qcom,sm8150-camnoc_virt", .data = &sm8150_camnoc_virt},
	{ .compatible = "qcom,sm8150-compute_noc", .data = &sm8150_compute_noc},
	{ .compatible = "qcom,sm8150-config_noc", .data = &sm8150_config_noc},
	{ .compatible = "qcom,sm8150-dc_noc", .data = &sm8150_dc_noc},
	{ .compatible = "qcom,sm8150-gem_noc", .data = &sm8150_gem_noc},
	{ .compatible = "qcom,sm8150-ipa_virt", .data = &sm8150_ipa_virt},
	{ .compatible = "qcom,sm8150-mc_virt", .data = &sm8150_mc_virt},
	{ .compatible = "qcom,sm8150-mmss_noc", .data = &sm8150_mmss_noc},
	{ .compatible = "qcom,sm8150-system_noc", .data = &sm8150_system_noc},
	{ .compatible = "qcom,sm8150-gem_noc_display", .data = &sm8150_gem_noc_display},
	{ .compatible = "qcom,sm8150-mc_virt_display", .data = &sm8150_mc_virt_display},
	{ .compatible = "qcom,sm8150-mmss_noc_display", .data = &sm8150_mmss_noc_display},
	{ },
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-sm8150",
		.of_match_table = qnoc_of_match,
	},
};
module_platform_driver(qnoc_driver);

MODULE_DESCRIPTION("sm8150 NoC driver");
MODULE_LICENSE("GPL v2");
