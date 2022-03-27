// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2017-2022 The Linux Foundation. All rights reserved. */

#include "msm_gem.h"
#include "msm_mmu.h"
#include "msm_gpu_trace.h"
#include "a7xx_gpu.h"

#include <linux/bitfield.h>

extern bool hang_debug;

#define GPU_PAS_ID 13

static inline bool _a7xx_check_idle(struct msm_gpu *gpu)
{
	/* Check that the CX master is idle */
	if (gpu_read(gpu, REG_A7XX_RBBM_STATUS) & ~A7XX_RBBM_STATUS_CPAHBBUSYCXMASTER)
		return false;

	return !(gpu_read(gpu, REG_A7XX_RBBM_INT_0_STATUS) & A7XX_RBBM_INT_0_MASK_HANGDETECTINTERRUPT);
}

static bool a7xx_idle(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	/* wait for CP to drain ringbuffer: */
	if (!adreno_idle(gpu, ring))
		return false;

	if (spin_until(_a7xx_check_idle(gpu))) {
		DRM_ERROR("%s: %ps: timeout waiting for GPU to idle: status %8.8X irq %8.8X rptr/wptr %d/%d\n",
			gpu->name, __builtin_return_address(0),
			gpu_read(gpu, REG_A7XX_RBBM_STATUS),
			gpu_read(gpu, REG_A7XX_RBBM_INT_0_STATUS),
			gpu_read(gpu, REG_A7XX_CP_RB_RPTR),
			gpu_read(gpu, REG_A7XX_CP_RB_WPTR));
		return false;
	}

	return true;
}

static void a7xx_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit)
{
	struct msm_ringbuffer *ring = submit->ring;
	unsigned int i;

	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, CP_SET_THREAD_BOTH);

	OUT_PKT7(ring, CP_SET_MARKER, 1);
	OUT_RING(ring, 0x101); /* IFPC disable */

	OUT_PKT7(ring, CP_SET_MARKER, 1);
	OUT_RING(ring, 0x00d); /* IB1LIST start */

	/* Submit the commands */
	for (i = 0; i < submit->nr_cmds; i++) {
		switch (submit->cmd[i].type) {
		case MSM_SUBMIT_CMD_IB_TARGET_BUF:
			break;
		case MSM_SUBMIT_CMD_CTX_RESTORE_BUF:
			if (gpu->cur_ctx_seqno == submit->queue->ctx->seqno)
				break;
			fallthrough;
		case MSM_SUBMIT_CMD_BUF:
			OUT_PKT7(ring, CP_INDIRECT_BUFFER_PFE, 3);
			OUT_RING(ring, lower_32_bits(submit->cmd[i].iova));
			OUT_RING(ring, upper_32_bits(submit->cmd[i].iova));
			OUT_RING(ring, submit->cmd[i].size);
			break;
		}
	}

	OUT_PKT7(ring, CP_SET_MARKER, 1);
	OUT_RING(ring, 0x00e); /* IB1LIST end */

	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, CP_SET_THREAD_BR);

	OUT_PKT7(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, CCU_INVALIDATE_DEPTH);

	OUT_PKT7(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, CCU_INVALIDATE_COLOR);

	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, CP_SET_THREAD_BV);

	/*
	 * Make sure the timestamp is committed once BV pipe is
	 * completely done with this submission.
	 */
	OUT_PKT7(ring, CP_EVENT_WRITE, 4);
	OUT_RING(ring, CACHE_CLEAN | BIT(27));
	OUT_RING(ring, lower_32_bits(rbmemptr(ring, bv_fence)));
	OUT_RING(ring, upper_32_bits(rbmemptr(ring, bv_fence)));
	OUT_RING(ring, submit->seqno);

	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, CP_SET_THREAD_BR);

	/*
	 * This makes sure that BR doesn't race ahead and commit
	 * timestamp to memstore while BV is still processing
	 * this submission.
	 */
	OUT_PKT7(ring, CP_WAIT_TIMESTAMP, 4);
	OUT_RING(ring, 0);
	OUT_RING(ring, lower_32_bits(rbmemptr(ring, bv_fence)));
	OUT_RING(ring, upper_32_bits(rbmemptr(ring, bv_fence)));
	OUT_RING(ring, submit->seqno);

	/* write the ringbuffer timestamp */
	OUT_PKT7(ring, CP_EVENT_WRITE, 4);
	OUT_RING(ring, CACHE_CLEAN | BIT(31) | BIT(27));
	OUT_RING(ring, lower_32_bits(rbmemptr(ring, fence)));
	OUT_RING(ring, upper_32_bits(rbmemptr(ring, fence)));
	OUT_RING(ring, submit->seqno);

	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, CP_SET_THREAD_BOTH);

	OUT_PKT7(ring, CP_SET_MARKER, 1);
	OUT_RING(ring, 0x100); /* IFPC enable */

	trace_msm_gpu_submit_flush(submit, gpu_read64(gpu, REG_A7XX_CP_ALWAYS_ON_COUNTER));

	adreno_flush(gpu, ring, REG_A7XX_CP_RB_WPTR);
}

