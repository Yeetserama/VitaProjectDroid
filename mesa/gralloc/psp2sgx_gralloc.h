// Copyright © 2026 AndroidOnVita
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

/*
 * psp2sgx gralloc - GPU memory allocation for Android
 *
 * Handles buffer allocation, mapping, and sharing for the Vita SGX543MP4+.
 * Uses the kernel DRM driver for physical memory management and
 * DMA buffer export/import for cross-process sharing.
 */

#ifndef PSP2SGX_GRALLOC_H
#define PSP2SGX_GRALLOC_H

#include <stdint.h>
#include <sys/cdefs.h>

/*
 * Buffer heap types for the SGX
 */
#define PSP2SGX_HEAP_TYPE_GENERAL	0
#define PSP2SGX_HEAP_TYPE_CODE_VERTEX	1
#define PSP2SGX_HEAP_TYPE_CODE_FRAGMENT	2
#define PSP2SGX_HEAP_TYPE_PDS		3

/*
 * Buffer flags
 */
#define PSP2SGX_BUFFER_GPU_READ		(1 << 0)
#define PSP2SGX_BUFFER_GPU_WRITE	(1 << 1)
#define PSP2SGX_BUFFER_CPU_READ		(1 << 2)
#define PSP2SGX_BUFFER_CPU_WRITE	(1 << 3)
#define PSP2SGX_BUFFER_SHARED		(1 << 4)
#define PSP2SGX_BUFFER_PROTECTED	(1 << 5)

/*
 * psp2sgx_gralloc_module - gralloc module state
 */
struct psp2sgx_gralloc_module {
	void * reserved[2]; /* future expansion */
};

/*
 * psp2sgx_gralloc_buffer - buffer object allocated by gralloc
 */
struct psp2sgx_gralloc_buffer {
	uint32_t handle;		/* handle for ioctl */
	uint32_t size;			/* buffer size in bytes */
	uint32_t flags;			/* buffer flags */
	uint32_t heap_id;		/* heap identifier */
	uint32_t offset;		/* offset within heap */
	void *cpu_vaddr;		/* CPU virtual address */
	dma_addr_t dma_addr;		/* DMA address for GPU */
};

/*
 * psp2sgx_gralloc_alloc - allocate a buffer
 * @module: gralloc module state
 * @w: buffer width
 * @h: buffer height
 * @format: buffer format
 * @flags: buffer flags
 * @out_buf: output buffer object
 *
 * Return: 0 on success, negative errno on failure.
 */
int psp2sgx_gralloc_alloc(struct psp2sgx_gralloc_module *module,
                          int w, int h, int format,
                          int flags,
                          struct psp2sgx_gralloc_buffer *out_buf);

/*
 * psp2sgx_gralloc_free - free a previously allocated buffer
 * @module: gralloc module state
 * @buf: buffer to free
 *
 * Return: 0 on success, negative errno on failure.
 */
int psp2sgx_gralloc_free(struct psp2sgx_gralloc_module *module,
                         struct psp2sgx_gralloc_buffer *buf);

/*
 * psp2sgx_gralloc_lock - lock a buffer for CPU access
 * @module: gralloc module state
 * @buf: buffer to lock
 * @flags: locking flags
 * @out_cpu_vaddr: output CPU virtual address
 *
 * Return: 0 on success, negative errno on failure.
 */
int psp2sgx_gralloc_lock(struct psp2sgx_gralloc_module *module,
                         struct psp2sgx_gralloc_buffer *buf,
                         int flags,
                         void **out_cpu_vaddr);

/*
 * psp2sgx_gralloc_unlock - unlock a previously locked buffer
 * @module: gralloc module state
 * @buf: buffer to unlock
 *
 * Return: 0 on success, negative errno on failure.
 */
int psp2sgx_gralloc_unlock(struct psp2sgx_gralloc_module *module,
                           struct psp2sgx_gralloc_buffer *buf);

/*
 * psp2sgx_gralloc_process_damage - get damage region for partial updates
 * @module: gralloc module state
 * @buf: buffer to query
 * @out_x, @out_y, @out_width, @out_height: output damage rectangle
 *
 * Return: 0 on success, negative errno on failure.
 */
int psp2sgx_gralloc_process_damage(struct psp2sgx_gralloc_module *module,
                                   struct psp2sgx_gralloc_buffer *buf,
                                   int *out_x, int *out_y,
                                   int *out_width, int *out_height);

/*
 * psp2sgx_gralloc_add_buffer - add a buffer to the free list
 * @module: gralloc module state
 * @buf: buffer to add
 *
 * Return: 0 on success, negative errno on failure.
 */
int psp2sgx_gralloc_add_buffer(struct psp2sgx_gralloc_module *module,
                               struct psp2sgx_gralloc_buffer *buf);

/*
 * psp2sgx_gralloc_remove_buffer - remove a buffer from the free list
 * @module: gralloc module state
 * @buf: buffer to remove
 *
 * Return: 0 on success, negative errno on failure.
 */
int psp2sgx_gralloc_remove_buffer(struct psp2sgx_gralloc_module *module,
                                  struct psp2sgx_gralloc_buffer *buf);

#endif /* PSP2SGX_GRALLOC_H */