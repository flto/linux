/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2017-2022 The Linux Foundation. All rights reserved. */

#ifndef __A7XX_GPU_H__
#define __A7XX_GPU_H__

#include "adreno_gpu.h"
#include "a7xx.xml.h"

struct a7xx_gpu {
	struct adreno_gpu base;

	struct drm_gem_object *sqe_bo;
	uint64_t sqe_iova;

	struct drm_gem_object *shadow_bo;
	uint64_t shadow_iova;
	uint32_t *shadow;
	void *memstore;

	struct msm_ringbuffer *cur_ring;
	spinlock_t preempt_lock;
	bool preempting;
};

#define to_a7xx_gpu(x) container_of(x, struct a7xx_gpu, base)

#define KGSL_MEMSTORE_SIZE SZ_32K
#define KGLS_MEMSTORE_CTX_SIZE (40*768) // size reserved for per-context memstore

#define shadowptr(_a7xx_gpu, _ring) ((_a7xx_gpu)->shadow_iova + KGLS_MEMSTORE_CTX_SIZE + \
		((_ring)->id * sizeof(uint32_t)))

#endif /* __A7XX_GPU_H__ */