const struct adreno_reglist a730_hwcg[] = {
	{ REG_A7XX_RBBM_CLOCK_CNTL_SP0, 0x02222222 },
	{ REG_A7XX_RBBM_CLOCK_CNTL2_SP0, 0x02022222 },
	{ REG_A7XX_RBBM_CLOCK_HYST_SP0, 0x0000f3cf },
	{ REG_A7XX_RBBM_CLOCK_DELAY_SP0, 0x00000080 },
	{ REG_A7XX_RBBM_CLOCK_CNTL_TP0, 0x22222220 },
	{ REG_A7XX_RBBM_CLOCK_CNTL2_TP0, 0x22222222 },
	{ REG_A7XX_RBBM_CLOCK_CNTL3_TP0, 0x22222222 },
	{ REG_A7XX_RBBM_CLOCK_CNTL4_TP0, 0x00222222 },
	{ REG_A7XX_RBBM_CLOCK_HYST_TP0, 0x77777777 },
	{ REG_A7XX_RBBM_CLOCK_HYST2_TP0, 0x77777777 },
	{ REG_A7XX_RBBM_CLOCK_HYST3_TP0, 0x77777777 },
	{ REG_A7XX_RBBM_CLOCK_HYST4_TP0, 0x00077777 },
	{ REG_A7XX_RBBM_CLOCK_DELAY_TP0, 0x11111111 },
	{ REG_A7XX_RBBM_CLOCK_DELAY2_TP0, 0x11111111 },
	{ REG_A7XX_RBBM_CLOCK_DELAY3_TP0, 0x11111111 },
	{ REG_A7XX_RBBM_CLOCK_DELAY4_TP0, 0x00011111 },
	{ REG_A7XX_RBBM_CLOCK_CNTL_UCHE, 0x22222222 },
	{ REG_A7XX_RBBM_CLOCK_HYST_UCHE, 0x00000004 },
	{ REG_A7XX_RBBM_CLOCK_DELAY_UCHE, 0x00000002 },
	{ REG_A7XX_RBBM_CLOCK_CNTL_RB0, 0x22222222 },
	{ REG_A7XX_RBBM_CLOCK_CNTL2_RB0, 0x01002222 },
	{ REG_A7XX_RBBM_CLOCK_CNTL_CCU0, 0x00002220 },
	{ REG_A7XX_RBBM_CLOCK_HYST_RB_CCU0, 0x44000f00 },
	{ REG_A7XX_RBBM_CLOCK_CNTL_RAC, 0x25222022 },
	{ REG_A7XX_RBBM_CLOCK_CNTL2_RAC, 0x00555555 },
	{ REG_A7XX_RBBM_CLOCK_DELAY_RAC, 0x00000011 },
	{ REG_A7XX_RBBM_CLOCK_HYST_RAC, 0x00440044 },
	{ REG_A7XX_RBBM_CLOCK_CNTL_TSE_RAS_RBBM, 0x04222222 },
	{ REG_A7XX_RBBM_CLOCK_MODE2_GRAS, 0x00000222 },
	{ REG_A7XX_RBBM_CLOCK_MODE_BV_GRAS, 0x00222222 },
	{ REG_A7XX_RBBM_CLOCK_MODE_GPC, 0x02222223 },
	{ REG_A7XX_RBBM_CLOCK_MODE_VFD, 0x00002222 },
	{ REG_A7XX_RBBM_CLOCK_MODE_BV_GPC, 0x00222222 },
	{ REG_A7XX_RBBM_CLOCK_MODE_BV_VFD, 0x00002222 },
	{ REG_A7XX_RBBM_CLOCK_HYST_TSE_RAS_RBBM, 0x00000000 },
	{ REG_A7XX_RBBM_CLOCK_HYST_GPC, 0x04104004 },
	{ REG_A7XX_RBBM_CLOCK_HYST_VFD, 0x00000000 },
	{ REG_A7XX_RBBM_CLOCK_DELAY_TSE_RAS_RBBM, 0x00004000 },
	{ REG_A7XX_RBBM_CLOCK_DELAY_GPC, 0x00000200 },
	{ REG_A7XX_RBBM_CLOCK_DELAY_VFD, 0x00002222 },
	{ REG_A7XX_RBBM_CLOCK_MODE_HLSQ, 0x00002222 },
	{ REG_A7XX_RBBM_CLOCK_DELAY_HLSQ, 0x00000000 },
	{ REG_A7XX_RBBM_CLOCK_HYST_HLSQ, 0x00000000 },
	{ REG_A7XX_RBBM_CLOCK_DELAY_HLSQ_2, 0x00000002 },
	{ REG_A7XX_RBBM_CLOCK_MODE_BV_LRZ, 0x55555552 },
	{ REG_A7XX_RBBM_CLOCK_MODE_CP, 0x00000223 },
	{ REG_A7XX_RBBM_CLOCK_CNTL, 0x8aa8aa82 },
	{ REG_A7XX_RBBM_ISDB_CNT, 0x00000182 },
	{ REG_A7XX_RBBM_RAC_THRESHOLD_CNT, 0x00000000 },
	{ REG_A7XX_RBBM_SP_HYST_CNT, 0x00000000 },
	{ REG_A7XX_RBBM_CLOCK_CNTL_GMU_GX, 0x00000222 },
	{ REG_A7XX_RBBM_CLOCK_DELAY_GMU_GX, 0x00000111 },
	{ REG_A7XX_RBBM_CLOCK_HYST_GMU_GX, 0x00000555 },
	{},
};

