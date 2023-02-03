#include <linux/file.h>
#include <linux/sync_file.h>
#include <linux/uaccess.h>

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_syncobj.h>

#include "msm_drv.h"
#include "msm_gpu.h"
#include "msm_gem.h"
#include "msm_gpu_trace.h"
#include "msm_kgsl.h"

// to create an already signalled fence (any better way to do this?)
struct dummy_fence {
	struct dma_fence base;
	spinlock_t spinlock;
};

static const char *dummy_fence_get_driver_name(struct dma_fence *fence)
{
	return "kgsl_dummy";
}

static const char *dummy_fence_get_timeline_name(struct dma_fence *fence)
{
	return "kgsl_dummy";
}

static const struct dma_fence_ops dummy_fence_ops = {
	.get_driver_name = dummy_fence_get_driver_name,
	.get_timeline_name = dummy_fence_get_timeline_name,
	// .signaled = dummy_fence_signaled, good idea to have this?
};

struct msm_gem_submit *submit_create(struct drm_device *dev,
		struct msm_gpu *gpu,
		struct msm_gpu_submitqueue *queue, uint32_t nr_cmds);

int wait_fence(struct msm_gpu_submitqueue *queue, uint32_t fence_id, signed long timeout);

uint64_t a7xx_gpu_shadow_iova(struct msm_gpu *gpu);
struct drm_gem_object *a7xx_gpu_shadow_bo(struct msm_gpu *gpu);

#define KGSL_MEMSTORE_TOKEN_ADDRESS 0xfffff000u
int kgsl_mmap(struct drm_file *file_priv, struct drm_device *dev, struct vm_area_struct *vma)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gpu *gpu = priv->gpu;
	struct drm_gem_object *obj;
	int ret;

	if ((vma->vm_pgoff << PAGE_SHIFT) == KGSL_MEMSTORE_TOKEN_ADDRESS)
		return drm_gem_mmap_obj(a7xx_gpu_shadow_bo(gpu), 4096, vma);

	obj = drm_gem_object_lookup(file_priv, vma->vm_pgoff);
	if (!obj)
		return -ENOENT;

	ret = drm_gem_mmap_obj(obj, obj->size, vma);
	drm_gem_object_put(obj);
	return ret;
}

