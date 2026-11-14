// Copyright © 2026 AndroidOnVita
//
// Mesa gallium driver for PlayStation Vita SGX543MP4+
//
// Minimal compilable version - uses only stub headers and standard C.
// Full Mesa integration requires the complete Mesa 3D source tree.

#include "psp2sgx_gallium.h"
#include "compiler/shader_enums.h"
#include "util/u_memory.h"
#include "util/u_helpers.h"

/* Minimal screen creation - just allocate and initialize */
void *psp2sgx_screen_new(int drm_fd)
{
	struct psp2sgx_screen *screen = calloc(1, sizeof(*screen));
	if (!screen)
		return NULL;

	screen->screen = NULL;
	screen->drm_file = NULL;
	screen->kb = NULL;
	screen->code_base = PSP2SGX_CODE_BASE;
	screen->self_test_passed = false;

	return screen;
}

void psp2sgx_screen_del(void *screen_)
{
	struct psp2sgx_screen *screen = (struct psp2sgx_screen *)screen_;

	if (screen->kb)
		free(screen->kb);
	free(screen);
}

/* Minimal shader compilation - stores that shader was compiled */
int psp2sgx_compile_shader(void *screen, struct gl_shader *templ,
                           struct psp2sgx_program *prog)
{
	if (!prog || !templ)
		return -22; /* EINVAL */

	prog->usse_size = 0;
	prog->usse_code = NULL;

	prog->pds_size = 0;
	prog->pds_code = NULL;

	prog->code_base = PSP2SGX_CODE_BASE;
	prog->kicker = 1;

	return 0;
}

int psp2sgx_emit_ussse(struct psp2sgx_program *prog, uint32_t inst)
{
	if (!prog)
		return -22;
	return 0;
}

int psp2sgx_launch_job(void *ctx, void *buf,
                       struct psp2sgx_program *prog,
                       int x, int y, int width, int height)
{
	if (!ctx || !prog)
		return -22;
	return 0;
}

void psp2sgx_destroy_screen(void *screen_)
{
	struct psp2sgx_screen *screen = (struct psp2sgx_screen *)screen_;

	if (screen->kb)
		free(screen->kb);
	free(screen);
}

/* Minimal driver init */
static int psp2sgx_driver_inited = 0;

int psp2sgx_init(void)
{
	if (psp2sgx_driver_inited)
		return 0;

	psp2sgx_driver_inited = 1;
	return 0;
}

void psp2sgx_fini(void)
{
	psp2sgx_driver_inited = 0;
}

/* Standard library constructor/destructor */
void _init(void)
{
	psp2sgx_init();
}

void _fini(void)
{
	psp2sgx_fini();
}

/* Minimal param getter */
int psp2sgx_get_param(int param, int *value)
{
	if (!value)
		return -22;

	switch (param) {
	case 1: /* DRM_PSP2SGX_PARAM_UAPI_VERSION */
		*value = 1;
		break;
	case 2: /* DRM_PSP2SGX_PARAM_CORE_ID */
		*value = 0;
		break;
	case 3: /* DRM_PSP2SGX_PARAM_CORE_REVISION */
		*value = 0x20106;
		break;
	case 4: /* DRM_PSP2SGX_PARAM_NUM_CORES */
		*value = 4;
		break;
	default:
		return -22;
	}
	return 0;
}
