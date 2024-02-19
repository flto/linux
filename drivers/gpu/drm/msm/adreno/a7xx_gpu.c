// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2017-2022 The Linux Foundation. All rights reserved. */

#include "msm_gem.h"
#include "msm_mmu.h"
#include "msm_gpu_trace.h"
#include "a7xx_gpu.h"
#include "msm_kgsl.h"

#include <linux/bitfield.h>

extern bool hang_debug;

// missing regs
#define REG_A7XX_RB_CMP_DBG_ECO_CNTL            0x8e28
#define REG_A7XX_TPL1_DBG_ECO_CNTL1             0xb602

#define GPU_PAS_ID 13

/* Used to point CP to the SMMU record during preemption */
#define SET_PSEUDO_SMMU_INFO 0
/* Used to inform CP where to save preemption data at the time of switch out */
#define SET_PSEUDO_PRIV_NON_SECURE_SAVE_ADDR 1
/* Used to inform CP where to save secure preemption data at the time of switch out */
#define SET_PSEUDO_PRIV_SECURE_SAVE_ADDR 2
/* Used to inform CP where to save per context non-secure data at the time of switch out */
#define SET_PSEUDO_NON_PRIV_SAVE_ADDR 3
/* Used to inform CP where to save preemption counter data at the time of switch out */
#define SET_PSEUDO_COUNTER 4

#define OUT_RING64(ring, val) OUT_RING(ring, lower_32_bits(val)); OUT_RING(ring, upper_32_bits(val))


/* preemption related structs/defines from downstream: */

/**
 * struct gen7_cp_preemption_record - CP context record for
 * preemption.
 * @magic: (00) Value at this offset must be equal to
 * GEN7_CP_CTXRECORD_MAGIC_REF.
 * @info: (04) Type of record. Written non-zero (usually) by CP.
 * we must set to zero for all ringbuffers.
 * @errno: (08) Error code. Initialize this to GEN7_CP_CTXRECORD_ERROR_NONE.
 * CP will update to another value if a preemption error occurs.
 * @data: (12) DATA field in YIELD and SET_MARKER packets.
 * Written by CP when switching out. Not used on switch-in. Initialized to 0.
 * @cntl: (16) RB_CNTL, saved and restored by CP. We must initialize this.
 * @rptr: (20) RB_RPTR, saved and restored by CP. We must initialize this.
 * @wptr: (24) RB_WPTR, saved and restored by CP. We must initialize this.
 * @_pad28: (28) Reserved/padding.
 * @rptr_addr: (32) RB_RPTR_ADDR_LO|HI saved and restored. We must initialize.
 * rbase: (40) RB_BASE_LO|HI saved and restored.
 * counter: (48) Pointer to preemption counter.
 * @bv_rptr_addr: (56) BV_RB_RPTR_ADDR_LO|HI save and restored. We must initialize.
 */
struct gen7_cp_preemption_record {
	u32 magic;
	u32 info;
	u32 errno;
	u32 data;
	u32 cntl;
	u32 rptr;
	u32 wptr;
	u32 _pad28;
	u64 rptr_addr;
	u64 rbase;
	u64 counter;
	u64 bv_rptr_addr;
};

/**
 * struct gen7_cp_smmu_info - CP preemption SMMU info.
 * @magic: (00) The value at this offset must be equal to
 * GEN7_CP_SMMU_INFO_MAGIC_REF
 * @_pad4: (04) Reserved/padding
 * @ttbr0: (08) Base address of the page table for the * incoming context
 * @asid: (16) Address Space IDentifier (ASID) of the incoming context
 * @context_idr: (20) Context Identification Register value
 * @context_bank: (24) Which Context Bank in SMMU to update
 */
struct gen7_cp_smmu_info {
	u32 magic;
	u32 _pad4;
	u64 ttbr0;
	u32 asid;
	u32 context_idr;
	u32 context_bank;
};

#define GEN7_CP_SMMU_INFO_MAGIC_REF		0x241350d5UL

#define GEN7_CP_CTXRECORD_MAGIC_REF		0xae399d6eUL
/* Size of each CP preemption record */
#define GEN7_CP_CTXRECORD_SIZE_IN_BYTES		(3572 * SZ_1K) // XXX: this is the size for a750 only, see adreno-gpulist.h (ctxt_record_size value)
/* Size of the user context record block (in bytes) */
#define GEN7_CP_CTXRECORD_USER_RESTORE_SIZE	(192 * 1024)
/* Size of the performance counter save/restore block (in bytes) */
#define GEN7_CP_PERFCOUNTER_SAVE_RESTORE_SIZE	(4 * 1024)