long kgsl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct drm_file *file_priv = file->private_data;
	struct drm_device *dev = file_priv->minor->dev;
	struct msm_file_private *ctx = file_priv->driver_priv;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gpu *gpu = priv->gpu;
	struct msm_gpu_submitqueue *queue;
	struct drm_gem_object *obj;
	int ret;

	char __user *argp = (char __user *)arg;

	switch (cmd) {
	case IOCTL_KGSL_DRAWCTXT_CREATE: {
		struct kgsl_drawctxt_create create;

		if (copy_from_user(&create, argp, sizeof(create)))
			return -EFAULT;

		// XXX: look at create.flags
		ret = msm_submitqueue_create(dev, ctx, 0, 0, &create.drawctxt_id);

		if (copy_to_user(argp, &create, sizeof(create)))
			WARN_ON(1);
		return 0;
	}
	case IOCTL_KGSL_DRAWCTXT_DESTROY: {
		struct kgsl_drawctxt_destroy destroy;

		if (copy_from_user(&destroy, argp, sizeof(destroy)))
			return -EFAULT;

		return msm_submitqueue_remove(ctx, destroy.drawctxt_id);
	}
	case IOCTL_KGSL_DEVICE_GETPROPERTY: {
		struct kgsl_device_getproperty get_prop;

		if (copy_from_user(&get_prop, argp, sizeof(get_prop)))
			return -EFAULT;

		union {
			u32 val;
			u64 gmem_vaddr;
			struct kgsl_ucode_version ucode;
			struct kgsl_qtimer_prop qtimer;
			struct kgsl_qdss_stm_prop qdss;
			struct kgsl_shadowprop shadow;
			struct kgsl_gpu_model model;
		} prop = {};

		switch (get_prop.type) {
		case KGSL_PROP_DEVICE_INFO: {
			static const struct kgsl_devinfo prop = {
				.device_id = 1, /* KGSL_DEVICE_3D0 + 1 ? */
				.chip_id = 0x43050a01,
				.mmu_enabled = 1,
				.gmem_gpubaseaddr = 0,
				.gpu_id = 740,
				.gmem_sizebytes = SZ_1M * 3,
			};
			if (get_prop.sizebytes != sizeof(prop))
				return -EINVAL;
			if (copy_to_user(get_prop.value, &prop, sizeof(prop)))
				return -EFAULT;
		} return 0;
		case KGSL_PROP_DEVICE_SHADOW: {
			prop.shadow.gpuaddr = KGSL_MEMSTORE_TOKEN_ADDRESS; //a7xx_gpu_shadow_iova(gpu);
			prop.shadow.size = 4096;
			prop.shadow.flags = KGSL_FLAGS_INITIALIZED | KGSL_FLAGS_PER_CONTEXT_TIMESTAMPS;
		} break;
		case KGSL_PROP_UCHE_GMEM_VADDR: {
			prop.gmem_vaddr = 0;
		} break;
		case KGSL_PROP_UCODE_VERSION: {
			prop.ucode.pfp = 0x01740157; // XXX read from FW
			prop.ucode.pm4 = 0;
		} break;
		case KGSL_PROP_HIGHEST_BANK_BIT: { prop.val = 16; } break;
		case KGSL_PROP_DEVICE_BITNESS: { prop.val = 48; } break;
		case KGSL_PROP_DEVICE_QDSS_STM: {
		} break;
		case KGSL_PROP_MIN_ACCESS_LENGTH: { prop.val = 32; } break;
		case KGSL_PROP_UBWC_MODE: { prop.val = 4; } break;
		case KGSL_PROP_DEVICE_QTIMER: {
			prop.qtimer.gpuaddr = 0;
			prop.qtimer.size = 0;
		} break;
		case KGSL_PROP_SECURE_BUFFER_ALIGNMENT: { prop.val = SZ_4K; } break;
		case KGSL_PROP_SECURE_CTXT_SUPPORT: { prop.val = 0; } break;
		case KGSL_PROP_SPEED_BIN: { prop.val = 0; } break;
		case KGSL_PROP_GAMING_BIN: { prop.val = 0; } break;
		case KGSL_PROP_GPU_MODEL: { strcpy(prop.model.gpu_model, "Adreno740v2"); } break;
		case KGSL_PROP_VK_DEVICE_ID: { prop.val = 0x43050a01; } break;
		case KGSL_PROP_IS_LPAC_ENABLED: { prop.val = 0; } break;
		default:
			DRM_DEV_ERROR(dev->dev, "TODO: get prop %d\n", get_prop.type);
			return -ENODEV;
		}

		if (get_prop.sizebytes > sizeof(prop))
				return -EINVAL;
		if (copy_to_user(get_prop.value, &prop, get_prop.sizebytes))
				return -EFAULT;
		return 0;
	}
	case IOCTL_KGSL_TIMESTAMP_EVENT: {
		struct kgsl_timestamp_event event;
		struct dma_fence *fence;
		struct sync_file *sync_file;
		int out_fence_fd;

		if (copy_from_user(&event, argp, sizeof(event)))
			return -EFAULT;

		if (event.type != KGSL_TIMESTAMP_EVENT_FENCE || event.len != sizeof(int))
			return -EINVAL;

		queue = msm_submitqueue_get(ctx, event.context_id);
		if (!queue)
			return -EINVAL;


		spin_lock(&queue->idr_lock);
		fence = idr_find(&queue->fence_idr, event.timestamp);
		if (fence)
			fence = dma_fence_get_rcu(fence);
		spin_unlock(&queue->idr_lock);
		msm_submitqueue_put(queue);

		if (!fence) {
			/* nothing to wait for, create an already signalled dummy fence */
			fence = kzalloc(sizeof(struct dummy_fence), GFP_KERNEL);
			WARN_ON(!fence);

			dma_fence_init(fence, &dummy_fence_ops, &((struct dummy_fence*)fence)->spinlock,
				dma_fence_context_alloc(1), 0);
			dma_fence_signal(fence);
		}

		sync_file = sync_file_create(fence);
		dma_fence_put(fence);
		if (!sync_file)
			return -ENOMEM;

		out_fence_fd = get_unused_fd_flags(O_CLOEXEC);
		if (out_fence_fd < 0) {
			WARN_ON(1); // XXX destroy sync_file
			return out_fence_fd;
		}
		fd_install(out_fence_fd, sync_file->file);

		if (copy_to_user(event.priv, &out_fence_fd, sizeof(out_fence_fd))) {
			WARN_ON(1); // XXX close fd
			return -EFAULT;
		}
		return 0;
	}

case IOCTL_KGSL_GPUOBJ_ALLOC: {
	struct kgsl_gpuobj_alloc alloc;
	unsigned size;
	union {
		struct {
			u64 bits_0_2 : 3;
			u64 secure : 1;
			u64 bits_4_7 : 4;
			u64 mem_type : 8;
			u64 mem_align : 8;
			u64 gpu_read_only : 1;
			u64 gpu_write_only : 1;
			u64 cache_mode : 2;
			u64 use_cpu_map : 1;
			u64 sparse_phys : 1;
			u64 sparse_virt : 1;
			u64 iocoherent : 1;
			u64 force_32bit : 1;
			u64 guard_page : 1;
			u64 vbo : 1;
		};
		u64 bits;
	} flags;
	u32 msm_flags = MSM_BO_WC;

	if (copy_from_user(&alloc, argp, sizeof(alloc)))
		return -EFAULT;

	if (alloc.size >> 32)
		return -EINVAL;

	flags.bits = alloc.flags;
	// support only mem_type, mem_align, readonly, cache_mode, iocoherent
	if (flags.bits & ~0x8dffff00ull) {
		DRM_DEV_ERROR(dev->dev, "unexpected alloc flags: %llx\n", alloc.flags);
		return -EINVAL;
	}

	if (flags.mem_align != 12)
		dev_warn(dev->dev, "ignored unsupported mem_align: %d\n", flags.mem_align);

	if (flags.cache_mode == KGSL_CACHEMODE_WRITETHROUGH || flags.cache_mode == KGSL_CACHEMODE_WRITEBACK)
		msm_flags = flags.iocoherent ? MSM_BO_CACHED_COHERENT : MSM_BO_CACHED;

	if (msm_flags == MSM_BO_CACHED) {
		dev_warn(dev->dev, "forced iocoherent because cache sync isn't implemented\n");
		msm_flags = MSM_BO_CACHED_COHERENT;
	}

	if (flags.gpu_read_only)
		msm_flags |= MSM_BO_GPU_READONLY;

	// XXX overflow
	size = (alloc.size + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1); /* 4k aligned size */

	obj = msm_gem_new(dev, size, msm_flags);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	ret = drm_gem_handle_create(file_priv, obj, &alloc.id);
	drm_gem_object_put(obj);
	if (ret)
		return ret;

	alloc.va_len = size;
	alloc.mmapsize = size;

	if (copy_to_user(argp, &alloc, sizeof(alloc)))
		WARN_ON(1);

	return 0;
}

case IOCTL_KGSL_GPUOBJ_IMPORT: {
	struct kgsl_gpuobj_import import;
	struct kgsl_gpuobj_import_dma_buf dma_buf;

	if (copy_from_user(&import, argp, sizeof(import)))
		return -EFAULT;

	if (import.type != KGSL_USER_MEM_TYPE_DMABUF)
		return -ENOENT;

	if (copy_from_user(&dma_buf, import.priv, sizeof(dma_buf)))
		return -EFAULT;

	ret = drm_gem_prime_fd_to_handle(dev, file_priv, dma_buf.fd, &import.id);
	if (ret)
		return ret;

	if (copy_to_user(argp, &import, sizeof(import)))
		WARN_ON(1);
	return 0;
}

case IOCTL_KGSL_GPUOBJ_INFO: {
	struct kgsl_gpuobj_info info;

	if (copy_from_user(&info, argp, sizeof(info)))
		return -EFAULT;

	obj = drm_gem_object_lookup(file_priv, info.id);
	if (!obj)
		return -EINVAL;

	msm_gem_get_iova(obj, ctx->aspace, &info.gpuaddr);
	info.flags = 0; // XXX
	info.size = obj->size;
	info.va_len = obj->size;
	info.va_addr = msm_gem_mmap_offset(obj);

	drm_gem_object_put(obj);

	if (copy_to_user(argp, &info, sizeof(info)))
		return -EINVAL;
	return 0;
}

case IOCTL_KGSL_GPUOBJ_FREE: {
	struct kgsl_gpuobj_free free;

	if (copy_from_user(&free, argp, sizeof(free)))
		return -EFAULT;

	if (free.flags) {
		DRM_DEV_ERROR(dev->dev, "unexpected free flags: %llx\n", free.flags);
		return -EINVAL;
	}

	return drm_gem_handle_delete(file_priv, free.id);
}

case IOCTL_KGSL_GPU_COMMAND: {
	struct msm_gem_submit *submit;
	struct kgsl_gpu_command cmd;
	struct kgsl_command_object obj;
	unsigned i;

	if (copy_from_user(&cmd, argp, sizeof(cmd)))
		return -EFAULT;

	if (cmd.cmdsize != sizeof(obj))
		return -EINVAL;

	if (!access_ok((void __user*)cmd.cmdlist, cmd.numcmds * sizeof(obj)))
		return -EFAULT;

	if (cmd.numsyncs != 0) {
		DRM_DEV_ERROR(dev->dev, "TODO: numsyncs");
		return -EINVAL;
	}

	// XXX cmd.flags

	queue = msm_submitqueue_get(ctx, cmd.context_id);
	if (!queue)
		return -EINVAL;

	submit = submit_create(dev, gpu, queue, cmd.numcmds);
	if (IS_ERR(submit))
		return PTR_ERR(submit);

	mutex_lock(&queue->lock);

	submit->nr_cmds = cmd.numcmds;
	for (i = 0; i < cmd.numcmds; i++) {
		__copy_from_user(&obj, (void __user*)cmd.cmdlist + i * sizeof(obj), sizeof(obj));
		submit->cmd[i].type = MSM_SUBMIT_CMD_BUF;
		submit->cmd[i].size = obj.size >> 2;
		submit->cmd[i].iova = obj.gpuaddr;
	}

	spin_lock(&queue->idr_lock);
	drm_sched_job_arm(&submit->base);
	submit->user_fence = dma_fence_get(&submit->base.s_fence->finished);
	submit->fence_id = idr_alloc_cyclic(&queue->fence_idr, submit->user_fence, 1, INT_MAX, GFP_KERNEL);
	spin_unlock(&queue->idr_lock);

	WARN_ON(submit->fence_id < 0);

	msm_gem_submit_get(submit);
	drm_sched_entity_push_job(&submit->base);

	cmd.timestamp = submit->fence_id;
	queue->last_fence = submit->fence_id;

	mutex_unlock(&queue->lock);
	msm_gem_submit_put(submit);

	if (copy_to_user(argp, &cmd, sizeof(cmd)))
		WARN_ON(1);

	return 0;
}

case IOCTL_KGSL_PERFCOUNTER_GET: {
	struct kgsl_perfcounter_get get;

	if (copy_from_user(&get, argp, sizeof(get)))
		return -EFAULT;

	DRM_DEV_ERROR(dev->dev, "TODO: perfcounter get\n");
	return -EBUSY;
}

case IOCTL_KGSL_PERFCOUNTER_PUT: {
	struct kgsl_perfcounter_put put;

	if (copy_from_user(&put, argp, sizeof(put)))
		return -EFAULT;

	DRM_DEV_ERROR(dev->dev, "TODO: perfcounter put\n");
	return 0;
}
case IOCTL_KGSL_SYNCSOURCE_CREATE:
	DRM_DEV_ERROR(dev->dev, "TODO: syncsource create\n");
	return 0;
case IOCTL_KGSL_SYNCSOURCE_DESTROY:
	DRM_DEV_ERROR(dev->dev, "TODO: syncsource destroy\n");
	return 0;

case IOCTL_KGSL_GPUOBJ_SYNC:
	// TODO: cache sync for cached objects (worked around by always allocating coherent)
	// DRM_DEV_ERROR(dev->dev, "TODO: GPUOBJ_SYNC\n");
	return 0;

case IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID: {
	struct kgsl_device_waittimestamp_ctxtid wait;

	if (copy_from_user(&wait, argp, sizeof(wait)))
		return -EFAULT;

	queue = msm_submitqueue_get(ctx, wait.context_id);
	if (!queue)
		return -EINVAL;

	ret = wait_fence(queue, wait.timestamp, msecs_to_jiffies(wait.timeout));
	msm_submitqueue_put(queue);
	return ret;
}

default:
	DRM_DEV_ERROR(dev->dev, "TODO: ioctl %.8x\n", cmd);
	return 0;
	}
}
