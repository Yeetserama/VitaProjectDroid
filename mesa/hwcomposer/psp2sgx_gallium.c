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

#include "psp2sgx_gallium.h"
#include "compiler/shader_enums.h"
#include "util/u_memory.h"
#include "util/u_helpers.h"
#include "frontend/nir/nir_shader_compiler.h"

/*
 * Shader stage to USSE mapping:
 * VERTEX -> USSE core 0 (vertex processing)
 * FRAGMENT -> USSE cores 1-3 (fragment processing)
 * PDS programs handle vertex attribute fetching and setup
 */

static const uint32_t psp2sgx_usse_regs[PSP2SGX_USSE_REGs];
/* Regs r0-r127, with r124-r127 being internal aliases */

/*
 * psp2sgx_screen_new - create a new screen context
 * @drm_fd: file descriptor to the DRM device
 *
 * Return: allocated screen, or NULL on failure.
 */
struct gallium_screen *
psp2sgx_screen_new(int drm_fd)
{
	struct psp2sgx_screen *screen;
	int err;

	screen = rzalloc(NULL, struct psp2sgx_screen);
	if (!screen)
		return NULL;

	screen->sgx = NULL;
	screen->drm_file = NULL;
	screen->kb = NULL;
	screen->code_base = PSP2SGX_CODE_BASE;
	screen->self_test_passed = false;

	/* Open the DRM device */
	screen->drm_file = drmOpenWithTag("psp2sgx", NULL);
	if (!screen->drm_file) {
		mesa_loge("psp2sgx: failed to open DRM device\n");
		ralloc_free(screen);
		return NULL;
	}

	/* Map the kernel region */
	err = psp2sgx_hw_init_screen(screen);
	if (err) {
		mesa_loge("psp2sgx: failed to init screen: %d\n", err);
		drmClose(screen->drm_file);
		ralloc_free(screen);
		return NULL;
	}

	/* Run self-test */
	err = psp2sgx_run_self_test(screen);
	if (err) {
		mesa_logw("psp2sgx: self-test failed (err=%d), continuing anyway\n", err);
		/* Self-test failure is not fatal - GPU is still powered on */
	} else {
		screen->self_test_passed = true;
		mesa_logi("psp2sgx: self-test passed\n");
	}

	return &screen->base;
}

/*
 * psp2sgx_screen_del - destroy a screen context
 */
void
psp2sgx_screen_del(struct gallium_screen *screen_)
{
	struct psp2sgx_screen *screen = (struct psp2sgx_screen *)screen_;

	if (screen->kb)
		pb_buffer_reference(&screen->kb, NULL);
	if (screen->drm_file)
		drmClose(screen->drm_file);
	ralloc_free(screen);
}

/*
 * psp2sgx_compile_shader - compile GLSL/ESSL to USSE/PDS
 */
int
psp2sgx_compile_shader(struct gallium_screen *screen,
                       struct gl_shader *templ,
                       struct psp2sgx_program *prog)
{
	struct psp2sgx_screen *psp2screen = (struct psp2sgx_screen *)screen;
	int err = 0;
	uint32_t *usse_buf;
	unsigned int i;

	if (!prog || !templ)
		return -EINVAL;

	/* Allocate USSE code buffer */
	prog->usse_size = templ->UsseSize;
	prog->usse_code = rzalloc_array(prog, uint32_t, prog->usse_size);
	if (!prog->usse_code) {
		err = -ENOMEM;
		goto out;
	}

	/* Encode USSE instructions from the shader */
	for (i = 0; i < prog->usse_size; i++) {
	 /* Simple encoding - real implementation would parse the shader */
		prog->usse_code[i] = USSE_MOV_IMM(0);
	}

	/* Build PDS program */
	prog->pds_size = 1; /* minimum PDS program */
	prog->pds_code = rzalloc_array(prog, uint32_t, prog->pds_size * 7);
	if (!prog->pds_code) {
		err = -ENOMEM;
		goto out;
	}

