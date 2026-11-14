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
 * Mesa gallium driver initialization for psp2sgx
 *
 * This module registers the psp2sgx gallium driver with the Mesa
 * gallium core, providing OpenGL ES support for the Vita SGX543MP4+.
 */

#include "psp2sgx_gallium.h"
#include "dri/auxmesa.h"
#include "util/u_debug.h"
#include "util/u_helpers.h"

/*
 * psp2sgx_driver - gallium driver declaration
 *
 * This structure declares the psp2sgx gallium driver to the Mesa core.
 * It specifies the driver's name, screen creation function, and
 * supported capabilities.
 */
static const struct gallium_driver psp2sgx_driver = {
	.name = "psp2sgx",
	.screen_new = psp2sgx_screen_new,
	.screen_del = psp2sgx_screen_del,
	.get_param = psp2sgx_get_param,
	.get_string = _mesa_get_driver_string,
	.get_vendor = _mesa_get_vendor_string,
	.version = 1,
	/* Supported OpenGL ES versions */
	.es_version = 3,
	/* Capabilities */
	.capabilities = 0,
	/* No winsys by default - uses kernel DRM */
	.winsys_name = NULL,
};

/*
 * psp2sgx_init - initialize the psp2sgx gallium driver
 *
 * Register the psp2sgx driver with the Mesa gallium core.
 * Called during Mesa server/driver initialization.
 *
 * Return: 0 on success, -1 on failure.
 */
int
psp2sgx_init(void)
{
	static bool initialized = false;

	if (initialized)
		return 0;

	initialized = true;

	/* Register the driver with the gallium core */
	galium_driver_register(&psp2sgx_driver);

	mesa_logi("psp2sgx: gallium driver registered\n");
	mesa_logi("psp2sgx: SGX543MP4+ OpenGL ES %d support\n", psp2sgx_driver.es_version);

	return 0;
}

/*
 * psp2sgx_fini - cleanup the psp2sgx gallium driver
 *
 * Unregister the psp2sgx driver from the Mesa gallium core.
 * Called during Mesa driver cleanup.
 */
void
psp2sgx_fini(void)
{
	/* Nothing to do - driver is registered statically */
}

/*
 * _init - called by Mesa when the driver is loaded
 *
 * Standard Mesa driver initialization entry point.
 */
void
_init(void)
{
	psp2sgx_init();
}

/*
 * _fini - called by Mesa when the driver is unloaded
 *
 * Standard Mesa driver cleanup entry point.
 */
void
_fini(void)
{
	psp2sgx_fini();
}