// layout of preempt BO
#define PREEMPT_OFFSET_PRIV_NON_SECURE 0
//#define PREEMPT_OFFSET_PRIV_SECURE (PREEMPT_OFFSET_PRIV_NON_SECURE + GEN7_CP_CTXRECORD_SIZE_IN_BYTES)
#define PREEMPT_OFFSET_NON_PRIV (PREEMPT_OFFSET_PRIV_NON_SECURE + GEN7_CP_CTXRECORD_SIZE_IN_BYTES)
#define PREEMPT_OFFSET_COUNTER (PREEMPT_OFFSET_NON_PRIV + GEN7_CP_CTXRECORD_USER_RESTORE_SIZE)
#define PREEMPT_OFFSET_SMMU_INFO (PREEMPT_OFFSET_COUNTER + GEN7_CP_PERFCOUNTER_SAVE_RESTORE_SIZE)
#define PREEMPT_SIZE (PREEMPT_OFFSET_SMMU_INFO + 4096)
// note: for the first ring BO, the pwrup_reglist follows (4k size)

static const u32 gen7_pwrup_reglist[] = { // note: pre-a740/a750 has a different list
	REG_A7XX_UCHE_TRAP_BASE,
	REG_A7XX_UCHE_TRAP_BASE+1,
	REG_A7XX_UCHE_WRITE_THRU_BASE,
	REG_A7XX_UCHE_WRITE_THRU_BASE+1,
	REG_A7XX_UCHE_GMEM_RANGE_MIN,
	REG_A7XX_UCHE_GMEM_RANGE_MIN+1,
	REG_A7XX_UCHE_GMEM_RANGE_MAX,
	REG_A7XX_UCHE_GMEM_RANGE_MAX+1,
	REG_A7XX_UCHE_CACHE_WAYS,
	REG_A7XX_UCHE_MODE_CNTL,
	REG_A7XX_RB_NC_MODE_CNTL,
	REG_A7XX_RB_CMP_DBG_ECO_CNTL,
	REG_A7XX_GRAS_NC_MODE_CNTL,
	REG_A7XX_RB_CONTEXT_SWITCH_GMEM_SAVE_RESTORE,
	REG_A7XX_UCHE_GBIF_GX_CONFIG,
	REG_A7XX_UCHE_CLIENT_PF,
	REG_A7XX_TPL1_DBG_ECO_CNTL1,
};

#define CP_RESET_CONTEXT_STATE 0x1F
#define CP_RESET_GLOBAL_LOCAL_TS BIT(3)
#define CP_CLEAR_BV_BR_COUNTER BIT(2)
#define CP_CLEAR_RESOURCE_TABLE BIT(1)
#define CP_CLEAR_ON_CHIP_TS BIT(0)

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

/* note: only call with preempt_lock held */
static void a7xx_preempt(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a7xx_gpu *a7xx_gpu = to_a7xx_gpu(adreno_gpu);
	struct gen7_cp_smmu_info *smmu_info = ring->preempt_ptr + PREEMPT_OFFSET_SMMU_INFO;

	/* need to manually save the ttbr0 value and write to preemption smmu info */
	smmu_info->ttbr0 = ring->memptrs->ttbr0;

	gpu_write64(gpu, REG_A7XX_CP_CONTEXT_SWITCH_SMMU_INFO,
		ring->preempt_iova + PREEMPT_OFFSET_SMMU_INFO);
	gpu_write64(gpu, REG_A7XX_CP_CONTEXT_SWITCH_PRIV_NON_SECURE_RESTORE_ADDR,
		ring->preempt_iova + PREEMPT_OFFSET_PRIV_NON_SECURE);
	gpu_write64(gpu, REG_A7XX_CP_CONTEXT_SWITCH_PRIV_SECURE_RESTORE_ADDR, 0);
	gpu_write64(gpu, REG_A7XX_CP_CONTEXT_SWITCH_NON_PRIV_RESTORE_ADDR,
		ring->preempt_iova + PREEMPT_OFFSET_NON_PRIV);

	gpu_write(gpu, REG_A7XX_CP_CONTEXT_SWITCH_CNTL,
		(((1 << 6) & 0xC0) | // preempt_level (0, 1, 2)
		((0 << 9) & 0x200) | // skipsaverestore: To skip saverestore during L1 preemption (for 6XX)
		((1 << 8) & 0x100) | 0x1)); // usesgmem: enable GMEM save/restore across preemption (for 6XX)

	a7xx_gpu->preempting = 1;
}

