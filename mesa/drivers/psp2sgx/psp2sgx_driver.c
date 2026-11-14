// Copyright © 2026 AndroidOnVita
//
// Mesa gallium driver for PlayStation Vita SGX543MP4+
//
// Driver registration and entry points.

#include "psp2sgx_gallium.h"

static int psp2sgx_driver_inited = 0;

/* Driver initialization */
int psp2sgx_init(void)
{
	if (psp2sgx_driver_inited)
		return 0;

	psp2sgx_driver_inited = 1;
	return 0;
}

/* Driver cleanup */
void psp2sgx_fini(void)
{
	psp2sgx_driver_inited = 0;
}

/* Get driver parameters */
int psp2sgx_get_param(int param, int *value)
{
	if (!value)
		return -22; /* EINVAL */

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