#define RBBM_CLOCK_CNTL_ON 0x8aa8aa82

static void a7xx_set_hwcg(struct msm_gpu *gpu, bool state)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	const struct adreno_reglist *reg;
	unsigned int i;
	u32 val;

	if (!adreno_gpu->info->hwcg)
		return;

	val = gpu_read(gpu, REG_A7XX_RBBM_CLOCK_CNTL);

	/* Don't re-program the registers if they are already correct */
	if ((val == RBBM_CLOCK_CNTL_ON) == state)
		return;

	for (i = 0; (reg = &adreno_gpu->info->hwcg[i], reg->offset); i++)
		gpu_write(gpu, reg->offset, state ? reg->value : 0);

	gpu_write(gpu, REG_A7XX_RBBM_CLOCK_CNTL, state ? RBBM_CLOCK_CNTL_ON : 0);
}

static const u32 a730_protect[] = {
	A6XX_PROTECT_RDONLY(0x00000, 0x04ff),
	A6XX_PROTECT_RDONLY(0x0050b, 0x0058),
	A6XX_PROTECT_NORDWR(0x0050e, 0x0000),
	A6XX_PROTECT_NORDWR(0x00510, 0x0000),
	A6XX_PROTECT_NORDWR(0x00534, 0x0000),
	A6XX_PROTECT_RDONLY(0x005fb, 0x009d),
	A6XX_PROTECT_NORDWR(0x00699, 0x01e9),
	A6XX_PROTECT_NORDWR(0x008a0, 0x0008),
	A6XX_PROTECT_NORDWR(0x008ab, 0x0024),
	A6XX_PROTECT_RDONLY(0x008d0, 0x0170),
	A6XX_PROTECT_NORDWR(0x00900, 0x004d),
	A6XX_PROTECT_NORDWR(0x0098d, 0x00b2),
	A6XX_PROTECT_NORDWR(0x00a41, 0x01be),
	A6XX_PROTECT_NORDWR(0x00df0, 0x0001),
	A6XX_PROTECT_NORDWR(0x00e01, 0x0000),
	A6XX_PROTECT_NORDWR(0x00e07, 0x0008),
	A6XX_PROTECT_NORDWR(0x03c00, 0x00c3),
	A6XX_PROTECT_RDONLY(0x03cc4, 0x1fff),
	A6XX_PROTECT_NORDWR(0x08630, 0x01cf),
	A6XX_PROTECT_NORDWR(0x08e00, 0x0000),
	A6XX_PROTECT_NORDWR(0x08e08, 0x0000),
	A6XX_PROTECT_NORDWR(0x08e50, 0x001f),
	A6XX_PROTECT_NORDWR(0x08e80, 0x0280),
	A6XX_PROTECT_NORDWR(0x09624, 0x01db),
	A6XX_PROTECT_NORDWR(0x09e40, 0x0000),
	A6XX_PROTECT_NORDWR(0x09e64, 0x000d),
	A6XX_PROTECT_NORDWR(0x09e78, 0x0187),
	A6XX_PROTECT_NORDWR(0x0a630, 0x01cf),
	A6XX_PROTECT_NORDWR(0x0ae02, 0x0000),
	A6XX_PROTECT_NORDWR(0x0ae50, 0x000f),
	A6XX_PROTECT_NORDWR(0x0ae66, 0x0003),
	A6XX_PROTECT_NORDWR(0x0ae6f, 0x0003),
	A6XX_PROTECT_NORDWR(0x0b604, 0x0003),
	A6XX_PROTECT_NORDWR(0x0ec00, 0x0fff),
	A6XX_PROTECT_RDONLY(0x0fc00, 0x1fff),
	A6XX_PROTECT_NORDWR(0x18400, 0x0053),
	A6XX_PROTECT_RDONLY(0x18454, 0x0004),
	A6XX_PROTECT_NORDWR(0x18459, 0x1fff),
	A6XX_PROTECT_NORDWR(0x1a459, 0x1fff),
	A6XX_PROTECT_NORDWR(0x1c459, 0x1fff),
	A6XX_PROTECT_NORDWR(0x1f400, 0x0443),
	A6XX_PROTECT_RDONLY(0x1f844, 0x007b),
	A6XX_PROTECT_NORDWR(0x1f860, 0x0000),
	A6XX_PROTECT_NORDWR(0x1f878, 0x002a),
	A6XX_PROTECT_NORDWR(0x1f8c0, 0x0000), /* note: infinite range */
};