static void a7xx_flush(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a7xx_gpu *a7xx_gpu = to_a7xx_gpu(adreno_gpu);
	uint32_t wptr;
	unsigned long flags;

	spin_lock_irqsave(&a7xx_gpu->preempt_lock, flags);

	/* in-progress preemption: don't touch anything until preemption finishes */
	if (a7xx_gpu->preempting) {
		spin_unlock_irqrestore(&a7xx_gpu->preempt_lock, flags);
		return;
	}

	if (a7xx_gpu->cur_ring == ring) {
		/* no ring change: just update WPTR */

		/* Copy the shadow to the actual register */
		ring->cur = ring->next;

		/*
		* Mask wptr value that we calculate to fit in the HW range. This is
		* to account for the possibility that the last command fit exactly into
		* the ringbuffer and rb->next hasn't wrapped to zero yet
		*/
		wptr = get_wptr(ring);

		/* ensure writes to ringbuffer have hit system memory: */
		mb();

		gpu_write(gpu, REG_A7XX_CP_RB_WPTR, wptr);
	} else if (ring == gpu->rb[1]) {
		/* to high priority ring: preempt now */
		a7xx_preempt(gpu, ring);
	} else {
		/* nothing to do: will switch to low priority ring when a high priority submit finishes */
	}

	spin_unlock_irqrestore(&a7xx_gpu->preempt_lock, flags);
}

static void a7xx_set_pagetable(struct msm_gpu *gpu,
		struct msm_ringbuffer *ring, struct msm_file_private *ctx)
{
	phys_addr_t ttbr;
	u32 asid;
	u64 memptr = rbmemptr(ring, ttbr0);

	// this check is buggy? (needs to be per ring instead of global?)
	//if (ctx->seqno == gpu->cur_ctx_seqno)
	//	return;

	if (msm_iommu_pagetable_params(ctx->aspace->mmu, &ttbr, &asid))
		return;

	/*
	 * Enable/disable concurrent binning for pagetable switch and
	 * set the thread to BR since only BR can execute the pagetable
	 * switch packets.
	 */
	/* Sync both threads and enable BR only */
	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, CP_THREAD_CONTROL_0_SYNC_THREADS | CP_SET_THREAD_BR);

	/* CP switches the pagetable and flushes the Caches */
	OUT_PKT7(ring, CP_SMMU_TABLE_UPDATE, 3);
	OUT_RING(ring, CP_SMMU_TABLE_UPDATE_0_TTBR0_LO(lower_32_bits(ttbr)));
	OUT_RING(ring,
		CP_SMMU_TABLE_UPDATE_1_TTBR0_HI(upper_32_bits(ttbr)) |
		CP_SMMU_TABLE_UPDATE_1_ASID(asid));
	OUT_RING(ring, CP_SMMU_TABLE_UPDATE_2_CONTEXTIDR(0));

	/*
	 * Sync both threads after switching pagetables and enable BR only
	 * to make sure BV doesn't race ahead while BR is still switching
	 * pagetables.
	 */
	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, CP_THREAD_CONTROL_0_SYNC_THREADS | CP_SET_THREAD_BR);

	/*
	 * Write the new TTBR0 to the memstore. This is needed for preemption
	 */
	OUT_PKT7(ring, CP_MEM_WRITE, 4);
	OUT_RING(ring, CP_MEM_WRITE_0_ADDR_LO(lower_32_bits(memptr)));
	OUT_RING(ring, CP_MEM_WRITE_1_ADDR_HI(upper_32_bits(memptr)));
	OUT_RING(ring, lower_32_bits(ttbr));
	OUT_RING(ring, upper_32_bits(ttbr));
}

static void a7xx_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a7xx_gpu *a7xx_gpu = to_a7xx_gpu(adreno_gpu);
	struct msm_ringbuffer *ring = submit->ring;
	unsigned int i;
	u64 timestamp_iova;
	unsigned long flags;

	// note: per-ring lock used only to avoid race condition with a7xx_irq() adding commands
	spin_lock_irqsave(&ring->preempt_lock, flags);

	a7xx_set_pagetable(gpu, ring, submit->queue->ctx);

	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, CP_SET_THREAD_BOTH);

	OUT_PKT7(ring, CP_SET_MARKER, 1);
	OUT_RING(ring, 0x101); /* IFPC disable */
	// TODO: kgsl expects soptimestamp to be filled
#if 0
	cmds[index++] = cp_type7_packet(CP_MEM_WRITE, 3);
	cmds[index++] = lower_32_bits(CTXT_SOPTIMESTAMP(device,
				drawctxt));
	cmds[index++] = upper_32_bits(CTXT_SOPTIMESTAMP(device,
				drawctxt));
	cmds[index++] = timestamp;
