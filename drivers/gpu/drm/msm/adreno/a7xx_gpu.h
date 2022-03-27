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
};

#define to_a7xx_gpu(x) container_of(x, struct a7xx_gpu, base)

#define shadowptr(_a7xx_gpu, _ring) ((_a7xx_gpu)->shadow_iova + \
		((_ring)->id * sizeof(uint32_t)))

#endif /* __A7XX_GPU_H__ */
