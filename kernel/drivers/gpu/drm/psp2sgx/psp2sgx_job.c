// SPDX-License-Identifier: GPL-2.0-only
/*
 * psp2sgx: jobs (submit ioctl).
 */
#include "psp2sgx_drv.h"
#include "psp2sgx_gem.h"
#include "psp2sgx_mk.h"

static int lookup_bos(struct drm_file *file, struct psp2sgx_job *job,
		      const struct drm_psp2sgx_submit *args,
		      struct drm_psp2sgx_submit_bo **out)
{
	struct drm_psp2sgx_submit_bo *sbos;
	u32 i;
	int err;

	if (!args->nr_bos)
		return 0;
	if (args->nr_bos > 4096)
		return -EINVAL;
	sbos = memdup_array_user(u64_to_user_ptr(args->bos), args->nr_bos,
				 sizeof(*sbos));
	if (IS_ERR(sbos))
		return PTR_ERR(sbos);
	job->bos = kvcalloc(args->nr_bos, sizeof(*job->bos), GFP_KERNEL);
	job->bo_va = kvcalloc(args->nr_bos, sizeof(*job->bo_va), GFP_KERNEL);
	if (!job->bos || !job->bo_va) {
		kfree(sbos);
		return -ENOMEM;
	}
	job->nr_bos = args->nr_bos;
	for (i = 0; i < args->nr_bos; i++) {
		struct drm_gem_object *obj;

		obj = drm_gem_object_lookup(file, sbos[i].handle);
		if (!obj) {
			err = -ENOENT;
			goto err;
		}
		err = psp2sgx_bo_map_get(to_psp2sgx_bo(obj), job->vm,
					 &job->bo_va[i]);
		if (err) {
			drm_gem_object_put(obj);
			goto err;
		}
		job->bos[i] = obj;
	}
	*out = sbos;
	return 0;
err:
	kfree(sbos);
	return err;
}

static int build_code(struct psp2sgx_job *job, const struct drm_psp2sgx_submit *args)
{
	void __user *cmds = u64_to_user_ptr(args->cmds);
	struct job_build b = { .job = job, .host = host_waits };
	void *buf = NULL;
	u32 waits;
	int err;

	if (args->type == PSP2SGX_JOB_RENDER &&
	    (!args->nr_cmds || args->nr_cmds > PSP2SGX_MAX_REG_OPS))
		return -EINVAL;
	waits = args->type == PSP2SGX_JOB_RENDER ?
		args->nr_cmds * (1 + PSP2SGX_NUM_CORES) : 0;
	job->max_steps = 2 * waits + 2;
	job->steps = kvmalloc_array(job->max_steps, sizeof(*job->steps), GFP_KERNEL);
	job->code = kvmalloc_array(JOB_CODE_TOTAL, sizeof(u64), GFP_KERNEL);
	if (!job->steps || !job->code)
		return -ENOMEM;

	switch (args->type) {
	case PSP2SGX_JOB_RENDER:
		buf = memdup_user(cmds, args->nr_cmds * sizeof(*cmds));
		if (IS_ERR(buf))
			return PTR_ERR(buf);
		err = build_render(&b, buf, args->nr_cmds);
		kfree(buf);
		break;
	case PSP2SGX_JOB_TASK:
		if (args->nr_cmds != 1)
			return -EINVAL;
		buf = memdup_user(cmds, sizeof(*cmds));
		if (IS_ERR(buf))
			return PTR_ERR(buf);
		err = build_task(&b, buf);
		kfree(buf);
		break;
	default:
		return -EINVAL;
	}
	if (err)
		return err;
	write_reg(&b, SGX_CLKGATECTL2, SGX_CLKGATECTL2_AUTO);
	end_run(&b);
	job->code_len = b.used;
	return b.err;
}

static int psp2sgx_ioctl_submit(struct drm_device *drm, void *data,
			 struct drm_file *file)
{
	struct psp2sgx_device *sgx = to_psp2sgx(drm);
	struct psp2sgx_file *pf = file->driver_priv;
	struct drm_psp2sgx_submit *args = data;
	struct drm_psp2sgx_submit_bo *sbos = NULL;
	struct drm_syncobj *out_sync = NULL;
	struct sync_file *sync_file = NULL;
	struct dma_fence *fence;
	struct psp2sgx_job *job;
	struct drm_exec exec;
	int out_fd = -1;
	u32 i;
	int err;

	if (args->pad ||
	    args->flags & ~(PSP2SGX_SUBMIT_FENCE_FD_IN | PSP2SGX_SUBMIT_FENCE_FD_OUT))
		return -EINVAL;