#endif

	if (ring->id == 0) { // only preempt commands on low priority ring
		OUT_PKT7(ring, CP_SET_MARKER, 1);
		OUT_RING(ring, 0x00d); /* IB1LIST start (enables L1/L2 preemption) */
	}

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

	if (ring->id == 0) {
		OUT_PKT7(ring, CP_SET_MARKER, 1);
		OUT_RING(ring, 0x00e); /* IB1LIST end (disables L1/L2 preemption) */
	}
	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, CP_SET_THREAD_BR);

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

	/* write the ctx timestamp (for kgsl memstore) */
	timestamp_iova = a7xx_gpu->shadow_iova + KGSL_MEMSTORE_OFFSET(submit->queue->id, eoptimestamp);
	OUT_PKT7(ring, CP_EVENT_WRITE, 4);
	OUT_RING(ring, CACHE_CLEAN | BIT(31) | BIT(27));
	OUT_RING(ring, lower_32_bits(timestamp_iova));
	OUT_RING(ring, upper_32_bits(timestamp_iova));
	OUT_RING(ring, submit->timestamp);

	/* write the ringbuffer timestamp */
	OUT_PKT7(ring, CP_EVENT_WRITE, 4);
	OUT_RING(ring, CACHE_CLEAN | BIT(27));
	OUT_RING(ring, lower_32_bits(rbmemptr(ring, fence)));
	OUT_RING(ring, upper_32_bits(rbmemptr(ring, fence)));
	OUT_RING(ring, submit->seqno);

	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, CP_SET_THREAD_BOTH);

	OUT_PKT7(ring, CP_SET_MARKER, 1);
	OUT_RING(ring, 0x100); /* IFPC enable */

	/* L0 preemption point, always emit this as last command */
	OUT_PKT7(ring, CP_CONTEXT_SWITCH_YIELD, 4);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);
	OUT_RING(ring, 1); // this is copied into the record ->data field (if preempted on this packet)
	OUT_RING(ring, 0);

	trace_msm_gpu_submit_flush(submit, gpu_read64(gpu, REG_A7XX_CP_ALWAYS_ON_COUNTER));

	spin_unlock_irqrestore(&ring->preempt_lock, flags);

	a7xx_flush(gpu, ring);
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

