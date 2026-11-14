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
 * Mesa gallium driver for PlayStation Vita SGX543MP4+
 *
 * This driver compiles GLSL/ESSL shaders to USSE/PDS code for the
 * Vita's SGX543MP4+ GPU and submits them via the psp2sgx kernel DRM driver.
 *
 * See docs/gpu-research.md for bring-up status.
 */

#ifndef PSP2SGX_GALLIUM_H
#define PSP2SGX_GALLIUM_H

#include "compiler/shader_enums.h"
#include "util/u_math.h"
#include "util/u_vertex_buffer.h"
#include "util/u_primitive.h"

/*
 * The SGX has 4 USSE cores and a PDS (Program Data Store) for microcode.
 * Each core has 128 general registers (r0-r127) and 4 internal registers
 * (r124-r127). The PDS program starts at kregion + 0x240.
 */

#define PSP2SGX_MAX_COREs	4
#define PSP2SGX_USSE_REGS	128
#define PSP2SGX_PDS_PROG_OFFSET	0x240
#define PSP2SGX_CODE_BASE	4
#define PSP2SGX_KN_REGION_VA	0xf0000000

/*
 * USSE instruction encoding - follows Imagination hwdefs
 * and Vita3K decoder patterns
 */
#define USSE_OPCODE_MASK	0xFF000000
#define USSE_OPCODE_SHIFT	24
#define USSE_MOD_MASK		0x00FF0000
#define USSE_MOD_SHIFT	16
#define USSE_SRC1_MASK		0x000F8000
#define USSE_SRC1_SHIFT	15
#define USSE_SRC2_MASK		0x00007C00
#define USSE_SRC2_SHIFT	10
#define USSE_DEST_MASK		0x000003FF
#define USSE_DEST_SHIFT	0

/* MOV immediate */
#define USSE_MOV_IMM(imm)	(0x01000000 | ((imm) & 0xFF))

/* LIMM - load immediate */
#define USSE_LIMM(imm)		(0x02000000 | ((imm) & 0xFF))

/* ADD */
#define USSE_ADD(dest, src1, src2) \
	(0x05000000 | (dest) << 16 | (src1) << 10 | (src2))

/* MUL */
#define USSE_MUL(dest, src1, src2) \
	(0x06000000 | (dest) << 16 | (src1) << 10 | (src2))

/* SIN/COS approximations */
#define USSE_SIN(dest, src) \
	(0x07000000 | (dest) << 16 | (src))

/* Helper: encode a src1 reading r0..r127 */
#define USSE_SRC1(reg)		((reg) << 18)

/* Helper: encode a src2 reading r0..r127 */
#define USSE_SRC2(reg)		((reg) << 12)

/* Helper: encode a dest writing r0..r127 */
#define USSE_DEST(reg)		((reg) << 2)

/* HALT instruction */
#define USSE_HALT		0x00000000

/*
 * psp2sgx_screen - screen context for the gallium driver
 */
struct psp2sgx_screen {
	struct gallium_screen screen;
	struct psp2sgx_device *sgx;
	struct drm_file *drm_file;
	struct pb_buffer *kb; /* kernel region buffer */
	uint32_t code_base;
	bool self_test_passed;
};

/*
 * psp2sgx_context - context management
 */
struct psp2sgx_context {
	struct gallium_context base;
	struct psp2sgx_screen *screen;
	uint32_t dirtily;
};

/*
 * psp2sgx_buffer - buffer object management
 */
struct psp2sgx_buffer {
	struct gallium_buffer base;
	uint32_t handle;
	uint32_t size;
	uint32_t flags;
	void *cpu_addr;
	dma_addr_t dma_addr;
};

/*
 * psp2sgx_program - compiled shader program (USSE + PDS)
 */
struct psp2sgx_program {
	struct gallium_program base;
	uint32_t *usse_code;    /* assembled USSE code */
	uint32_t usse_size;     /* number of USSE instructions */
	uint32_t *pds_code;     /* PDS program code */
	uint32_t pds_size;      /* number of PDS units */
	uint32_t code_base;     /* code base index */
	uint32_t kicker;        /* kicker word value */
};

/*
 * psp2sgx_compile_shader - compile GLSL/ESSL to USSE/PDS
 * @screen: screen context
 * @templ: shader template from GLSL compiler
 * @prog: output program structure
 *
 * Return: 0 on success, error code on failure.
 */
int psp2sgx_compile_shader(struct gallium_screen *screen,
                           struct gl_shader *templ,
                           struct psp2sgx_program *prog);

/*
 * psp2sgx_validate_state - validate context state
 * @ctx: context to validate
 *
 * Return: 0 on success, error code on failure.
 */
int psp2sgx_validate_state(struct gallium_context *ctx);

/*
 * psp2sgx_emit_ussse - emit USSE instructions to a buffer
 * @prog: program to emit into
 * @inst: instruction to emit
 *
 * Return: 0 on success, error code on failure.
 */
int psp2sgx_emit_ussse(struct psp2sgx_program *prog, uint32_t inst);

/*
 * psp2sgx_launch_job - launch a shader job on the SGX
 * @ctx: gallium context
 * @buf: buffer containing vertex data
 * @prog: compiled shader program
 * @x, @y, @width, @height: viewport dimensions
 *
 * Return: 0 on success, error code on failure.
 */
int psp2sgx_launch_job(struct gallium_context *ctx,
                       struct gallium_buffer *buf,
                       struct psp2sgx_program *prog,
                       int x, int y, int width, int height);

/*
 * psp2sgx_destroy_screen - destroy the screen context
 */
void psp2sgx_destroy_screen(struct gallium_screen *screen);

/*
 * psp2sgx_get_param - get driver parameters via ioctl
 */
int psp2sgx_get_param(struct drm_device *drm, void *data, struct drm_file *file);

#endif /* PSP2SGX_GALLIUM_H */