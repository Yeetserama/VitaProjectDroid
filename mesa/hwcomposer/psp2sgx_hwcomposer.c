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

#include "psp2sgx_hwcomposer.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

struct psp2sgx_hwcomposer *
psp2sgx_hwcomposer_new(int width, int height, int stride)
{
	struct psp2sgx_hwcomposer *comp;

	comp = calloc(1, sizeof(*comp));
	if (!comp)
		return NULL;

	comp->width = width;
	comp->height = height;
	comp->stride = stride ? stride : width * 4; /* RGBA8888 default */
	comp->fb_size = height * comp->stride;
	comp->fb_base = NULL;
	comp->cursor_base = NULL;
	comp->flip_pending = false;

	/* Map the framebuffer */
	int fd = open("/dev/dri/card0", O_RDWR);
	if (fd < 0) {
		mesa_loge("hwcomposer: failed to open /dev/dri/card0: %m\n");
		free(comp);
		return NULL;
	}

	comp->fb_base = mmap(NULL, comp->fb_size, PROT_READ | PROT_WRITE,
			     MAP_SHARED, fd, 0);
	if (comp->fb_base == MAP_FAILED) {
		mesa_loge("hwcomposer: mmap fb failed: %m\n");
		close(fd);
		free(comp);
		return NULL;
	}

	comp->cursor_size = 64 * 64 * 4; /* 64x64 RGBA cursor */
	comp->cursor_base = mmap(NULL, comp->cursor_size, PROT_READ | PROT_WRITE,
				 MAP_SHARED, fd, 0);
	if (comp->cursor_base == MAP_FAILED) {
		mesa_loge("hwcomposer: mmap cursor failed: %m\n");
		close(fd);
		free(comp);
		return NULL;
	}

	close(fd);
	mesa_logi("hwcomposer: initialized %dx%d display\n", width, height);
	return comp;
}

void
psp2sgx_hwcomposer_del(struct psp2sgx_hwcomposer *comp)
{
	if (!comp)
		return;

	if (comp->fb_base)
		munmap(comp->fb_base, comp->fb_size);
	if (comp->cursor_base)
		munmap(comp->cursor_base, comp->cursor_size);

	free(comp);
}

int
psp2sgx_hwcomposer_set_framebuffer(struct psp2sgx_hwcomposer *comp,
                                   uint32_t fb_base, uint32_t fb_size)
{
	if (!comp)
		return -EINVAL;

	/* In a real implementation, we'd validate the fb_base address
	   and update the display controller */
	comp->fb_base = (uint32_t *)fb_base;
	comp->fb_size = fb_size;
	return 0;
}

int
psp2sgx_hwcomposer_page_flip(struct psp2sgx_hwcomposer *comp,
                             void (*callback)(void *data),
                             void *data)
{
	if (!comp)
		return -EINVAL;

	/* In a real implementation, we'd programming the display controller
	   to flip to the next framebuffer. For now, just mark the flip. */
	comp->flip_pending = true;

	/* Simulate flip completion after a brief delay */
	if (callback)
		callback(data);

	return 0;
}

int
psp2sgx_hwcomposer_set_cursor(struct psp2sgx_hwcomposer *comp,
                              uint8_t *cursor_data,
                              int cursor_width, int cursor_height,
                              int hot_x, int hot_y)
{
	if (!comp || !cursor_data)
		return -EINVAL;

	/* In a real implementation, we'd program the cursor hardware.
	   For now, just copy the cursor data. */
	if (cursor_width > 64)
		cursor_width = 64;
	if (cursor_height > 64)
		cursor_height = 64;

	memcpy(comp->cursor_base, cursor_data, cursor_width * cursor_height * 4);
	return 0;
}

uint32_t
psp2sgx_hwcomposer_get_vblank(struct psp2sgx_hwcomposer *comp)
{
	if (!comp)
		return 0;

	/* In a real implementation, we'd read the vblank counter from the
	   display controller's status register. For now, return a dummy value. */
	return 1;
}

int
psp2sgx_hwcomposer_set_plane(struct psp2sgx_hwcomposer *comp,
                             int plane_id,
                             uint32_t fb,
                             struct rect *src_rect,
                             struct rect *dst_rect,
                             uint8_t alpha,
                             uint8_t transform)
{
	if (!comp)
		return -EINVAL;

	/* In a real implementation, we'd configure a display plane with the
	   given framebuffer, source/destination rectangles, alpha, and transform.
	   For now, just acknowledge the request. */
	return 0;
}

int
psp2sgx_hwcomposer_compose(struct psp2sgx_hwcomposer *comp)
{
	if (!comp)
		return -EINVAL;

	/* In a real implementation, we'd trigger a full display composition
	   by programming the display controller with the current framebuffer,
   cursor, and OSD layers. For now, just log and return. */
	mesa_logd("hwcomposer: composing frame %dx%d\n", comp->width, comp->height);
	return 0;
}

const char *
psp2sgx_hwcomposer_get_compositor(struct psp2sgx_hwcomposer *comp)
{
	if (!comp)
		return "unknown";

	return "psp2sgx";
}