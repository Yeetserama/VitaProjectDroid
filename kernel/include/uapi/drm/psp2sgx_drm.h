// SPDX-License-Identifier: GPL-2.0-only */
/*
 * UAPI for the psp2sgx DRM driver (PlayStation Vita SGX543MP4+).
 */
#ifndef _DRM_PSP2SGX_H_
#define _DRM_PSP2SGX_H_

#include <drm/drm.h>

#define DRM_PSP2SGX_GET_PARAM		0x00
#define DRM_PSP2SGX_BO_CREATE		0x01
#define DRM_PSP2SGX_BO_INFO		0x02
#define DRM_PSP2SGX_BO_WAIT		0x03
#define DRM_PSP2SGX_SUBMIT		0x04

#define DRM_IOCTL_PSP2SGX_GET_PARAM	DRM_IOWR(DRM_COMMAND_BASE + DRM_PSP2SGX_GET_PARAM, struct drm_psp2sgx_get_param)
#define DRM_IOCTL_PSP2SGX_BO_CREATE	DRM_IOW(DRM_COMMAND_BASE + DRM_PSP2SGX_BO_CREATE, struct drm_psp2sgx_bo_create)
#define DRM_IOCTL_PSP2SGX_BO_INFO	DRM_IOWR(DRM_COMMAND_BASE + DRM_PSP2SGX_BO_INFO, struct drm_psp2sgx_bo_info)
#define DRM_IOCTL_PSP2SGX_BO_WAIT	DRM_IOW(DRM_COMMAND_BASE + DRM_PSP2SGX_BO_WAIT, struct drm_psp2sgx_bo_wait)
#define DRM_IOCTL_PSP2SGX_SUBMIT	DRM_IOWR(DRM_COMMAND_BASE + DRM_PSP2SGX_SUBMIT, struct drm_psp2sgx_submit)

#define DRM_PSP2SGX_PARAM_UAPI_VERSION	0
#define DRM_PSP2SGX_PARAM_CORE_ID	1
#define DRM_PSP2SGX_PARAM_CORE_REVISION	2
#define DRM_PSP2SGX_PARAM_NUM_CORES	3
#define DRM_PSP2SGX_PARAM_GENERAL_HEAP_BASE	4
#define DRM_PSP2SGX_PARAM_GENERAL_HEAP_SIZE	5
#define DRM_PSP2SGX_PARAM_CODE_VERTEX_BASE	6
#define DRM_PSP2SGX_PARAM_CODE_FRAGMENT_BASE	7
#define DRM_PSP2SGX_PARAM_CODE_WINDOW_SIZE	8
#define DRM_PSP2SGX_PARAM_PDS_HEAP_BASE		9
#define DRM_PSP2SGX_PARAM_PDS_HEAP_SIZE		10
#define DRM_PSP2SGX_PARAM_MAX_REG_OPS		11
#define DRM_PSP2SGX_PARAM_CDRAM_SIZE		12

#define DRM_PSP2SGX_UAPI_VERSION	1

#define PSP2SGX_BO_HEAP_MASK		0x7f
#define PSP2SGX_BO_CDRAM		0x80
#define PSP2SGX_BO_PLACEMENT_SYSTEM	0
#define PSP2SGX_BO_PLACEMENT_CDRAM	1

#define PSP2SGX_BO_HEAP_GENERAL		0
#define PSP2SGX_BO_HEAP_CODE_VERTEX	1
#define PSP2SGX_BO_HEAP_CODE_FRAGMENT	2
#define PSP2SGX_BO_HEAP_PDS		3

#define PSP2SGX_BO_HEAP_MASK_SIZE	4

#define PSP2SGX_BO_WAIT_READ		1
#define PSP2SGX_BO_WAIT_WRITE		2

#define PSP2SGX_JOB_TYPE_RENDER		0
#define PSP2SGX_JOB_TYPE_TASK		1

#define PSP2SGX_MAX_REG_OPS		1024

#define PSP2SGX_GENERAL_BASE		0x01000000u
#define PSP2SGX_GENERAL_END		0xd0000000u
#define PSP2SGX_PDS_BASE		0xd0000000u
#define PSP2SGX_PDS_END			0xe0000000u
#define PSP2SGX_CODE_VERTEX_BASE	0xe0000000u
#define PSP2SGX_CODE_FRAGMENT_BASE	0xe0800000u
#define PSP2SGX_CODE_END		0xe1000000u
#define PSP2SGX_CODE_WINDOW		0x00800000u

#define PSP2SGX_JOB_TIMEOUT_MS		2000

struct drm_psp2sgx_get_param {
	__u32 param;
	__u32 value;
	__u32 pad;
};

struct drm_psp2sgx_bo_create {
	__u32 size;
	__u32 flags;
	__u32 handle;
	__u32 pad;
	__u64 placement;
	__u64 gpu_va;
};

struct drm_psp2sgx_bo_info {
	__u32 handle;
	__u32 pad;
	__u64 size;
	__u64 gpu_va;
	__u64 mmap_offset;
};

struct drm_psp2sgx_bo_wait {
	__u32 handle;
	__u32 op;
	__u64 timeout_ns;
};

struct drm_psp2sgx_reg_op {
	__u32 op;
	__u32 reg;
	__u32 mask;
	__u32 value;
};

struct drm_psp2sgx_task {
	__u32 ir0;
	__u32 sideband;
	__u32 pds_addr;
	__u32 pad;
};

struct drm_psp2sgx_submit {
	__u32 type;
	__u32 flags;
	__u32 nr_cmds;
	__u32 nr_bos;
	__u64 cmds;
	__u64 bos;
	__u64 in_sync;
	__u64 out_sync;
	__u32 fence_fd;
	__u32 pad;
};

#define PSP2SGX_SUBMIT_FENCE_FD_IN	1
#define PSP2SGX_SUBMIT_FENCE_FD_OUT	2

#define PSP2SGX_SUBMIT_BO_WRITE		1

#endif
