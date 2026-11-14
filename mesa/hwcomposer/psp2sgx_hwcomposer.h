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

#ifndef PSP2SGX_HWCOMPOSER_H
#define PSP2SGX_HWCOMPOSER_H

#include <stdint.h>
#include <sys/cdefs.h>

/*
 * Vita SGX543MP4+ hardware composer
 *
 * Manages display composition across the SGX's four cores.
 * Handles page flipping, vsync, and VRAM allocation.
 */

/*
 * psp2sgx_hwcomposer - hardware composer state
 */
struct psp2sgx_hwcomposer {
	int width;		/* display width in pixels */
	int height;		/* display height in pixels */
	int stride;		/* byte stride between scanlines */
	uint32_t *fb_base;		/* framebuffer base address */
	uint32_t fb_size;		/* framebuffer size in bytes */
	uint32_t *cursor_base;	/* cursor framebuffer base */
	uint32_t cursor_size;	/* cursor framebuffer size */
	bool flip_pending;		/* page flip in progress */
};

/*
 * psp2sgx_hwcomposer_new - create a new hardware composer
 * @width: display width in pixels
 * @height: display height in pixels
 * @stride: byte stride between scanlines
 *
 * Return: allocated hwcomposer, or NULL on failure.
 */
struct psp2sgx_hwcomposer *
psp2sgx_hwcomposer_new(int width, int height, int stride);

/*
 * psp2sgx_hwcomposer_del - destroy a hardware composer
 * @comp: hwcomposer to destroy
 */
void
psp2sgx_hwcomposer_del(struct psp2sgx_hwcomposer *comp);

/*
 * psp2sgx_hwcomposer_set_framebuffer - set the framebuffer
 * @comp: hwcomposer instance
 * @fb_base: framebuffer base address
 * @fb_size: framebuffer size
 *
 * Return: 0 on success, negative errno on failure.
 */
int
psp2sgx_hwcomposer_set_framebuffer(struct psp2sgx_hwcomposer *comp,
                                   uint32_t fb_base, uint32_t fb_size);

/*
 * psp2sgx_hwcomposer_page_flip - request a page flip
 * @comp: hwcomposer instance
 * @callback: callback invoked after flip completes
 * @data: user data passed to callback
 *
 * Return: 0 on success, negative errno on failure.
 */
int
psp2sgx_hwcomposer_page_flip(struct psp2sgx_hwcomposer *comp,
                             void (*callback)(void *data),
                             void *data);

/*
 * psp2sgx_hwcomposer_set_cursor - set the cursor image
 * @comp: hwcomposer instance
 * @cursor_data: cursor pixel data
 * @cursor_width: cursor width in pixels
 * @cursor_height: cursor height in pixels
 * @hot_x: cursor hotspot X offset
 * @hot_y: cursor hotspot Y offset
 *
 * Return: 0 on success, negative errno on failure.
 */
int
psp2sgx_hwcomposer_set_cursor(struct psp2sgx_hwcomposer *comp,
                              uint8_t *cursor_data,
                              int cursor_width, int cursor_height,
                              int hot_x, int hot_y);

/*
 * psp2sgx_hwcomposer_get_vblank - get the vblank count
 * @comp: hwcomposer instance
 *
 * Return: vblank count.
 */
uint32_t
psp2sgx_hwcomposer_get_vblank(struct psp2sgx_hwcomposer *comp);

/*
 * psp2sgx_hwcomposer_set_plane - set a display plane
 * @comp: hwcomposer instance
 * @plane_id: plane identifier
 * @fb: framebuffer to display
 * @src_rect: source rectangle in pixels
 * @dst_rect: destination rectangle in pixels
 * @alpha: alpha value (0-255)
 * @transform: transformation applied to the layer
 *
 * Return: 0 on success, negative errno on failure.
 */
int
psp2sgx_hwcomposer_set_plane(struct psp2sgx_hwcomposer *comp,
                             int plane_id,
                             uint32_t fb,
                             struct rect *src_rect,
                             struct rect *dst_rect,
                             uint8_t alpha,
                             uint8_t transform);

/*
 * psp2sgx_hwcomposer_compose - request a full composition
 * @comp: hwcomposer instance
 *
 * Return: 0 on success, negative errno on failure.
 */
int
psp2sgx_hwcomposer_compose(struct psp2sgx_hwcomposer *comp);

/*
 * psp2sgx_hwcomposer_compositor - get the compositor name
 * @comp: hwcomposer instance
 *
 * Return: compositor name string.
 */
const char *
psp2sgx_hwcomposer_get_compositor(struct psp2sgx_hwcomposer *comp);

#endif /* PSP2SGX_HWCOMPOSER_H */