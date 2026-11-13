// SPDX-License-Identifier: GPL-2.0-only
/*
 * psp2sgx: header for driver internals.
 */
#ifndef PSP2SGX_DRV_H
#define PSP2SGX_DRV_H

#include <linux/atomic.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include <drm/drm_device.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_mm.h>
#include <drm/gpu_scheduler.h>

#include "psp2sgx_mk.h"
#include "psp2sgx_regs.h"

#define PSP2SGX_NUM_CORES		4

/* GPU virtual address layout of every file's address space */
#define PSP2SGX_GENERAL_BASE		0x01000000u
#define PSP2SGX_GENERAL_END		0xd0000000u
#define PSP2SGX_PDS_BASE		0xd0000000u
#define PSP2SGX_PDS_END			0xe0000000u
#define PSP2SGX_CODE_WINDOW		0x00800000u	/* 2^20 instructions */
#define PSP2SGX_CODE_VERTEX_BASE	0xe0000000u	/* USSE code base 0 */
#define PSP2SGX_CODE_FRAGMENT_BASE	0xe0800000u	/* USSE code base 1 */
#define PSP2SGX_CODE_END		0xe1000000u

#define PSP2SGX_JOB_TIMEOUT_MS		2000
#define PSP2SGX_MAX_REG_OPS		1024

struct psp2sgx_vm;
struct psp2sgx_job;

/* CDRAM for buffer objects */
struct psp2sgx_cdram {
	phys_addr_t base;
	resource_size_t size;
	struct mutex lock;
	struct drm_mm mm;
	u64 used, peak;
	unsigned int objects;
	u64 fallbacks;
};

struct psp2sgx_device {
	struct drm_device drm;
	struct device *dev;
	void __iomem *regs;
	int irq;
	struct psp2sgx_vm *current_vm;

	int power_smc;
	u32 power_smc_bus;

	u32 core_id;
	u32 core_revision;

	void *kregion;
	dma_addr_t kregion_dma;
	size_t kregion_size;
	u32 *kregion_pt;
	dma_addr_t kregion_pt_dma;
	struct psp2sgx_vm *kernel_vm;

	struct drm_gpu_scheduler sched;
	u64 fence_context;
	u64 emit_seqno;
	spinlock_t fence_lock;
	struct psp2sgx_job *current_job;
	u32 kick_seqno;
	u32 sw_events, irq_done;
	unsigned int late_done, missed_done;
	spinlock_t slc_lock;
	struct workqueue_struct *step_wq;
	struct work_struct step_work;
	bool events_reported;
	unsigned int timeouts_dumped;
	bool regs_dumped;
	bool needs_reset;
	unsigned int resets;
	struct {
		u32 jobs, max;
		u64 sum;
		u32 over[4];
	} polls[2];
	struct {
		u32 jobs, max_us;
		u64 sum_us;
	} host_polls[2];
	struct delayed_work diag_work;
	char selftest[64];

	struct mutex hw_lock;

	struct psp2sgx_cdram cdram;
	atomic64_t shmem_bytes;
	atomic_t shmem_objects;
};

static inline struct psp2sgx_device *to_psp2sgx(struct drm_device *drm)
{
	return container_of(drm, struct psp2sgx_device, drm);
}

static inline u32 sgx_read(struct psp2sgx_device *sgx, u32 reg)
{
	return readl(sgx->regs + reg);
}

static inline void sgx_write(struct psp2sgx_device *sgx, u32 reg, u32 value)
{
	writel(value, sgx->regs + reg);
}

static inline u32 *psp2sgx_hostctl(struct psp2sgx_device *sgx)
{
	return (u32 *)((u8 *)sgx->kregion + SGX_MK_HOSTCTL);
}

static inline u32 *psp2sgx_hostpost(struct psp2sgx_device *sgx)
{
	return (u32 *)((u8 *)sgx->kregion + SGX_MK_HOSTPOST);
}

static inline void psp2sgx_put_code(void *dst, const u64 *code, u32 n)
{
	u32 *w = dst;
	u32 i;

	for (i = 0; i < n; i++) {
		w[2 * i] = lower_32_bits(code[i]);
		w[2 * i + 1] = upper_32_bits(code[i]);
	}
}

struct psp2sgx_vm {
	struct kref ref;
	struct psp2sgx_device *sgx;
	struct mutex lock;
	struct drm_mm mm;
	u32 *pd;
	dma_addr_t pd_dma;
	u32 *pt[SGX_MMU_ENTRIES];
	dma_addr_t pt_dma[SGX_MMU_ENTRIES];
};

struct psp2sgx_file {
	struct psp2sgx_vm *vm;
	struct drm_sched_entity entity;
};

int psp2sgx_hw_power_on(struct psp2sgx_device *sgx);
void psp2sgx_hw_power_off(struct psp2sgx_device *sgx);
int psp2sgx_hw_init(struct psp2sgx_device *sgx);
unsigned int psp2sgx_hw_cores(void);
int psp2sgx_hw_reset(struct psp2sgx_device *sgx);
int psp2sgx_hw_slc_flush(struct psp2sgx_device *sgx);
int psp2sgx_hw_slc_writeback(struct psp2sgx_device *sgx);
int psp2sgx_hw_wait_cores_idle(struct psp2sgx_device *sgx, u32 seqno);
int psp2sgx_hw_invalidate_caches(struct psp2sgx_device *sgx);
void psp2sgx_hw_set_vm(struct psp2sgx_device *sgx, struct psp2sgx_vm *vm);
void psp2sgx_hw_kick(struct psp2sgx_device *sgx);
int psp2sgx_hw_build_kregion(struct psp2sgx_device *sgx);

void psp2sgx_diag_dump(struct psp2sgx_device *sgx, const char *why);
int psp2sgx_diag_selftest(struct psp2sgx_device *sgx);
void psp2sgx_diag_init(struct psp2sgx_device *sgx);
void psp2sgx_diag_fini(struct psp2sgx_device *sgx);
void psp2sgx_diag_regdump(struct psp2sgx_device *sgx, const char *why);

int psp2sgx_kregion_init(struct psp2sgx_device *sgx);
void psp2sgx_kregion_fini(struct psp2sgx_device *sgx);
struct psp2sgx_vm *psp2sgx_vm_create(struct psp2sgx_device *sgx);
void psp2sgx_vm_put(struct psp2sgx_vm *vm);
static inline struct psp2sgx_vm *psp2sgx_vm_get(struct psp2sgx_vm *vm)
{
	kref_get(&vm->ref);
	return vm;
}
u32 psp2sgx_pte_flags(void);
int psp2sgx_vm_map_sgt(struct psp2sgx_vm *vm, u32 va, struct sg_table *sgt,
		       size_t size, u32 flags);
int psp2sgx_vm_map_contig(struct psp2sgx_vm *vm, u32 va, phys_addr_t pa,
			  size_t size, u32 flags);
void psp2sgx_vm_unmap(struct psp2sgx_vm *vm, u32 va, size_t size);
u32 psp2sgx_vm_pte_read(struct psp2sgx_vm *vm, u32 va);
bool psp2sgx_vm_node_at(struct psp2sgx_vm *vm, u32 va, u32 *start, u32 *size);

int psp2sgx_sched_init(struct psp2sgx_device *sgx);
void psp2sgx_sched_fini(struct psp2sgx_device *sgx);
void psp2sgx_job_irq(struct psp2sgx_device *sgx);
int psp2sgx_ioctl_submit(struct drm_device *drm, void *data,
			 struct drm_file *file);

#endif
