// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 The Linux Foundation. All rights reserved.
 */

#include <linux/kref.h>
#include <linux/uaccess.h>

#include "msm_gpu.h"

int msm_file_private_set_sysprof(struct msm_file_private *ctx,
				 struct msm_gpu *gpu, int sysprof)
{
	/*
	 * Since pm_runtime and sysprof_active are both refcounts, we
	 * call apply the new value first, and then unwind the previous
	 * value
	 */

	switch (sysprof) {
	default:
		return -EINVAL;
	case 2:
		pm_runtime_get_sync(&gpu->pdev->dev);
		fallthrough;
	case 1:
		refcount_inc(&gpu->sysprof_active);
		fallthrough;
	case 0:
		break;
	}

	/* unwind old value: */
	switch (ctx->sysprof) {
	case 2:
		pm_runtime_put_autosuspend(&gpu->pdev->dev);
		fallthrough;
	case 1:
		refcount_dec(&gpu->sysprof_active);
		fallthrough;
	case 0:
		break;
	}

	ctx->sysprof = sysprof;

	return 0;
}

void __msm_file_private_destroy(struct kref *kref)
{
	struct msm_file_private *ctx = container_of(kref,
		struct msm_file_private, ref);

	msm_gem_address_space_put(ctx->aspace);
	kfree(ctx->comm);
	kfree(ctx->cmdline);
	kfree(ctx);
}

void msm_submitqueue_destroy(struct kref *kref)
{
	struct msm_gpu_submitqueue *queue = container_of(kref,
		struct msm_gpu_submitqueue, ref);
	struct kgsl_fence *fence, *tmp;

	list_for_each_entry_safe(fence, tmp, &queue->fences, node) {
		dma_fence_signal(&fence->base);
		dma_fence_put(&fence->base);
	}

	idr_destroy(&queue->fence_idr);

	msm_file_private_put(queue->ctx);

	spin_lock(&queue->gpu->id_lock);
	queue->gpu->id_map[queue->id >> 6] &= ~(1ull << (~queue->id&63));
	spin_unlock(&queue->gpu->id_lock);

	kfree(queue);
}

int wait_fence(struct msm_gpu_submitqueue *queue, uint32_t fence_id, signed long timeout);

static void msm_submitqueue_wait_idle(struct msm_gpu_submitqueue *queue)
{
	int ret;
	long timeout;

	/* wait for any submits to finish, to block until GEM objects are unused */
	do {
		ret = wait_fence(queue, queue->last_fence_submitted, 5 * HZ);
	} while (ret == -ERESTARTSYS);
	if (ret)
		printk("msm_submitqueue_wait_idle timed out %d\n", ret);

	do {
		timeout = wait_event_interruptible_timeout(queue->waitqueue,
				queue->timestamp_retired >= queue->timestamp,
				5 * HZ);
	} while (timeout == -ERESTARTSYS);
	WARN_ON(timeout == 0);
}

void msm_submitqueue_retire_timestamp(struct msm_gpu_submitqueue *queue, u32 timestamp)
{
	struct kgsl_fence *fence, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&queue->fence_lock, flags);
	list_for_each_entry_safe(fence, tmp, &queue->fences, node) {
		if (timestamp < fence->timestamp)
			continue;
		dma_fence_signal(&fence->base);
		dma_fence_put(&fence->base);
		list_del(&fence->node);
	}
	WARN_ON(timestamp < queue->timestamp_retired);
	queue->timestamp_retired = timestamp;
	spin_unlock_irqrestore(&queue->fence_lock, flags);

	wmb(); /* make sure timestamp_retired write goes through */
	wake_up_interruptible(&queue->waitqueue);
}

struct msm_gpu_submitqueue *msm_submitqueue_get(struct msm_file_private *ctx,
		u32 id)
{
	struct msm_gpu_submitqueue *entry = NULL;
	int i;

	if (!ctx)
		return NULL;

	read_lock(&ctx->queuelock);

	for (i = 0; i < ARRAY_SIZE(ctx->queue); i++) {
		if (ctx->queue[i] && ctx->queue[i]->id == id) {
			entry = ctx->queue[i];
			break;
		}
	}

	if (entry) {
		kref_get(&entry->ref);
		read_unlock(&ctx->queuelock);

		return entry;
	}

	read_unlock(&ctx->queuelock);
	return NULL;
}

void msm_submitqueue_close(struct msm_file_private *ctx)
{
	struct msm_gpu_submitqueue *entry;
	u32 i;

	if (!ctx)
		return;

	/* cancel all jobs from this context that aren't yet submitted to HW */
	for (i = 0; i < ARRAY_SIZE(ctx->entities); i++) {
		if (!ctx->entities[i])
			continue;

		drm_sched_entity_destroy(ctx->entities[i]);
		kfree(ctx->entities[i]);
	}

	/*
	 * No lock needed in close and there won't
	 * be any more user ioctls coming our way
	 */
	for (i = 0; i < ARRAY_SIZE(ctx->queue); i++) {
		entry = ctx->queue[i];
		ctx->queue[i] = NULL;
		if (!entry)
			continue;

		/* wait outside of destroy so it doesn't get deferred to submit retire */
		msm_submitqueue_wait_idle(entry);
		msm_submitqueue_put(entry);
	}
}

