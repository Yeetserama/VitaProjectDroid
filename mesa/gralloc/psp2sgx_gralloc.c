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

#include "psp2sgx_gralloc.h"
#include "psp2sgx_gem.h"
#include <linux/dma-buf.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <error.h>

/*
 * psp2sgx_gralloc_alloc - allocate a buffer via the kernel DRM driver
 */
int
psp2sgx_gralloc_alloc(struct psp2sgx_gralloc_module *module,
                      int w, int h, int format,
                      int flags,
                      struct psp2sgx_gralloc_buffer *out_buf)
{
	int fd;
	uint32_t size;
	int err;

	if (!module || !out_buf)
		return -EINVAL;

	/* Calculate buffer size based on format and dimensions */
	switch (format) {
	case HAL_FORMAT_RGB_565:
		size = w * h * 2;
		break;
	case HAL_FORMAT_RGB_888:
		size = w * h * 3;
		break;
	case HAL_FORMAT_RGBA_8888:
		size = w * h * 4;
		break;
	default:
		size = w * h * 4;
		break;
	}

	/* Round up to page boundary */
	size = (size + 4095) & ~4095;

	/* Open the DRM device fd */
	fd = open("/dev/dri/card0", O_RDWR);
	if (fd < 0) {
		mesa_loge("gralloc: failed to open /dev/dri/card0: %m\n");
		return -errno;
	}

	/* Create a DMA buffer */
	/* In a real implementation, we'd use the DMABUF API */
	out_buf->size = size;
	out_buf->flags = flags;
	out_buf->cpu_vaddr = mmap(NULL, size, PROT_READ | PROT_WRITE,
				  MAP_SHARED, fd, 0);
	if (out_buf->cpu_vaddr == MAP_FAILED) {
		mesa_loge("gralloc: mmap failed: %m\n");
		close(fd);
		return -errno;
	}

	out_buf->dma_addr = 0; /* will be filled in by kernel */
	out_buf->handle = fd; /* use fd as handle */
	out_buf->offset = 0;

	close(fd);
	return 0;
}

/*
 * psp2sgx_gralloc_free - free a previously allocated buffer
 */
int
psp2sgx_gralloc_free(struct psp2sgx_gralloc_module *module,
                     struct psp2sgx_gralloc_buffer *buf)
{
	if (!buf)
		return -EINVAL;

	if (buf->cpu_vaddr) {
		munmap(buf->cpu_vaddr, buf->size);
	}
	if (buf->handle >= 0) {
		close(buf->handle);
	}

	memset(buf, 0, sizeof(*buf));
	return 0;
}

/*
 * psp2sgx_gralloc_lock - lock a buffer for CPU access
 */
int
psp2sgx_gralloc_lock(struct psp2sgx_gralloc_module *module,
                     struct psp2sgx_gralloc_buffer *buf,
                     int flags,
                     void **out_cpu_vaddr)
{
	if (!buf || !out_cpu_vaddr)
		return -EINVAL;

	if (!buf->cpu_vaddr) {
		mesa_loge("gralloc: buffer not allocated\n");
		return -EINVAL;
	}

	*out_cpu_vaddr = buf->cpu_vaddr;

	/* Increment reference count if needed */
	return 0;
}

/*
 * psp2sgx_gralloc_unlock - unlock a previously locked buffer
 */
int
psp2sgx_gralloc_unlock(struct psp2sgx_gralloc_module *module,
                       struct psp2sgx_gralloc_buffer *buf)
{
	if (!buf)
		return -EINVAL;

	/* For mapped buffers, just clear the lock flag */
	/* No-op for mmap'd buffers */
	return 0;
}

/*
 * psp2sgx_gralloc_process_damage - get damage region for partial updates
 */
int
psp2sgx_gralloc_process_damage(struct psp2sgx_gralloc_module *module,
                               struct psp2sgx_gralloc_buffer *buf,
                               int *out_x, int *out_y,
                               int *out_width, int *out_height)
{
	if (!buf || !out_x || !out_y || !out_width || !out_height)
		return -EINVAL;

	/* For full-screen buffers, return the full rectangle */
	*out_x = 0;
	*out_y = 0;
	*out_width = buf->size ? 1920 : 0; /* assuming 1920x1080 default */
	*out_height = buf->size ? 1080 : 0;

	return 0;
}

/*
 * psp2sgx_gralloc_add_buffer - add a buffer to the free list
 */
int
psp2sgx_gralloc_add_buffer(struct psp2sgx_gralloc_module *module,
                           struct psp2sgx_gralloc_buffer *buf)
{
	if (!module || !buf)
		return -EINVAL;

	/* In a real implementation, add to a free list */
	return 0;
}

/*
 * psp2sgx_gralloc_remove_buffer - remove a buffer from the free list
 */
int
psp2sgx_gralloc_remove_buffer(struct psp2sgx_gralloc_module *module,
                              struct psp2sgx_gralloc_buffer *buf)
{
	if (!module || !buf)
		return -EINVAL;

	/* In a real implementation, remove from free list */
	return 0;
}