static void a7xx_set_cp_protect(struct msm_gpu *gpu)
{
	const u32 *regs = a730_protect;
	unsigned i, count, count_max;

	regs = a730_protect;
	count = ARRAY_SIZE(a730_protect);
	count_max = 48;
	BUILD_BUG_ON(ARRAY_SIZE(a730_protect) > 48);

	/*
	 * Enable access protection to privileged registers, fault on an access
	 * protect violation and select the last span to protect from the start
	 * address all the way to the end of the register address space
	 */
	gpu_write(gpu, REG_A7XX_CP_PROTECT_CNTL, BIT(0) | BIT(1) | BIT(3));

	for (i = 0; i < count - 1; i++)
		gpu_write(gpu, REG_A7XX_CP_PROTECT_REG(i), regs[i]);
	/* last CP_PROTECT to have "infinite" length on the last entry */
	gpu_write(gpu, REG_A7XX_CP_PROTECT_REG(count_max - 1), regs[i]);
}

static void a7xx_set_ubwc_config(struct msm_gpu *gpu)
{
	u32 lower_bit = 3;
	u32 amsbc = 1;
	u32 rgb565_predicator = 1;
	u32 uavflagprd_inv = 2;

	gpu_write(gpu, REG_A7XX_RB_NC_MODE_CNTL, rgb565_predicator << 11 | amsbc << 4 | lower_bit << 1);
	gpu_write(gpu, REG_A7XX_TPL1_NC_MODE_CNTL, lower_bit << 1);
	gpu_write(gpu, REG_A7XX_SP_NC_MODE_CNTL, uavflagprd_inv << 4 | lower_bit << 1);
	gpu_write(gpu, REG_A7XX_GRAS_NC_MODE_CNTL, lower_bit << 5);
	gpu_write(gpu, REG_A7XX_UCHE_MODE_CNTL, lower_bit << 21);
}

static int a7xx_cp_init(struct msm_gpu *gpu)
{
	struct msm_ringbuffer *ring = gpu->rb[0];

	/* Disable concurrent binning before sending CP init */
	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, BIT(27));

	OUT_PKT7(ring, CP_ME_INIT, 7);
	OUT_RING(ring, BIT(0) | /* Use multiple HW contexts */
		BIT(1) | /* Enable error detection */
		BIT(3) | /* Set default reset state */
		BIT(6) | /* Disable save/restore of performance counters across preemption */
		BIT(8)); /* Enable the register init list with the spinlock */
	OUT_RING(ring, 0x00000003); /* Set number of HW contexts */
	OUT_RING(ring, 0x20000000); /* Enable error detection */
	OUT_RING(ring, 0x00000002); /* Operation mode mask */
	/* Register initialization list with spinlock (TODO used for IFPC/preemption) */
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);

	adreno_flush(gpu, ring, REG_A7XX_CP_RB_WPTR);
	return a7xx_idle(gpu, ring) ? 0 : -EINVAL;
}

static int a7xx_ucode_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a7xx_gpu *a7xx_gpu = to_a7xx_gpu(adreno_gpu);

	if (!a7xx_gpu->sqe_bo) {
		a7xx_gpu->sqe_bo = adreno_fw_create_bo(gpu,
			adreno_gpu->fw[ADRENO_FW_SQE], &a7xx_gpu->sqe_iova);

		if (IS_ERR(a7xx_gpu->sqe_bo)) {
			int ret = PTR_ERR(a7xx_gpu->sqe_bo);

			a7xx_gpu->sqe_bo = NULL;
			DRM_DEV_ERROR(&gpu->pdev->dev,
				"Could not allocate SQE ucode: %d\n", ret);

			return ret;
		}

		msm_gem_object_set_name(a7xx_gpu->sqe_bo, "sqefw");
	}

	gpu_write64(gpu, REG_A7XX_CP_SQE_INSTR_BASE, a7xx_gpu->sqe_iova);

	return 0;
}