static struct drm_sched_entity *
get_sched_entity(struct msm_file_private *ctx, struct msm_ringbuffer *ring,
		 unsigned ring_nr, enum drm_sched_priority sched_prio)
{
	static DEFINE_MUTEX(entity_lock);
	unsigned idx = (ring_nr * NR_SCHED_PRIORITIES) + sched_prio;

	/* We should have already validated that the requested priority is
	 * valid by the time we get here.
	 */
	if (WARN_ON(idx >= ARRAY_SIZE(ctx->entities)))
		return ERR_PTR(-EINVAL);

	mutex_lock(&entity_lock);

	if (!ctx->entities[idx]) {
		struct drm_sched_entity *entity;
		struct drm_gpu_scheduler *sched = &ring->sched;
		int ret;

		entity = kzalloc(sizeof(*ctx->entities[idx]), GFP_KERNEL);

		ret = drm_sched_entity_init(entity, sched_prio, &sched, 1, NULL);
		if (ret) {
			mutex_unlock(&entity_lock);
			kfree(entity);
			return ERR_PTR(ret);
		}

		ctx->entities[idx] = entity;
	}

	mutex_unlock(&entity_lock);

	return ctx->entities[idx];
}

void a7xx_reset_memstore(struct msm_gpu *gpu, uint32_t ctx_id);

int msm_submitqueue_create(struct drm_device *drm, struct msm_file_private *ctx,
		u32 prio, u32 flags, u32 *id)
{
	struct msm_drm_private *priv = drm->dev_private;
	struct msm_gpu *gpu = priv->gpu;
	struct msm_gpu_submitqueue *queue;
	enum drm_sched_priority sched_prio;
	unsigned ring_nr, i;
	int ret;

	if (!ctx)
		return -ENODEV;

	if (!priv->gpu)
		return -ENODEV;

	ret = msm_gpu_convert_priority(priv->gpu, prio, &ring_nr, &sched_prio);
	if (ret)
		return ret;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);

	if (!queue)
		return -ENOMEM;

	kref_init(&queue->ref);
	queue->flags = flags;
	queue->ring_nr = ring_nr;

	queue->entity = get_sched_entity(ctx, priv->gpu->rb[ring_nr],
					 ring_nr, sched_prio);
	if (IS_ERR(queue->entity)) {
		ret = PTR_ERR(queue->entity);
		kfree(queue);
		return ret;
	}

	write_lock(&ctx->queuelock);

	queue->ctx = msm_file_private_get(ctx);

	idr_init(&queue->fence_idr);
	spin_lock_init(&queue->idr_lock);
	mutex_init(&queue->lock);

	queue->timestamp = 0;
	queue->timestamp_retired = 0;
	spin_lock_init(&queue->fence_lock);
	INIT_LIST_HEAD(&queue->fences);
	init_waitqueue_head(&queue->waitqueue);

	for (i = 0; i < ARRAY_SIZE(ctx->queue); i++) {
		if (ctx->queue[i])
			continue;

		ctx->queue[i] = queue;
		queue->id = -1;
		queue->gpu = gpu;
		spin_lock(&gpu->id_lock);
		for (i = 0; i < ARRAY_SIZE(gpu->id_map); i++) {
			if (gpu->id_map[i] == ~0ull)
				continue;

			queue->id = __builtin_clzll(~gpu->id_map[i]);
			gpu->id_map[i] |= 1ull << (~queue->id & 63);
			queue->id += i*64;
			break;
		}
		spin_unlock(&gpu->id_lock);
		WARN_ON (queue->id == -1); // XXX

		a7xx_reset_memstore(gpu, queue->id);

		if (id)
			*id = queue->id;
		break;
	}
	WARN_ON(i == ARRAY_SIZE(ctx->queue)); // XXX

	write_unlock(&ctx->queuelock);

	return 0;
}

static int msm_submitqueue_query_faults(struct msm_gpu_submitqueue *queue,
		struct drm_msm_submitqueue_query *args)
{
	size_t size = min_t(size_t, args->len, sizeof(queue->faults));
	int ret;

	/* If a zero length was passed in, return the data size we expect */
	if (!args->len) {
		args->len = sizeof(queue->faults);
		return 0;
	}

	/* Set the length to the actual size of the data */
	args->len = size;

	ret = copy_to_user(u64_to_user_ptr(args->data), &queue->faults, size);

	return ret ? -EFAULT : 0;
}

int msm_submitqueue_query(struct drm_device *drm, struct msm_file_private *ctx,
		struct drm_msm_submitqueue_query *args)
{
	struct msm_gpu_submitqueue *queue;
	int ret = -EINVAL;

	if (args->pad)
		return -EINVAL;

	queue = msm_submitqueue_get(ctx, args->id);
	if (!queue)
		return -ENOENT;

	if (args->param == MSM_SUBMITQUEUE_PARAM_FAULTS)
		ret = msm_submitqueue_query_faults(queue, args);

	msm_submitqueue_put(queue);

	return ret;
}

int msm_submitqueue_remove(struct msm_file_private *ctx, u32 id)
{
	struct msm_gpu_submitqueue *entry = NULL;
	int i;

	if (!ctx)
		return 0;

	write_lock(&ctx->queuelock);

	for (i = 0; i < ARRAY_SIZE(ctx->queue); i++) {
		if (ctx->queue[i] && ctx->queue[i]->id == id) {
			entry = ctx->queue[i];
			ctx->queue[i] = NULL;
			break;
		}
	}

	if (entry) {
		write_unlock(&ctx->queuelock);

		msm_submitqueue_put(entry);
		return 0;
	}

	write_unlock(&ctx->queuelock);
	return -ENOENT;
}

