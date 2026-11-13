// SPDX-License-Identifier: GPL-2.0-only */
/*
 * Vita SGX543MP4+ PDS programs for the microkernel.
 *
 * A PDS program in device memory is its data segment followed by its code.
 * The data segment is made of 16-byte units; each unit holds two words of
 * DS0 and then two words of DS1, so DS0 word n sits at byte
 * (n / 2) * 16 + (n % 2) * 4 and DS1 word n eight bytes further.  The code
 * is a sequence of 32-bit instructions.  The size registers and the emitpds
 * task word count the data segment in units.
 *
 * Only the MOVS forms the driver needs are here: DOUTA (a word into a
 * primary attribute of the task), DOUTU (start the USSE task) and HALT.  The
 * layout follows the hwdefs' EURASIA_PDS_MOVS_* fields with the extended
 * sources of the SGX543; every word a builder makes matches one of the event
 * programs of Sony's gpuinit_es4, which a bare-metal probe ran on the DevKit.
 */
#include <linux/types.h>

#include "psp2sgx_pds.h"

#define PDS_CC_ALWAYS		0x7f000000u
#define PDS_SRC1SEL_SHIFT	23
#define PDS_SRC1_SHIFT		18
#define PDS_SRC2SEL_SHIFT	17
#define PDS_SRC2_SHIFT		12
#define PDS_SWIZ0_SHIFT		10
#define PDS_SWIZ1_SHIFT		8
#define PDS_SWIZ2_SHIFT		6
#define PDS_SWIZ3_SHIFT		4

u32 sgx_pds_movs(unsigned int dest, unsigned int src1_sel, unsigned int src1,
		 unsigned int src2_sel, unsigned int src2,
		 unsigned int swiz0, unsigned int swiz1, unsigned int swiz2,
		 unsigned int swiz3)
{
	/* instruction class MOV (0) and type MOVS (0) are both zero */
	return PDS_CC_ALWAYS |
	       (src1_sel & 1) << PDS_SRC1SEL_SHIFT |
	       (src1 & 0x1f) << PDS_SRC1_SHIFT |
	       (src2_sel & 1) << PDS_SRC2SEL_SHIFT |
	       (src2 & 0x1f) << PDS_SRC2_SHIFT |
	       (swiz0 & 3) << PDS_SWIZ0_SHIFT |
	       (swiz1 & 3) << PDS_SWIZ1_SHIFT |
	       (swiz2 & 3) << PDS_SWIZ2_SHIFT |
	       (swiz3 & 3) << PDS_SWIZ3_SHIFT |
	       (dest & 0xf);
}

void sgx_pds_put_ds0(u32 *prog, unsigned int n, u32 value)
{
	prog[(n / 2) * 4 + n % 2] = value;
}

void sgx_pds_put_ds1(u32 *prog, unsigned int n, u32 value)
{
	prog[(n / 2) * 4 + 2 + n % 2] = value;
}

/* DOUTA's second word: the primary attribute the first word goes to. */
static u32 douta_pa(unsigned int pa)
{
	return pa << 8;
}

void sgx_pds_event_program(u32 *prog, u32 word, u32 doutu0)
{
	u32 *code = prog + SGX_PDS_EVENT_UNITS * 4;
	unsigned int n;

	for (n = 0; n < SGX_PDS_EVENT_UNITS * 4; n++)
		prog[n] = 0;
	sgx_pds_put_ds0(prog, 0, douta_pa(0));
	sgx_pds_put_ds0(prog, 1, douta_pa(1));
	sgx_pds_put_ds0(prog, 2, word);
	sgx_pds_put_ds0(prog, 3, douta_pa(2));
	sgx_pds_put_ds0(prog, 4, doutu0);
	sgx_pds_put_ds0(prog, 5, 1);		/* DOUTU1: once per instance */
	/* ds1[0] = 0: DOUTU2 */

	/* pa0 = IR0 (control in ds0[0]), pa1 = IR1 (control in ds0[1]) */
	code[0] = sgx_pds_movs(SGX_PDS_DOUTA, SGX_PDS_SRC_DS, 0,
			       SGX_PDS_SRC_REG, SGX_PDS_REG_IR0,
			       SGX_PDS_SRC1L, SGX_PDS_SRC1H, 0, 0);
	code[1] = sgx_pds_movs(SGX_PDS_DOUTA, SGX_PDS_SRC_DS, 0,
			       SGX_PDS_SRC_REG, SGX_PDS_REG_IR1,
			       SGX_PDS_SRC2L, SGX_PDS_SRC1H, 0, 0);
	/*
	 * pa2 = word.  Sony's program issues this DOUTA three times and the
	 * bare-metal runs kept that; so do we until a DevKit run shows one is
	 * enough.
	 */
	for (n = 2; n < 5; n++)
		code[n] = sgx_pds_movs(SGX_PDS_DOUTA, SGX_PDS_SRC_DS, 1,
				       SGX_PDS_SRC_DS, 0, SGX_PDS_SRC1L,
				       SGX_PDS_SRC1H, 0, 0);
	code[5] = sgx_pds_movs(SGX_PDS_DOUTU, SGX_PDS_SRC_DS, 2,
			       SGX_PDS_SRC_DS, 0,
			       SGX_PDS_SRC1L, SGX_PDS_SRC1H,
			       SGX_PDS_SRC2L, 0);
	code[6] = SGX_PDS_HALT;
}

void sgx_pds_doutu_program(u32 *prog, u32 doutu0, u32 doutu1, u32 doutu2)
{
	u32 *code = prog + SGX_PDS_DOUTU_UNITS * 4;

	prog[0] = prog[1] = prog[2] = prog[3] = 0;
	sgx_pds_put_ds0(prog, 0, doutu0);
	sgx_pds_put_ds0(prog, 1, doutu1);
	sgx_pds_put_ds1(prog, 0, doutu2);
	code[0] = sgx_pds_movs(SGX_PDS_DOUTU, SGX_PDS_SRC_DS, 0,
			       SGX_PDS_SRC_DS, 0,
			       SGX_PDS_SRC1L, SGX_PDS_SRC1H,
			       SGX_PDS_SRC2L, 0);
	code[1] = SGX_PDS_HALT;
}