static int a7xx_zap_shader_init(struct msm_gpu *gpu)
{
	static bool loaded;
	int ret;

	if (loaded)
		return 0;

	ret = adreno_zap_shader_load(gpu, GPU_PAS_ID);

	loaded = !ret;
	return ret;
}

#define A7XX_INT_MASK ( \
	A7XX_RBBM_INT_0_MASK_AHBERROR | \
	A7XX_RBBM_INT_0_MASK_ATBASYNCFIFOOVERFLOW | \
	A7XX_RBBM_INT_0_MASK_GPCERROR | \
	A7XX_RBBM_INT_0_MASK_SWINTERRUPT | \
	A7XX_RBBM_INT_0_MASK_HWERROR | \
	A7XX_RBBM_INT_0_MASK_PM4CPINTERRUPT | \
	A7XX_RBBM_INT_0_MASK_RB_DONE_TS | \
	A7XX_RBBM_INT_0_MASK_CACHE_CLEAN_TS | \
	A7XX_RBBM_INT_0_MASK_ATBBUSOVERFLOW | \
	A7XX_RBBM_INT_0_MASK_HANGDETECTINTERRUPT | \
	A7XX_RBBM_INT_0_MASK_OUTOFBOUNDACCESS | \
	A7XX_RBBM_INT_0_MASK_UCHETRAPINTERRUPT | \
	A7XX_RBBM_INT_0_MASK_TSBWRITEERROR)

/*
 * All Gen7 targets support marking certain transactions as always privileged
 * which allows us to mark more memory as privileged without having to
 * explicitly set the APRIV bit. Choose the following transactions to be
 * privileged by default:
 * CDWRITE     [6:6] - Crashdumper writes
 * CDREAD      [5:5] - Crashdumper reads
 * RBRPWB      [3:3] - RPTR shadow writes
 * RBPRIVLEVEL [2:2] - Memory accesses from PM4 packets in the ringbuffer
 * RBFETCH     [1:1] - Ringbuffer reads
 * ICACHE      [0:0] - Instruction cache fetches
 */

#define A7XX_APRIV_DEFAULT (BIT(3) | BIT(2) | BIT(1) | BIT(0))
/* Add crashdumper permissions for the BR APRIV */
#define A7XX_BR_APRIV_DEFAULT (A7XX_APRIV_DEFAULT | BIT(6) | BIT(5))

