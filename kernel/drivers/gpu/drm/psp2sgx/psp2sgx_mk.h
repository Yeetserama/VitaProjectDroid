// SPDX-License-Identifier: GPL-2.0-only */
/*
 * Vita SGX543MP4+ microkernel header.
 *
 * The microkernel is our own USSE code, assembled at probe time,
 * with the device virtual addresses it needs written straight into its
 * immediates.  Only the instructions needed by the microkernel are here;
 * the floating point forms live in the userspace compiler.
 *
 * Register numbers are those the hardware uses for 32-bit words: r0..r127
 * temporaries (the top four alias the internal registers and are not
 * accepted), pa0..pa127, sa0..sa127, o0..o127.
 */
#ifndef PSP2SGX_MK_H
#define PSP2SGX_MK_H

#include <linux/types.h>

#include "psp2sgx_usse.h"

/* Byte offsets within the kernel region (kregion) for the host control
 * block and the host post block.  The hostctl block is SGX_HC_WORDS u32
 * values (64 bytes) starting at offset 0; the hostpost block follows it.
 */
#define SGX_MK_HOSTCTL 0
#define SGX_MK_HOSTPOST 64
#define SGX_MK_CODE_BASE 0	/* code base window index for microkernel */
#define SGX_MK_REGION_USED 256

/* The microkernel's block.  Only the microkernel writes it while the GPU
 * runs; the host reads it after an SLC flush, and writes it only at probe
 * and after a reset, with the GPU idle.
 */
enum sgx_mk_hostctl {
	SGX_HC_STATE,		/* microkernel: RUNNING, IDLE */
	SGX_HC_CLAIMED,		/* microkernel: the last post it took */
	SGX_HC_DONE_SEQNO,	/* microkernel: the last post it finished */
	SGX_HC_ERROR,		/* microkernel: 0, or SGX_MK_ERR_* | register */
	SGX_HC_RUNS,		/* microkernel: runs that took the lock */
	/*
	 * microkernel: EVENT_STATUS in bank 0, the master and core 0 when the
	 * last wait for TA_FINISHED, then for any other event, met or timed out
	 */
	SGX_HC_DIAG_TA,
	SGX_HC_DIAG_EVENT = SGX_HC_DIAG_TA + 3,
	/*
	 * microkernel: the poll rounds the last TA and other event wait had
	 * left, 0 after a timeout
	 */
	SGX_HC_POLLS_TA = SGX_HC_DIAG_EVENT + 3,
	SGX_HC_POLLS_EVENT,
	/* microkernel: EVENT_STATUS of cores 1 to 3 when an event wait timed out */
	SGX_HC_DIAG_CORES,
	SGX_HC_WORDS = 16,
};

/* The host's block: the GPU never writes it, so it is never written back */
enum sgx_mk_hostpost {
	SGX_HP_POSTED,		/* host: the sequence number of the posted run */
	SGX_HP_WORDS = 16,
};

#define SGX_MK_IDLE		0
#define SGX_MK_RUNNING		2

#define SGX_MK_ERR_WAIT_TIMEOUT	0x80000000u

/* Registers the microkernel keeps across job code */
#define SGX_MK_R_HOSTCTL	0

/* Assembles the microkernel for a kernel region at kregion_va. */
int sgx_mk_build(u64 *code, u32 cap, u32 *len, u32 kregion_va);

/* Job code */
struct sgx_mk_job {
	struct sgx_usse_asm a;
};

void sgx_mk_job_begin(struct sgx_mk_job *j, u64 *buf, u32 cap);
/* SGX register `reg` (byte offset) = value */
void sgx_mk_job_write(struct sgx_mk_job *j, u32 reg, u32 value);
/*
 * Poll until (register & mask) == value, at most max_polls rounds; on a
 * timeout record SGX_MK_ERR_WAIT_TIMEOUT | reg and return to the
 * microkernel, skipping the rest of the job.  A wait on EVENT_STATUS in
 * bank 0 or the master is met by the bits in any of bank 0, the master and
 * core 0, which of them latch the TA and 3D events on the hardware, and
 * leaves the three in the host control block's DIAG words.  A wait on
 * EVENT_STATUS2 in bank 0 is met by bank 0 or core 0.
 */
void sgx_mk_job_wait(struct sgx_mk_job *j, u32 reg, u32 mask, u32 value,
		     u32 max_polls);
/*
 * Extra register reads per poll round of every later wait: each read's
 * data fence deschedules the microkernel's thread, which leaves core 0's
 * pipe to the render's own threads (psp2sgx.wait_yield).
 */
extern unsigned int sgx_mk_wait_yield;
/*
 * Emit a PDS task and wait for it to signal core 0's USE_GLOBCOM1 bit 0, as
 * Sony's microkernel does.
 */
void sgx_mk_job_task(struct sgx_mk_job *j, u32 task_word, u32 pds_addr,
		     u32 ir0, u32 sideband);
int sgx_mk_job_end(struct sgx_mk_job *j);

#endif