const struct adreno_reglist a740_hwcg[] = {
	{ REG_A7XX_RBBM_CLOCK_CNTL_SP0, 0x02222222 },
	{ REG_A7XX_RBBM_CLOCK_CNTL2_SP0, 0x22022222 },
	{ REG_A7XX_RBBM_CLOCK_HYST_SP0, 0x003cf3cf },
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
	{ REG_A7XX_RBBM_CLOCK_CNTL2_UCHE, 0x00222222 },
	{ REG_A7XX_RBBM_CLOCK_HYST_UCHE, 0x00000444 },
	{ REG_A7XX_RBBM_CLOCK_DELAY_UCHE, 0x00000222 },
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
	{ REG_A7XX_RBBM_CLOCK_MODE_VFD, 0x00222222 },
	{ REG_A7XX_RBBM_CLOCK_MODE_BV_GPC, 0x00222222 },
	{ REG_A7XX_RBBM_CLOCK_MODE_BV_VFD, 0x00002222 },
	{ REG_A7XX_RBBM_CLOCK_HYST_TSE_RAS_RBBM, 0x00000000 },
	{ REG_A7XX_RBBM_CLOCK_HYST_GPC, 0x04104004 },
	{ REG_A7XX_RBBM_CLOCK_HYST_VFD, 0x00000000 },
	{ REG_A7XX_RBBM_CLOCK_DELAY_TSE_RAS_RBBM, 0x00000000 },
	{ REG_A7XX_RBBM_CLOCK_DELAY_GPC, 0x00000200 },
	{ REG_A7XX_RBBM_CLOCK_DELAY_VFD, 0x00000000 },
	{ REG_A7XX_RBBM_CLOCK_MODE_HLSQ, 0x00002222 },
	{ REG_A7XX_RBBM_CLOCK_DELAY_HLSQ, 0x00000000 },
	{ REG_A7XX_RBBM_CLOCK_HYST_HLSQ, 0x00000000 },
	{ REG_A7XX_RBBM_CLOCK_MODE_BV_LRZ, 0x55555552 },
	{ REG_A7XX_RBBM_CLOCK_HYST2_VFD, 0x00000000 },
	{ REG_A7XX_RBBM_CLOCK_MODE_CP, 0x00000222 },
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
#define REG_A7XX_RBBM_CLOCK_CNTL_GLOBAL 0x000ad
#define REG_A7XX_RBBM_CGC_GLOBAL_LOAD_CMD 0x0011e
#define REG_A7XX_RBBM_CGC_P2S_TRIG_CMD 0x0011f
#define REG_A7XX_RBBM_CGC_P2S_STATUS 0x00122
	if (!adreno_gpu->info->hwcg) {
		gpu_write(gpu, REG_A7XX_RBBM_CLOCK_CNTL_GLOBAL, 1);
		gpu_write(gpu, REG_A7XX_RBBM_CGC_GLOBAL_LOAD_CMD, state);

		if (state) {
			u32 retry = 3;
			gpu_write(gpu, REG_A7XX_RBBM_CGC_P2S_TRIG_CMD, 1);
			/* Poll for the TXDONE:BIT(0) status */
			do {
				/* Wait for small amount of time for TXDONE status*/
				udelay(1);
				val = gpu_read(gpu, REG_A7XX_RBBM_CGC_P2S_STATUS);
			} while (!(val & BIT(0)) && --retry);

			if (!(val & BIT(0))) {
				dev_err(&gpu->pdev->dev, "RBBM_CGC_P2S_STATUS:TXDONE Poll failed\n");
				return;
			}
			gpu_write(gpu, REG_A7XX_RBBM_CLOCK_CNTL_GLOBAL, 0);
		}
		return;
	}

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
/* note: a740 same protected regs as a730 */

static void a7xx_set_cp_protect(struct msm_gpu *gpu)
{
	const u32 *regs = a730_protect;
	unsigned i, count, count_max;

	regs = a730_protect;
	count = ARRAY_SIZE(a730_protect);
	count_max = 48;
	BUILD_BUG_ON(ARRAY_SIZE(a730_protect) > 48);

	// XXX a750 has new protect list

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
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a7xx_gpu *a7xx_gpu = to_a7xx_gpu(adreno_gpu);
	struct msm_ringbuffer *ring = gpu->rb[0];
	struct cpu_gpu_lock {
		u32 gpu_req;
		u32 cpu_req;
		u32 turn;
		u8 ifpc_list_len;
		u8 preemption_list_len;
		u16 dynamic_list_len;
		u64 regs[62];
	} *lock = ring->preempt_ptr + PREEMPT_SIZE;
	int i;

	/* reset reglist */
	lock->gpu_req = 0;
	lock->cpu_req = 0;
	lock->turn = 0;
	lock->ifpc_list_len = 0; // IFPC not enabled
	lock->preemption_list_len = ARRAY_SIZE(gen7_pwrup_reglist);
	lock->dynamic_list_len = 0;
	for (i = 0; i < ARRAY_SIZE(gen7_pwrup_reglist); i++)
		lock->regs[i] = gen7_pwrup_reglist[i] | (u64) gpu_read(gpu, gen7_pwrup_reglist[i]) << 32;

	/* reset preemption records to a known state */
	for (i = 0; i < gpu->nr_rings; i++) {
		struct msm_ringbuffer *ring = gpu->rb[i];
		struct gen7_cp_preemption_record *preempt = ring->preempt_ptr + PREEMPT_OFFSET_PRIV_NON_SECURE;
		struct gen7_cp_smmu_info *smmu_info = ring->preempt_ptr + PREEMPT_OFFSET_SMMU_INFO;

		memset(preempt, 0, GEN7_CP_CTXRECORD_SIZE_IN_BYTES);

		preempt->magic = GEN7_CP_CTXRECORD_MAGIC_REF;
		preempt->info = 0;
		preempt->errno = 0;
		preempt->data = 0;
		preempt->cntl = MSM_GPU_RB_CNTL_DEFAULT;
		preempt->rptr = get_wptr(ring);
		preempt->wptr = get_wptr(ring);
		preempt->rptr_addr = shadowptr(a7xx_gpu, ring);
		preempt->rbase = ring->iova;
		preempt->counter = 0;

		smmu_info->magic = GEN7_CP_SMMU_INFO_MAGIC_REF;
		smmu_info->ttbr0 = 0;
		smmu_info->asid = 0xdecafbad;
		smmu_info->context_idr = 0;
		smmu_info->context_bank = 0;
	}

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
	/* Register initialization list with spinlock */
	OUT_RING64(ring, ring->preempt_iova + PREEMPT_SIZE);
	OUT_RING(ring, BIT(31));

	a7xx_flush(gpu, ring);
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
	int ret, i;
	unsigned long flags;

	spin_lock_irqsave(&a7xx_gpu->preempt_lock, flags);

	gpu_write(gpu, REG_A7XX_RBBM_SW_RESET_CMD, 1);
	udelay(1000); // XXX: how to determine when reset is completed?

	a7xx_gpu->preempting = 0;
	spin_unlock_irqrestore(&a7xx_gpu->preempt_lock, flags);

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

	/*
	 * Some GPUs needs specific alignment for UCHE GMEM base address.
	 * Configure UCHE GMEM base based on GMEM size and align it accordingly.
	 * This needs to be done based on GMEM size to avoid overlap between
	 * RB and UCHE GMEM range.
	 */
	if (1) { // a740/a750
		gpu_write64(gpu, REG_A7XX_UCHE_GMEM_RANGE_MIN, SZ_16M);
		gpu_write64(gpu, REG_A7XX_UCHE_GMEM_RANGE_MAX, SZ_16M + 3 * SZ_1M - 1);
	}

	gpu_write(gpu, REG_A7XX_UCHE_CACHE_WAYS, 0x800000);
	gpu_write(gpu, REG_A7XX_UCHE_CMDQ_CONFIG, 6 << 16 | 6 << 12 | 9 << 8 | BIT(3) | BIT(2) | 2);

	/* Set the AHB default slave response to "ERROR" */
	gpu_write(gpu, REG_A7XX_CP_AHB_CNTL, 0x1);

	/* Turn on performance counters */
	// if (pwrup_lock->dynamic_list_len > 0)
	gpu_write(gpu, REG_A7XX_RBBM_PERFCTR_CNTL, 0x1);

	a7xx_set_ubwc_config(gpu);

	gpu_write(gpu, REG_A7XX_RBBM_INTERFACE_HANG_INT_CNTL, BIT(30) | 0xcfffff);
	gpu_write(gpu, REG_A7XX_UCHE_CLIENT_PF, BIT(7) | BIT(0));

	gpu_write(gpu, REG_A7XX_RB_CONTEXT_SWITCH_GMEM_SAVE_RESTORE, 1);

	// XXX: set BIT(11) in RB_CMP_DBG_ECO_CNTL (pre-a750)
	// a750 only: (0x00004000 is reset value)
	gpu_write(gpu, REG_A7XX_RB_CMP_DBG_ECO_CNTL, 0x00004000 | BIT(19));
	gpu_write(gpu, REG_A7XX_TPL1_DBG_ECO_CNTL1, 0xc0700);

	a7xx_set_cp_protect(gpu);

	/* TODO: Configure LLCC */

	gpu_write(gpu, REG_A7XX_CP_APRIV_CNTL, A7XX_BR_APRIV_DEFAULT);
	gpu_write(gpu, REG_A7XX_CP_BV_APRIV_CNTL, A7XX_APRIV_DEFAULT);
	gpu_write(gpu, REG_A7XX_CP_LPAC_APRIV_CNTL, A7XX_APRIV_DEFAULT);

	// pre-a750: set CHICKEN_DBG BIT(0) (for all 3 CP)

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
	gpu->rb[1]->memptrs->bv_fence = gpu->rb[1]->fctx->completed_fence;

	ret = a7xx_ucode_init(gpu);
	if (ret)
		return ret;

	/* Set the ringbuffer address and setup rptr shadow */
	gpu_write64(gpu, REG_A7XX_CP_RB_BASE, gpu->rb[0]->iova);

	gpu_write(gpu, REG_A7XX_CP_RB_CNTL, MSM_GPU_RB_CNTL_DEFAULT);

	if (!a7xx_gpu->shadow_bo) {
		a7xx_gpu->memstore = msm_gem_kernel_new(gpu->dev,
			KGSL_MEMSTORE_SIZE, // sizeof(u32) * gpu->nr_rings for shadow
			MSM_BO_WC | MSM_BO_MAP_PRIV,
			gpu->aspace, &a7xx_gpu->shadow_bo,
			&a7xx_gpu->shadow_iova);

		if (IS_ERR(a7xx_gpu->memstore))
			return PTR_ERR(a7xx_gpu->memstore);

		msm_gem_object_set_name(a7xx_gpu->shadow_bo, "shadow");
		a7xx_gpu->shadow = (void*) a7xx_gpu->memstore + KGLS_MEMSTORE_CTX_SIZE; // shadow after memstore
	}

	/* reset ringbuffer state */
	if (gpu_read(gpu, REG_A7XX_CP_RB_RPTR) != 0)
		printk("RPTR is not zero\n");
	a7xx_gpu->shadow[0] = 0;
	gpu->rb[0]->cur = gpu->rb[0]->next = gpu->rb[0]->start;
	gpu->rb[0]->memptrs->fence = gpu->rb[0]->fctx->last_fence;

	a7xx_gpu->shadow[1] = 0;
	gpu->rb[1]->cur = gpu->rb[1]->next = gpu->rb[1]->start;
	gpu->rb[1]->memptrs->fence = gpu->rb[1]->fctx->last_fence;

	gpu_write64(gpu, REG_A7XX_CP_RB_RPTR_ADDR, shadowptr(a7xx_gpu, gpu->rb[0]));

	a7xx_gpu->cur_ring = gpu->rb[0];
	a7xx_gpu->preempting = 0;

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
		// note: hitting this path would also be a problem for this preemption implementation (check this?)
		OUT_PKT7(gpu->rb[0], CP_SET_SECURE_MODE, 1);
		OUT_RING(gpu->rb[0], 0x00000000);

		a7xx_flush(gpu, gpu->rb[0]);
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

	// set preemption pointers for each ring
	for (i = 0; i < 2; i++) {
		struct msm_ringbuffer *ring = gpu->rb[i];
		OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
		OUT_RING(ring, CP_SET_THREAD_BR);
		OUT_PKT7(ring, CP_SET_PSEUDO_REG, 15);
		OUT_RING(ring, SET_PSEUDO_SMMU_INFO);
		OUT_RING64(ring, ring->preempt_iova + PREEMPT_OFFSET_SMMU_INFO);
		OUT_RING(ring, SET_PSEUDO_PRIV_NON_SECURE_SAVE_ADDR);
		OUT_RING64(ring, ring->preempt_iova + PREEMPT_OFFSET_PRIV_NON_SECURE);
		OUT_RING(ring, SET_PSEUDO_PRIV_SECURE_SAVE_ADDR);
		OUT_RING64(ring, 0);
		OUT_RING(ring, SET_PSEUDO_NON_PRIV_SAVE_ADDR);
		OUT_RING64(ring, ring->preempt_iova + PREEMPT_OFFSET_NON_PRIV);
		OUT_RING(ring, SET_PSEUDO_COUNTER);
		OUT_RING64(ring, ring->preempt_iova + PREEMPT_OFFSET_COUNTER);
		OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
		OUT_RING(ring, CP_SET_THREAD_BOTH | CP_THREAD_CONTROL_0_SYNC_THREADS);

		// make ring0 preemptible, flush commands on ring0
		if (i == 0) {
			OUT_PKT7(ring, CP_CONTEXT_SWITCH_YIELD, 4);
			OUT_RING(ring, 0);
			OUT_RING(ring, 0);
			OUT_RING(ring, 1);
			OUT_RING(ring, 0);

			a7xx_flush(gpu, ring);
		}
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
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a7xx_gpu *a7xx_gpu = to_a7xx_gpu(adreno_gpu);
	struct msm_drm_private *priv = gpu->dev->dev_private;
	u32 status = gpu_read(gpu, REG_A7XX_RBBM_INT_0_STATUS);
	unsigned long flags, flags2;

	gpu_write(gpu, REG_A7XX_RBBM_INT_CLEAR_CMD, status);

	if (status & A7XX_RBBM_INT_0_MASK_SWINTERRUPT) {
		u32 ctx_switch;

		WARN_ON(!a7xx_gpu->preempting);

		spin_lock_irqsave(&a7xx_gpu->preempt_lock, flags);
		ctx_switch = gpu_read(gpu, REG_A7XX_CP_CONTEXT_SWITCH_CNTL);
		if ((ctx_switch & 1) == 0 && a7xx_gpu->preempting) { // preemption finished
			u32 wptr;

			a7xx_gpu->preempting = 0;

			// yield packets can only be preempted from once,
			// so just add another every time to be ablle to keep
			// preempting even if there wasn't a new submit
			{
				struct msm_ringbuffer *ring = a7xx_gpu->cur_ring;
				spin_lock_irqsave(&ring->preempt_lock, flags2);
				OUT_PKT7(ring, CP_CONTEXT_SWITCH_YIELD, 4);
				OUT_RING(ring, 0);
				OUT_RING(ring, 0);
				OUT_RING(ring, 1);
				OUT_RING(ring, 0);
				spin_unlock_irqrestore(&ring->preempt_lock, flags2);
			}

			/* only two rings, always switching to the other one */
			a7xx_gpu->cur_ring = gpu->rb[(a7xx_gpu->cur_ring == gpu->rb[0])];

			// flush wptr for the ring we just switched to
			{
				struct msm_ringbuffer *ring = a7xx_gpu->cur_ring;
				spin_lock_irqsave(&ring->preempt_lock, flags2);
				ring->cur = ring->next;
				wptr = get_wptr(ring);
				spin_unlock_irqrestore(&ring->preempt_lock, flags2);
			}
			gpu_write(gpu, REG_A7XX_CP_RB_WPTR, wptr);

			/* if there is pending work on the higher priority ring, switch back now */
			if (a7xx_gpu->cur_ring == gpu->rb[0]) {
				struct msm_ringbuffer *ring = gpu->rb[1];
				struct gen7_cp_preemption_record *preempt = ring->preempt_ptr + PREEMPT_OFFSET_PRIV_NON_SECURE;
				spin_lock_irqsave(&ring->preempt_lock, flags2);
				wptr = get_wptr(ring);
				spin_unlock_irqrestore(&ring->preempt_lock, flags2);
				if (wptr != preempt->rptr) {
					// print something because dont want this to happen
					printk("switched back to high priority ring immediately\n");
					a7xx_preempt(gpu, gpu->rb[1]);
				}
			}

			/* if switched to high priority ring, queue preemption back to low priority now
			 * note: this only works acceptably if there is only one high priority submit at a time
			 */
			if (a7xx_gpu->cur_ring == gpu->rb[1])
				a7xx_preempt(gpu, gpu->rb[0]);
		}
		spin_unlock_irqrestore(&a7xx_gpu->preempt_lock, flags);
	}

	if (priv->disable_err_irq)
		status &= A7XX_RBBM_INT_0_MASK_CACHE_CLEAN_TS;

	/* TODO: print human friendly strings for each error ? */
	if (status & ~(A7XX_RBBM_INT_0_MASK_CACHE_CLEAN_TS|A7XX_RBBM_INT_0_MASK_SWINTERRUPT))
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

static struct msm_gem_address_space *
a7xx_create_private_address_space(struct msm_gpu *gpu)
{
	struct msm_mmu *mmu;

	mmu = msm_iommu_pagetable_create(gpu->aspace->mmu);

	if (IS_ERR(mmu))
		return ERR_CAST(mmu);

	return msm_gem_address_space_create(mmu,
		"gpu", 0x100000000ULL,
		adreno_private_address_space_size(gpu));
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
		.create_private_address_space = a7xx_create_private_address_space,
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
	struct msm_gpu *gpu;
	int ret, i;

	a7xx_gpu = kzalloc(sizeof(*a7xx_gpu), GFP_KERNEL);
	if (!a7xx_gpu)
		return ERR_PTR(-ENOMEM);

	adreno_gpu = &a7xx_gpu->base;
	gpu = &adreno_gpu->base;
	adreno_gpu->registers = NULL;
	adreno_gpu->base.hw_apriv = true;

	ret = adreno_gpu_init(dev, pdev, adreno_gpu, &funcs, 2);
	if (ret) {
		a7xx_destroy(&(a7xx_gpu->base.base));
		return ERR_PTR(ret);
	}


	/* allocate preemption BOs for each ring */
	for (i = 0; i < gpu->nr_rings; i++) {
		struct msm_ringbuffer *ring = gpu->rb[i];
		ring->preempt_ptr = msm_gem_kernel_new(gpu->dev, PREEMPT_SIZE + (i == 0 ? 4096 : 0),
			MSM_BO_WC | MSM_BO_MAP_PRIV, gpu->aspace, &ring->preempt_bo, &ring->preempt_iova);
		if (IS_ERR(ring->preempt_ptr))
			return ring->preempt_ptr;
		// note: missing matching cleanup code on driver unload
	}
	spin_lock_init(&a7xx_gpu->preempt_lock);

	return &adreno_gpu->base;
}

uint64_t a7xx_gpu_shadow_iova(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a7xx_gpu *a7xx_gpu = to_a7xx_gpu(adreno_gpu);
	return a7xx_gpu->shadow_iova;
}

struct drm_gem_object *a7xx_gpu_shadow_bo(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a7xx_gpu *a7xx_gpu = to_a7xx_gpu(adreno_gpu);
	return a7xx_gpu->shadow_bo;
}

void a7xx_reset_memstore(struct msm_gpu *gpu, uint32_t ctx_id)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a7xx_gpu *a7xx_gpu = to_a7xx_gpu(adreno_gpu);
	uint32_t *eoptimestamp = a7xx_gpu->memstore + KGSL_MEMSTORE_OFFSET(ctx_id, eoptimestamp);
	*eoptimestamp = 0;
}