static int a7xx_hw_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a7xx_gpu *a7xx_gpu = to_a7xx_gpu(adreno_gpu);
	int ret;

	/* Set up GBIF registers */
	gpu_write(gpu, REG_A7XX_GBIF_QSB_SIDE0, 0x00071620);
	gpu_write(gpu, REG_A7XX_GBIF_QSB_SIDE1, 0x00071620);
	gpu_write(gpu, REG_A7XX_GBIF_QSB_SIDE2, 0x00071620);
	gpu_write(gpu, REG_A7XX_GBIF_QSB_SIDE3, 0x00071620);
	gpu_write(gpu, REG_A7XX_GBIF_QSB_SIDE3, 0x00071620);
	gpu_write(gpu, REG_A7XX_RBBM_GBIF_CLIENT_QOS_CNTL, 0x2120212);
	gpu_write(gpu, REG_A7XX_UCHE_GBIF_GX_CONFIG, 0x10240e0);

	/* Make all blocks contribute to the GPU BUSY perf counter */
	gpu_write(gpu, REG_A7XX_RBBM_PERFCTR_GPU_BUSY_MASKED, 0xffffffff);

	/*
	 * Set UCHE_WRITE_THRU_BASE to the UCHE_TRAP_BASE effectively
	 * disabling L2 bypass
	 */
	gpu_write64(gpu, REG_A7XX_UCHE_TRAP_BASE, ~0ull);
	gpu_write64(gpu, REG_A7XX_UCHE_WRITE_THRU_BASE, ~0ull);

	gpu_write(gpu, REG_A7XX_UCHE_CACHE_WAYS, 0x800000);
	gpu_write(gpu, REG_A7XX_UCHE_CMDQ_CONFIG, 6 << 16 | 6 << 12 | 9 << 8 | BIT(3) | BIT(2) | 2);

	/* Set the AHB default slave response to "ERROR" */
	gpu_write(gpu, REG_A7XX_CP_AHB_CNTL, 0x1);

	/* Turn on performance counters */
	gpu_write(gpu, REG_A7XX_RBBM_PERFCTR_CNTL, 0x1);

	a7xx_set_ubwc_config(gpu);

	gpu_write(gpu, REG_A7XX_RBBM_INTERFACE_HANG_INT_CNTL, BIT(30) | 0xcfffff);
	gpu_write(gpu, REG_A7XX_UCHE_CLIENT_PF, BIT(0));

	a7xx_set_cp_protect(gpu);

	/* TODO: Configure LLCC */

	gpu_write(gpu, REG_A7XX_CP_APRIV_CNTL, A7XX_BR_APRIV_DEFAULT);
	gpu_write(gpu, REG_A7XX_CP_BV_APRIV_CNTL, A7XX_APRIV_DEFAULT);
	gpu_write(gpu, REG_A7XX_CP_LPAC_APRIV_CNTL, A7XX_APRIV_DEFAULT);

	gpu_write(gpu, REG_A7XX_RBBM_SECVID_TSB_CNTL, 0);
	gpu_write64(gpu, REG_A7XX_RBBM_SECVID_TSB_TRUSTED_BASE, 0);
	gpu_write(gpu, REG_A7XX_RBBM_SECVID_TSB_TRUSTED_SIZE, 0);

	a7xx_set_hwcg(gpu, true);

	/* Enable interrupts */
	gpu_write(gpu, REG_A7XX_RBBM_INT_0_MASK, A7XX_INT_MASK);

	ret = adreno_hw_init(gpu);
	if (ret)
		return ret;

	/* reset the value of bv_fence too */
	gpu->rb[0]->memptrs->bv_fence = gpu->rb[0]->fctx->completed_fence;

	ret = a7xx_ucode_init(gpu);
	if (ret)
		return ret;

	/* Set the ringbuffer address and setup rptr shadow */
	gpu_write64(gpu, REG_A7XX_CP_RB_BASE, gpu->rb[0]->iova);

	gpu_write(gpu, REG_A7XX_CP_RB_CNTL, MSM_GPU_RB_CNTL_DEFAULT);

	if (!a7xx_gpu->shadow_bo) {
		a7xx_gpu->shadow = msm_gem_kernel_new(gpu->dev,
			sizeof(u32) * gpu->nr_rings,
			MSM_BO_WC | MSM_BO_MAP_PRIV,
			gpu->aspace, &a7xx_gpu->shadow_bo,
			&a7xx_gpu->shadow_iova);

		if (IS_ERR(a7xx_gpu->shadow))
			return PTR_ERR(a7xx_gpu->shadow);

		msm_gem_object_set_name(a7xx_gpu->shadow_bo, "shadow");
	}

	gpu_write64(gpu, REG_A7XX_CP_RB_RPTR_ADDR, shadowptr(a7xx_gpu, gpu->rb[0]));

	gpu->cur_ctx_seqno = 0;

	/* Enable the SQE_to start the CP engine */
	gpu_write(gpu, REG_A7XX_CP_SQE_CNTL, 1);

	ret = a7xx_cp_init(gpu);
	if (ret)
		return ret;

	/*
	 * Try to load a zap shader into the secure world. If successful
	 * we can use the CP to switch out of secure mode. If not then we
	 * have no resource but to try to switch ourselves out manually. If we
	 * guessed wrong then access to the RBBM_SECVID_TRUST_CNTL register will
	 * be blocked and a permissions violation will soon follow.
	 */
	ret = a7xx_zap_shader_init(gpu);
	if (!ret) {
		OUT_PKT7(gpu->rb[0], CP_SET_SECURE_MODE, 1);
		OUT_RING(gpu->rb[0], 0x00000000);

		adreno_flush(gpu, gpu->rb[0], REG_A7XX_CP_RB_WPTR);
		if (!a7xx_idle(gpu, gpu->rb[0]))
			return -EINVAL;
	} else if (ret == -ENODEV) {
		/*
		 * This device does not use zap shader (but print a warning
		 * just in case someone got their dt wrong.. hopefully they
		 * have a debug UART to realize the error of their ways...
		 * if you mess this up you are about to crash horribly)
		 */
		dev_warn_once(gpu->dev->dev,
			"Zap shader not enabled - using SECVID_TRUST_CNTL instead\n");
		gpu_write(gpu, REG_A7XX_RBBM_SECVID_TRUST_CNTL, 0x0);
		ret = 0;
	} else {
		return ret;
	}

	return ret;
}

static void a7xx_dump(struct msm_gpu *gpu)
{
	DRM_DEV_INFO(&gpu->pdev->dev, "status:   %08x\n",
			gpu_read(gpu, REG_A7XX_RBBM_STATUS));
	adreno_dump(gpu);
}