	job = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job)
		return -ENOMEM;
	job->sgx = sgx;
	job->vm = psp2sgx_vm_get(pf->vm);

	err = build_code(job, args);
	if (err)
		goto err_free;
	err = lookup_bos(file, job, args, &sbos);
	if (err)
		goto err_free;

	if (args->out_sync) {
		out_sync = drm_syncobj_find(file, args->out_sync);
		if (!out_sync) {
			err = -ENOENT;
			goto err_free;
		}
	}
	if (args->flags & PSP2SGX_SUBMIT_FENCE_FD_OUT) {
		out_fd = get_unused_fd_flags(O_CLOEXEC);
		if (out_fd < 0) {
			err = out_fd;
			goto err_free;
		}
	}

	err = drm_sched_job_init(&job->base, &pf->entity, 1, NULL, file->client_id);
	if (err)
		goto err_free;

	if (args->in_sync) {
		err = drm_sched_job_add_syncobj_dependency(&job->base, file,
							   args->in_sync, 0);
		if (err)
			goto err_job;
	}
	if (args->flags & PSP2SGX_SUBMIT_FENCE_FD_IN) {
		struct dma_fence *in = sync_file_get_fence(args->fence_fd);

		if (!in) {
			err = -EINVAL;
			goto err_job;
		}
		err = drm_sched_job_add_dependency(&job->base, in);
		if (err)
			goto err_job;
	}

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT, job->nr_bos);
	drm_exec_until_all_locked(&exec) {
		err = drm_exec_prepare_array(&exec, job->bos, job->nr_bos, 1);
		drm_exec_retry_on_contention(&exec);
		if (err)
			goto err_exec;
	}
	for (i = 0; i < job->nr_bos; i++) {
		err = drm_sched_job_add_implicit_dependencies(&job->base, job->bos[i],
							      sbos[i].flags & PSP2SGX_SUBMIT_BO_WRITE);
		if (err)
			goto err_exec;
	}

	drm_sched_job_arm(&job->base);
	fence = dma_fence_get(&job->base.s_fence->finished);
	drm_sched_entity_push_job(&job->base);

	for (i = 0; i < job->nr_bos; i++)
		dma_resv_add_fence(job->bos[i]->resv, fence,
				   sbos[i].flags & PSP2SGX_SUBMIT_BO_WRITE ?
				   DMA_RESV_USAGE_WRITE : DMA_RESV_USAGE_READ);
	drm_exec_fini(&exec);
	kfree(sbos);

	if (out_sync) {
		drm_syncobj_replace_fence(out_sync, fence);
		drm_syncobj_put(out_sync);
	}
	if (out_fd >= 0) {
		sync_file = sync_file_create(fence);
		if (sync_file) {
			fd_install(out_fd, sync_file->file);
			args->fence_fd = out_fd;
		} else {
			put_unused_fd(out_fd);
			args->fence_fd = -1;
		}
	}
	dma_fence_put(fence);
	return 0;

err_exec:
	drm_exec_fini(&exec);
err_job:
	drm_sched_job_cleanup(&job->base);
err_free:
	if (out_fd >= 0)
		put_unused_fd(out_fd);
	if (out_sync)
		drm_syncobj_put(out_sync);
	kfree(sbos);
	psp2sgx_job_free(job);
	return err;
}

static int psp2sgx_sched_init(struct psp2sgx_device *sgx)
{
	const struct drm_sched_init_args args = {
		.ops = &psp2sgx_sched_ops,
		.num_rqs = DRM_SCHED_PRIORITY_COUNT,
		.credit_limit = 1,
		.hang_limit = 0,
		.timeout = msecs_to_jiffies(PSP2SGX_JOB_TIMEOUT_MS),
		.name = "psp2sgx",
		.dev = sgx->dev,
	};

	int err;

	sgx->fence_context = dma_fence_context_alloc(1);
	spin_lock_init(&sgx->fence_lock);
	INIT_WORK(&sgx->step_work, psp2sgx_step_work);
	sgx->step_wq = alloc_ordered_workqueue("psp2sgx-steps", WQ_HIGHPRI | WQ_MEM_RECLAIM);
	if (!sgx->step_wq)
		return -ENOMEM;
	err = drm_sched_init(&sgx->sched, &args);
	if (err)
		destroy_workqueue(sgx->step_wq);
	return err;
}

void psp2sgx_sched_fini(struct psp2sgx_device *sgx)
{
	drm_sched_fini(&sgx->sched);
	destroy_workqueue(sgx->step_wq);
}