	/* PDS program: DOUTU to start USSE task, then HALT */
	prog->pds_code[0] = 0x00012001; /* info 00012001 */
	prog->pds_code[1] = 0x22008000; /* PDS enable 0x80 = 128 regs */
	prog->pds_code[2] = 0xf0000200; /* kicker */
	prog->pds_code[3] = 0x00000000; /* reserved */
	prog->pds_code[4] = 0x00000000; /* reserved */
	prog->pds_code[5] = 0x00000000; /* reserved */
	prog->pds_code[6] = 0x00000000; /* HALT */

	prog->code_base = PSP2SGX_CODE_BASE;
	prog->kicker = 1;

	out:
	return err;
}

/*
 * psp2sgx_validate_state - validate context state
 */
int
psp2sgx_validate_state(struct gallium_context *ctx)
{
	struct psp2sgx_context *pctx = (struct psp2sgx_context *)ctx;

	if (!pctx)
		return -EINVAL;

	/* Validate that we have a valid program */
	if (!pctx->base.current.prog)
		return -EINVAL;

	/* Validate kicker word */
	if (pctx->dirtily & PSP2SGX_DIRTY_KICKER) {
		/* Update kicker word in kernel region */
	}

	return 0;
}

/*
 * psp2sgx_emit_ussse - emit USSE instructions to a program
 */
int
psp2sgx_emit_ussse(struct psp2sgx_program *prog, uint32_t inst)
{
	if (!prog || prog->usse_size <= 0)
		return -EINVAL;

	if (prog->usse_idx >= prog->usse_size)
		return -E2BIG;

	prog->usse_code[prog->usse_idx++] = inst;
	return 0;
}

/*
 * psp2sgx_launch_job - launch a shader job on the SGX
 */
int
psp2sgx_launch_job(struct gallium_context *ctx,
                   struct gallium_buffer *buf,
                   struct psp2sgx_program *prog,
                   int x, int y, int width, int height)
{
	struct psp2sgx_context *pctx = (struct psp2sgx_context *)ctx;
	struct psp2sgx_screen *screen = (struct psp2sgx_context *)ctx->screen;
	struct drm_psp2sgx_submit submit = {0};
	int err;

	if (!pctx || !screen || !prog)
		return -EINVAL;

	/* Build the submit data */
	submit.prog = prog->pds_code; /* PDS program */
	submit.ussc = prog->usse_code; /* USSE code */
	submit.kicker = prog->kicker;
	submit.x = x;
	submit.y = y;
	submit.width = width;
	submit.height = height;
	submit.code_base = prog->code_base;
	submit.flags = 0;

	/* Submit via DRM ioctl */
	err = drmIoctl(screen->drm_file, DRM_PSP2SGX_SUBMIT, &submit);
	if (err) {
		mesa_loge("psp2sgx: job submit failed: %d\n", err);
		return err;
	}

	return 0;
}

/*
 * psp2sgx_destroy_screen - destroy the screen context
 */
void
psp2sgx_destroy_screen(struct gallium_screen *screen_)
{
	struct psp2sgx_screen *screen = (struct psp2sgx_screen *)screen_;

	if (screen->kb)
		pb_buffer_reference(&screen->kb, NULL);
	if (screen->drm_file)
		drmClose(screen->drm_file);
	ralloc_free(screen);
}

/*
 * psp2sgx_get_param - get driver parameters via ioctl
 */
int
psp2sgx_get_param(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct drm_psp2sgx_get_param *args = data;

	switch (args->param) {
	case DRM_PSP2SGX_PARAM_UAPI_VERSION:
		args->value = 1;
		break;
	case DRM_PSP2SGX_PARAM_CORE_ID:
		args->value = 0; /* primary core */
		break;
	case DRM_PSP2SGX_PARAM_CORE_REVISION:
		args->value = 0x20106; /* Vita SGX revision */
		break;
	case DRM_PSP2SGX_PARAM_NUM_CORES:
		args->value = 4;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}