static void a7xx_recover(struct msm_gpu *gpu)
{
	adreno_dump_info(gpu);

	if (hang_debug)
		a7xx_dump(gpu);

	gpu_write(gpu, REG_A7XX_RBBM_SW_RESET_CMD, 1);
	/*
	 * Do a dummy read to get a brief read cycle delay for the
	 * reset to take effect
	 * (does this work as expected for a7xx?)
	 */
	gpu_read(gpu, REG_A7XX_RBBM_SW_RESET_CMD);
	gpu_write(gpu, REG_A7XX_RBBM_SW_RESET_CMD, 0);

	gpu->needs_hw_init = true;
	msm_gpu_hw_init(gpu);
}

static void a7xx_cp_hw_err_irq(struct msm_gpu *gpu)
{
	struct device *dev = &gpu->pdev->dev;
	u32 status = gpu_read(gpu, REG_A7XX_CP_INTERRUPT_STATUS);
	u32 val;

	if (status & A7XX_CP_INTERRUPT_STATUS_OPCODEERROR) {
		gpu_write(gpu, REG_A7XX_CP_SQE_STAT_ADDR, 1);
		val = gpu_read(gpu, REG_A7XX_CP_SQE_STAT_DATA);
		dev_err_ratelimited(dev,"CP | opcode error | possible opcode=0x%8.8X\n", val);
	}

	if (status & A7XX_CP_INTERRUPT_STATUS_UCODEERROR)
		dev_err_ratelimited(dev, "CP ucode error interrupt\n");

	if (status & A7XX_CP_INTERRUPT_STATUS_CPHWFAULT)
		dev_err_ratelimited(dev, "CP | HW fault | status=0x%8.8X\n", gpu_read(gpu, REG_A7XX_CP_HW_FAULT));

	if (status & A7XX_CP_INTERRUPT_STATUS_REGISTERPROTECTION) {
		val = gpu_read(gpu, REG_A7XX_CP_PROTECT_STATUS);
		dev_err_ratelimited(dev,
			"CP | protected mode error | %s | addr=0x%8.8X | status=0x%8.8X\n",
			val & (1 << 20) ? "READ" : "WRITE",
			(val & 0x3ffff), val);
	}

	if (status & A7XX_CP_INTERRUPT_STATUS_VSDPARITYERROR)
		dev_err_ratelimited(dev,"CP VSD decoder parity error\n");

	if (status & A7XX_CP_INTERRUPT_STATUS_ILLEGALINSTRUCTION)
		dev_err_ratelimited(dev,"CP illegal instruction error\n");

	if (status & A7XX_CP_INTERRUPT_STATUS_OPCODEERRORLPAC)
		dev_err_ratelimited(dev, "CP opcode error LPAC\n");

	if (status & A7XX_CP_INTERRUPT_STATUS_UCODEERRORLPAC)
		dev_err_ratelimited(dev, "CP ucode error LPAC\n");

	if (status & A7XX_CP_INTERRUPT_STATUS_CPHWFAULTLPAC)
		dev_err_ratelimited(dev, "CP hw fault LPAC\n");

	if (status & A7XX_CP_INTERRUPT_STATUS_REGISTERPROTECTIONLPAC)
		dev_err_ratelimited(dev, "CP register protection LPAC\n");

	if (status & A7XX_CP_INTERRUPT_STATUS_ILLEGALINSTRUCTIONLPAC)
		dev_err_ratelimited(dev, "CP illegal instruction LPAC\n");

	if (status & A7XX_CP_INTERRUPT_STATUS_OPCODEERRORBV) {
		gpu_write(gpu, REG_A7XX_CP_BV_SQE_STAT_ADDR, 1);
		val = gpu_read(gpu, REG_A7XX_CP_BV_SQE_STAT_DATA);
		dev_err_ratelimited(dev, "CP opcode error BV | opcode=0x%8.8x\n", val);
	}

	if (status & A7XX_CP_INTERRUPT_STATUS_UCODEERRORBV)
		dev_err_ratelimited(dev, "CP ucode error BV\n");

	if (status & A7XX_CP_INTERRUPT_STATUS_CPHWFAULTBV) {
		val = gpu_read(gpu, REG_A7XX_CP_BV_HW_FAULT);
		dev_err_ratelimited(dev, "CP BV | Ringbuffer HW fault | status=%x\n", val);
	}

	if (status & A7XX_CP_INTERRUPT_STATUS_REGISTERPROTECTIONBV) {
		val = gpu_read(gpu, REG_A7XX_CP_BV_PROTECT_STATUS);
		dev_err_ratelimited(dev,
			"CP BV | protected mode error | %s | addr=0x%8.8X | status=0x%8.8X\n",
			val & BIT(20) ? "READ" : "WRITE",
			val & 0x3ffff, val);
	}

	if (status & A7XX_CP_INTERRUPT_STATUS_ILLEGALINSTRUCTIONBV)
		dev_err_ratelimited(dev, "CP illegal instruction BV\n");
}

