// Copyright © 2026 AndroidOnVita
//
// Mesa gallium driver for PlayStation Vita SGX543MP4+
//
// Minimal compilable header - uses stub headers and standard C.
// Full Mesa integration requires the complete Mesa 3D source tree.

#ifndef PSP2SGX_GALLIUM_H
#define PSP2SGX_GALLIUM_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#define PSP2SGX_USSE_REGs 128
#define PSP2SGX_PDS_PROG_OFFSET 0x240
#define PSP2SGX_CODE_BASE 4
#define PSP2SGX_KN_REGION_VA 0xf0000000

/* Forward declarations for gallium core types */
struct gallium_screen;
struct gallium_context;
struct gallium_buffer;
struct gallium_program;
struct gl_shader;

struct psp2sgx_screen {
	struct gallium_screen *screen;
	void *drm_file;
	void *kb;
	uint32_t code_base;
	bool self_test_passed;
};

struct psp2sgx_context {
	struct gallium_context *base;
	uint32_t dirtily;
};

struct psp2sgx_buffer {
	struct gallium_buffer *base;
	uint32_t handle;
	uint32_t size;
	uint32_t flags;
	void *cpu_addr;
	uintptr_t dma_addr;
};

struct psp2sgx_program {
	struct gallium_program *base;
	uint32_t *usse_code;
	uint32_t usse_size;
	uint32_t *pds_code;
	uint32_t pds_size;
	uint32_t code_base;
	uint32_t kicker;
};

/* Driver functions */
void *psp2sgx_screen_new(int drm_fd);
void psp2sgx_screen_del(void *screen_);
int psp2sgx_compile_shader(void *screen, struct gl_shader *templ,
                           struct psp2sgx_program *prog);
int psp2sgx_emit_ussse(struct psp2sgx_program *prog, uint32_t inst);
int psp2sgx_launch_job(void *ctx, void *buf,
                       struct psp2sgx_program *prog,
                       int x, int y, int width, int height);
void psp2sgx_destroy_screen(void *screen_);

/* Driver init/cleanup */
int psp2sgx_init(void);
void psp2sgx_fini(void);

/* Parameter getter */
int psp2sgx_get_param(int param, int *value);

#endif /* PSP2SGX_GALLIUM_H */