static irqreturn_t a7xx_irq(struct msm_gpu *gpu)
{
	struct msm_drm_private *priv = gpu->dev->dev_private;
	u32 status = gpu_read(gpu, REG_A7XX_RBBM_INT_0_STATUS);

	gpu_write(gpu, REG_A7XX_RBBM_INT_CLEAR_CMD, status);

	if (priv->disable_err_irq)
		status &= A7XX_RBBM_INT_0_MASK_CACHE_CLEAN_TS;

	/* TODO: print human friendly strings for each error ? */
	if (status & ~A7XX_RBBM_INT_0_MASK_CACHE_CLEAN_TS)
		dev_err_ratelimited(&gpu->pdev->dev, "unexpected irq status: 0x%8.8X\n", status);

	if (status & A7XX_RBBM_INT_0_MASK_HWERROR)
		a7xx_cp_hw_err_irq(gpu);

	if (status & A7XX_RBBM_INT_0_MASK_CACHE_CLEAN_TS)
		msm_gpu_retire(gpu);

	return IRQ_HANDLED;
}

static int a7xx_get_timestamp(struct msm_gpu *gpu, uint64_t *value)
{
	*value = gpu_read64(gpu, REG_A7XX_CP_ALWAYS_ON_COUNTER);
	return 0;
}

static void a7xx_destroy(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a7xx_gpu *a7xx_gpu = to_a7xx_gpu(adreno_gpu);

	if (a7xx_gpu->sqe_bo) {
		msm_gem_unpin_iova(a7xx_gpu->sqe_bo, gpu->aspace);
		drm_gem_object_put(a7xx_gpu->sqe_bo);
	}

	if (a7xx_gpu->shadow_bo) {
		msm_gem_unpin_iova(a7xx_gpu->shadow_bo, gpu->aspace);
		drm_gem_object_put(a7xx_gpu->shadow_bo);
	}

	adreno_gpu_cleanup(adreno_gpu);

	kfree(a7xx_gpu);
}

static struct msm_gpu_state *a7xx_gpu_state_get(struct msm_gpu *gpu)
{
	struct msm_gpu_state *state = kzalloc(sizeof(*state), GFP_KERNEL);

	if (!state)
		return ERR_PTR(-ENOMEM);

	adreno_gpu_state_get(gpu, state);

	state->rbbm_status = gpu_read(gpu, REG_A7XX_RBBM_STATUS);

	return state;
}

static uint32_t a7xx_get_rptr(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a7xx_gpu *a7xx_gpu = to_a7xx_gpu(adreno_gpu);

	return a7xx_gpu->shadow[ring->id];
}

static const struct adreno_gpu_funcs funcs = {
	.base = {
		.get_param = adreno_get_param,
		.set_param = adreno_set_param,
		.hw_init = a7xx_hw_init,
		.pm_suspend = msm_gpu_pm_suspend,
		.pm_resume = msm_gpu_pm_resume,
		.recover = a7xx_recover,
		.submit = a7xx_submit,
		.active_ring = adreno_active_ring,
		.irq = a7xx_irq,
		.destroy = a7xx_destroy,
#if defined(CONFIG_DEBUG_FS) || defined(CONFIG_DEV_COREDUMP)
		.show = adreno_show,
#endif
		.gpu_state_get = a7xx_gpu_state_get,
		.gpu_state_put = adreno_gpu_state_put,
		.create_address_space = adreno_create_address_space,
		.get_rptr = a7xx_get_rptr,
	},
	.get_timestamp = a7xx_get_timestamp,
};

struct msm_gpu *a7xx_gpu_init(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct platform_device *pdev = priv->gpu_pdev;
	struct a7xx_gpu *a7xx_gpu;
	struct adreno_gpu *adreno_gpu;
	int ret;

	a7xx_gpu = kzalloc(sizeof(*a7xx_gpu), GFP_KERNEL);
	if (!a7xx_gpu)
		return ERR_PTR(-ENOMEM);

	adreno_gpu = &a7xx_gpu->base;
	adreno_gpu->registers = NULL;
	adreno_gpu->base.hw_apriv = true;

	ret = adreno_gpu_init(dev, pdev, adreno_gpu, &funcs, 1);
	if (ret) {
		a7xx_destroy(&(a7xx_gpu->base.base));
		return ERR_PTR(ret);
	}

	return &adreno_gpu->base;
}
