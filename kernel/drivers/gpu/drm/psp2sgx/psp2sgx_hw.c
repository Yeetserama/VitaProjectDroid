// SPDX-License-Identifier: GPL-2.0-only */
/*
 * Vita SGX543MP4+ GPU driver - hardware power, start-up, initialisation,
 * caches and the kick.
 *
 * The sequences are those a bare-metal SGX probe ran on the DevKit, which
 * in turn replay what SceGpuEs4 and gpuinit_es4 write on a genuine resume
 * (an MMIO trace).  Anything the probe did not run is marked as such.
 */
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/moduleparam.h>
#include <linux/sizes.h>

#include "psp2sgx_drv.h"
#include "psp2sgx_mk.h"
#include "psp2sgx_pds.h"
#include "psp2sgx_usse.h"

/* psp2sgx.cores: number of SGX cores to use (1-4, default: all four) */
static unsigned int cores = PSP2SGX_NUM_CORES;
module_param(cores, uint, 0444);
MODULE_PARM_DESC(cores, "Number of SGX cores to use (1-4)");

/*
 * The GPU power domain belongs to the secure world: SceGpuEs4 switches it
 * with SMC 0x110 (bus 0 = GPU, 1 = start, 0 = stop), and the monitor then
 * programs the Pervasive registers.  Whether the monitor is still resident
 * depends on how Linux was started, so the device tree says whether to call
 * it (sony,power-smc).  Without the monitor, drive the same registers
 * directly, replaying GpuPowerOn from Sony's secure Pervasive driver
 * (pervasive_tzs.c): isolate off, bus idle, compat RAM gate, GPU gate mask
 * 0xf000f, power clock, reset release.  Writing only the reset and gate bit
 * 0 leaves the core clocks off and the first SGX read aborts.
 */
#define PERVASIVE_MISC_BASE	0xe3100000
#define PERVASIVE_RESET_BASE	0xe3101000
#define PERVASIVE_GATE_BASE	0xe3102000
#define PERVASIVE_BASECLK_BASE	0xe3103000
#define PERVASIVE_GPU_OFFSET	0x10
#define PERVASIVE_COMPATRAM_OFFSET	0x28

#define PERVASIVE_MISC_GPU_ISOLATION	0x34
#define PERVASIVE_MISC_COMPATRAM_ISOLATION	0x3c
#define PERVASIVE_MISC_GPU_BUS_IDLE	0x54
#define PERVASIVE_MISC_BUS_IDLE_STATUS	0xc0

#define PERVASIVE_BASECLK_GPU	0x10
#define PERVASIVE_BASECLK_ALLEGREX	0x1e0
#define PERVASIVE_GPU_POWER_CLOCK	0x4000000	/* allegrexClock bit 26 */
#define PERVASIVE_GPU_GATE_MASK	0xf000f

static u32 psp2sgx_smc_bus(u32 service, u32 bus, u32 command)
{
	register u32 r0 asm("r0") = bus;
	register u32 r1 asm("r1") = command;
	register u32 r12 asm("r12") = service;

	asm volatile(".arch_extension sec\n\tsmc #0"
		     : "+r"(r0) : "r"(r1), "r"(r12) : "r2", "r3", "memory");
	return r0;
}

/*
 * SceGpuEs4's SGXStart after the power-up: master clocks and SLC set-up,
 * master soft reset with the BIF set up during it, core clock gating.  The
 * genuine resume writes exactly these before its first SGX read; any read
 * before them hangs the bus.  Its delays are 100 SGX clocks.
 */
static void psp2sgx_start(struct psp2sgx_device *sgx)
{
	psp2sgx_dbg(sgx, "start: master core and clock gates\n");
	sgx_write(sgx, SGX_MASTER_CORE, clamp(cores, 1u, 4u) - 1);
	sgx_write(sgx, SGX_MASTER_CLKGATECTL2, 0x2a9);
	sgx_write(sgx, SGX_MASTER_CLKGATECTL, 0xaa);
	psp2sgx_dbg(sgx, "start: master soft reset\n");
	sgx_write(sgx, SGX_MASTER_SOFT_RESET, 0x7ff);
	wmb();
	udelay(10);
	psp2sgx_dbg(sgx, "start: BIF and SLC\n");
	sgx_write(sgx, SGX_MASTER_BIF_CTRL, 0);
	sgx_write(sgx, SGX_MASTER_SLC_CTRL, 0x4c800);
	sgx_write(sgx, SGX_MASTER_SLC_CTRL_BYPASS, 0x4000000);
	wmb();
	udelay(10);
	psp2sgx_dbg(sgx, "start: release reset\n");
	sgx_write(sgx, SGX_MASTER_SOFT_RESET, 0);
	wmb();
	udelay(10);
	psp2sgx_dbg(sgx, "start: core clock gates\n");
	sgx_write(sgx, SGX_CLKGATECTL, 0x2aaaaa);
	sgx_write(sgx, SGX_CLKGATECTL2, 0xa8a8aaa);
	wmb();
	udelay(10);
	psp2sgx_dbg(sgx, "start: done\n");
}

unsigned int psp2sgx_hw_cores(void)
{
	return cores;
}

static int psp2sgx_hw_build_event_pds(struct psp2sgx_device *sgx);

int psp2sgx_hw_build_kregion(struct psp2sgx_device *sgx)
{
	u32 len = 0;
	int err;

	err = sgx_mk_build((u64 *)((u8 *)sgx->kregion + SGX_MK_CODE),
			   (sgx->kregion_size - SGX_MK_CODE) / sizeof(u64),
			   &len, SGX_KREGION_VA);
	if (err)
		return err;
	dev_info(sgx->dev, "microkernel: %u instructions\n", len);
	err = psp2sgx_hw_build_event_pds(sgx);
	if (err)
		return err;
	return 0;
}

/* The event PDS program the kick runs: it starts one microkernel task at
 * code base SGX_MK_CODE_BASE, instruction index 0.  pa2 (the kick word) is
 * zero for now; the microkernel reads hostctl from its own address. */
static int psp2sgx_hw_build_event_pds(struct psp2sgx_device *sgx)
{
	u32 *prog = (u32 *)((u8 *)sgx->kregion + SGX_MK_EVENT_PDS);
	u32 doutu0 = sgx_pds_doutu0(SGX_MK_CODE_BASE, 0, 8);

	sgx_pds_event_program(prog, 0, doutu0);
	wmb();
	return 0;
}

static int psp2sgx_perv_map(struct psp2sgx_device *sgx)
{
	if (!sgx->perv_misc) {
		sgx->perv_misc = devm_ioremap(sgx->dev, PERVASIVE_MISC_BASE,
					      SZ_4K);
		if (!sgx->perv_misc)
			return -ENOMEM;
	}
	if (!sgx->perv_reset) {
		sgx->perv_reset = devm_ioremap(sgx->dev, PERVASIVE_RESET_BASE,
					       SZ_4K);
		if (!sgx->perv_reset)
			return -ENOMEM;
	}
	if (!sgx->perv_gate) {
		sgx->perv_gate = devm_ioremap(sgx->dev, PERVASIVE_GATE_BASE,
					      SZ_4K);
		if (!sgx->perv_gate)
			return -ENOMEM;
	}
	if (!sgx->perv_baseclk) {
		sgx->perv_baseclk = devm_ioremap(sgx->dev, PERVASIVE_BASECLK_BASE,
						 SZ_4K);
		if (!sgx->perv_baseclk)
			return -ENOMEM;
	}
	return 0;
}

/*
 * Log every Pervasive word the power sequence touches, so a boot log shows
 * which stage did not take effect if the SGX read still faults.
 */
static void psp2sgx_perv_log(struct psp2sgx_device *sgx, const char *stage)
{
	dev_info(sgx->dev,
		 "pervasive %s: misc iso %#x/%#x busidle %#x status %#x "
		 "baseclk %#x/%#x reset %#x gate %#x/%#x\n",
		 stage,
		 readl(sgx->perv_misc + PERVASIVE_MISC_GPU_ISOLATION),
		 readl(sgx->perv_misc + PERVASIVE_MISC_COMPATRAM_ISOLATION),
		 readl(sgx->perv_misc + PERVASIVE_MISC_GPU_BUS_IDLE),
		 readl(sgx->perv_misc + PERVASIVE_MISC_BUS_IDLE_STATUS),
		 readl(sgx->perv_baseclk + PERVASIVE_BASECLK_GPU),
		 readl(sgx->perv_baseclk + PERVASIVE_BASECLK_ALLEGREX),
		 readl(sgx->perv_reset + PERVASIVE_GPU_OFFSET),
		 readl(sgx->perv_gate + PERVASIVE_GPU_OFFSET),
		 readl(sgx->perv_gate + PERVASIVE_COMPATRAM_OFFSET));
}

static void psp2sgx_perv_set(void __iomem *page, u32 offset, u32 mask, bool on)
{
	u32 value = readl(page + offset);

	if (on)
		value |= mask;
	else
		value &= ~mask;
	writel(value, page + offset);
	/* order the write before the next register access */
	(void)readl(page + offset);
}

/*
 * GpuBusIdle from Sony's driver: ask the bus to idle, wait for the request
 * to be taken and reported done, then acknowledge it.  The monitor spins
 * forever; bound each wait so a stuck bus cannot hang the probe thread.
 */
static int psp2sgx_perv_bus_idle(struct psp2sgx_device *sgx)
{
	void __iomem *request = sgx->perv_misc + PERVASIVE_MISC_GPU_BUS_IDLE;
	void __iomem *status = sgx->perv_misc + PERVASIVE_MISC_BUS_IDLE_STATUS;
	u32 value;
	int err;

	writel(1, request);
	err = readl_poll_timeout_atomic(request, value, !(value & 1), 1,
					10000);
	if (err) {
		dev_warn(sgx->dev, "power: bus idle request stuck at %#x\n",
			 value);
		return err;
	}
	err = readl_poll_timeout_atomic(status, value, value & 2, 1, 10000);
	if (err) {
		dev_warn(sgx->dev, "power: bus idle not reported, status %#x\n",
			 value);
		return err;
	}
	writel(2, status);
	err = readl_poll_timeout_atomic(status, value, !(value & 2), 1, 10000);
	if (err)
		dev_warn(sgx->dev, "power: bus idle ack stuck at %#x\n", value);
	return err;
}

int psp2sgx_hw_power_on(struct psp2sgx_device *sgx)
{
	int err;

	if (sgx->power_smc >= 0) {
		u32 ret = psp2sgx_smc_bus(sgx->power_smc, sgx->power_smc_bus, 1);

		if (ret)
			dev_warn(sgx->dev, "power SMC returned %#x\n", ret);
		usleep_range(1000, 2000);
	} else {
		err = psp2sgx_perv_map(sgx);
		if (err)
			return err;
		psp2sgx_dbg(sgx, "power: pervasive registers mapped\n");
		psp2sgx_perv_log(sgx, "before power-on");

		psp2sgx_dbg(sgx, "power: reset enable\n");
		psp2sgx_perv_set(sgx->perv_reset, PERVASIVE_GPU_OFFSET, 1, true);
		psp2sgx_dbg(sgx, "power: isolation disable\n");
		writel(0, sgx->perv_misc + PERVASIVE_MISC_GPU_ISOLATION);
		psp2sgx_dbg(sgx, "power: bus idle\n");
		err = psp2sgx_perv_bus_idle(sgx);
		if (err)
			dev_warn(sgx->dev, "power: bus idle failed: %d, continuing\n",
				 err);
		psp2sgx_dbg(sgx, "power: compat ram gate on\n");
		psp2sgx_perv_set(sgx->perv_gate, PERVASIVE_COMPATRAM_OFFSET, 1,
				 true);
		psp2sgx_dbg(sgx, "power: gate on\n");
		psp2sgx_perv_set(sgx->perv_gate, PERVASIVE_GPU_OFFSET,
				 PERVASIVE_GPU_GATE_MASK, true);
		psp2sgx_dbg(sgx, "power: compat ram gate off\n");
		psp2sgx_perv_set(sgx->perv_gate, PERVASIVE_COMPATRAM_OFFSET, 1,
				 false);
		psp2sgx_dbg(sgx, "power: gate off\n");
		psp2sgx_perv_set(sgx->perv_gate, PERVASIVE_GPU_OFFSET,
				 PERVASIVE_GPU_GATE_MASK, false);
		psp2sgx_dbg(sgx, "power: clock enable\n");
		psp2sgx_perv_set(sgx->perv_baseclk,
				 PERVASIVE_BASECLK_ALLEGREX,
				 PERVASIVE_GPU_POWER_CLOCK, true);
		psp2sgx_dbg(sgx, "power: reset disable\n");
		psp2sgx_perv_set(sgx->perv_reset, PERVASIVE_GPU_OFFSET, 1,
				 false);
		psp2sgx_dbg(sgx, "power: compat ram gate on\n");
		psp2sgx_perv_set(sgx->perv_gate, PERVASIVE_COMPATRAM_OFFSET, 1,
				 true);
		psp2sgx_dbg(sgx, "power: gate on\n");
		psp2sgx_perv_set(sgx->perv_gate, PERVASIVE_GPU_OFFSET,
				 PERVASIVE_GPU_GATE_MASK, true);

		psp2sgx_perv_log(sgx, "after power-on");
		msleep(10);
	}

	psp2sgx_dbg(sgx, "power: starting SGX\n");
	psp2sgx_start(sgx);
	psp2sgx_dbg(sgx, "power: reading core id\n");
	sgx->core_id = sgx_read(sgx, SGX_CORE_ID);
	sgx->core_revision = sgx_read(sgx, SGX_CORE_REVISION);
	psp2sgx_dbg(sgx, "power: core id %#x revision %#x\n", sgx->core_id,
		    sgx->core_revision);
	return 0;
}

void psp2sgx_hw_power_off(struct psp2sgx_device *sgx)
{
	sgx_write(sgx, SGX_EVENT_HOST_ENABLE, 0);
	sgx_write(sgx, SGX_EVENT_HOST_ENABLE2, 0);
	if (sgx->power_smc >= 0) {
		psp2sgx_smc_bus(sgx->power_smc, sgx->power_smc_bus, 0);
	} else if (sgx->perv_reset && sgx->perv_gate && sgx->perv_misc &&
		   sgx->perv_baseclk) {
		/* GpuPowerOff: reset hold, gates off, power clock off, isolate */
		psp2sgx_perv_set(sgx->perv_reset, PERVASIVE_GPU_OFFSET, 1, true);
		psp2sgx_perv_set(sgx->perv_gate, PERVASIVE_GPU_OFFSET,
				 PERVASIVE_GPU_GATE_MASK, false);
		psp2sgx_perv_set(sgx->perv_baseclk,
				 PERVASIVE_BASECLK_ALLEGREX,
				 PERVASIVE_GPU_POWER_CLOCK, false);
		writel(1, sgx->perv_misc + PERVASIVE_MISC_GPU_ISOLATION);
	}
}

static int poll_bits(struct psp2sgx_device *sgx, u32 reg, u32 bits, u32 *seen)
{
	u32 v;
	int err;

	err = readl_poll_timeout_atomic(sgx->regs + reg, v, (v & bits) == bits,
					1, 1000);
	if (seen)
		*seen = v;
	return err;
}

/*
 * Wait until no core's pipeline is clocked.  The master's end-of-job events
 * can come before every core is done (DevKit, 2026-09-24: TA_FINISHED early,
 * PIXELBE_END_RENDER only in core 0's bank), and job 3 then faulted on cores
 * 2 and 3 at the first page of a render target whose PTE was valid, as if
 * their accesses were still in flight when the next job switched the page
 * directory and dropped the TLBs.  Each wait that was needed is logged
 * (the first 16), with every core's clock status and reads in flight.
 */
int psp2sgx_hw_wait_cores_idle(struct psp2sgx_device *sgx, u32 seqno)
{
	static unsigned int reported;
	u32 before[PSP2SGX_NUM_CORES], reqs[PSP2SGX_NUM_CORES];
	bool busy = false;
	unsigned int n;
	int err = 0;
	ktime_t t0 = ktime_get();

	for (n = 0; n < PSP2SGX_NUM_CORES; n++) {
		before[n] = sgx_read(sgx, SGX_CORE_BANK(n) + SGX_CLKGATESTATUS);
		reqs[n] = sgx_read(sgx, SGX_CORE_BANK(n) + SGX_BIF_MEM_REQ_STAT);
		busy |= !!(before[n] & SGX_CLKGATESTATUS_PIPELINE);
	}
	if (!busy)
		return 0;
	for (n = 0; n < PSP2SGX_NUM_CORES && !err; n++) {
		u32 v;

		err = readl_poll_timeout(sgx->regs + SGX_CORE_BANK(n) + SGX_CLKGATESTATUS,
					 v, !(v & SGX_CLKGATESTATUS_PIPELINE), 2, 100000);
	}
	if (reported < 16 || err) {
		reported++;
		dev_warn(sgx->dev, "job %u: cores busy before the switch: clocks %08x %08x %08x %08x, reads %02x %02x %02x %02x; %s after %lld us, now %08x %08x %08x %08x\n",
			 seqno, before[0], before[1], before[2], before[3],
			 reqs[0] & 0xff, reqs[1] & 0xff, reqs[2] & 0xff, reqs[3] & 0xff,
			 err ? "still busy" : "idle",
			 ktime_us_delta(ktime_get(), t0),
			 sgx_read(sgx, SGX_CORE_BANK(0) + SGX_CLKGATESTATUS),
			 sgx_read(sgx, SGX_CORE_BANK(1) + SGX_CLKGATESTATUS),
			 sgx_read(sgx, SGX_CORE_BANK(2) + SGX_CLKGATESTATUS),
			 sgx_read(sgx, SGX_CORE_BANK(3) + SGX_CLKGATESTATUS));
	}
	return err;
}

/*
 * Write the SLC's dirty lines back and drop its lines, as the microkernel
 * does before the host reads what the GPU wrote (program A 0x1071).  USSE
 * stores are invisible to the CPU without it, and a program's last store is
 * no fence: the host flushes after it sees the completion interrupt.
 */
/*
 * One SLC request, acknowledged in MASTER_SLC_EVENT_STATUS.  The bit is
 * cleared before the request as well: one left set would end the wait at
 * once, with the request still going.  slc_lock keeps the IRQ thread's
 * write-back and the job path's flushes from taking each other's bits.
 */
static int slc_request(struct psp2sgx_device *sgx, u32 ctrl, u32 value, u32 event)
{
	int err;

	spin_lock(&sgx->slc_lock);
	sgx_write(sgx, SGX_MASTER_SLC_EVENT_CLEAR, event);
	sgx_write(sgx, ctrl, value);
	err = poll_bits(sgx, SGX_MASTER_SLC_EVENT_STATUS, event, NULL);
	sgx_write(sgx, SGX_MASTER_SLC_EVENT_CLEAR, event);
	spin_unlock(&sgx->slc_lock);
	return err;
}

/*
 * Write the SLC's dirty lines back and drop every line.  Only while no
 * render or microkernel run is going: an invalidate drops the
 * stores of a program that is running (seen on the DevKit).
 */
int psp2sgx_hw_slc_flush(struct psp2sgx_device *sgx)
{
	int err;

	err = slc_request(sgx, SGX_MASTER_SLC_CTRL_FLUSH, SGX_SLC_FLUSH_ALL,
			  SGX_SLC_EVENT_FLUSH);
	if (err)
		return err;
	return slc_request(sgx, SGX_MASTER_SLC_CTRL_INVAL, SGX_SLC_INVAL_ALL,
			   SGX_SLC_EVENT_INVAL);
}

/*
 * Write the dirty lines back and keep them: for the host to read what the
 * GPU stored while a TA or 3D pass may still be running, as after a run
 * that started one.
 */
int psp2sgx_hw_slc_writeback(struct psp2sgx_device *sgx)
{
	return slc_request(sgx, SGX_MASTER_SLC_CTRL_FLUSH, SGX_SLC_FLUSH_ALL,
			   SGX_SLC_EVENT_FLUSH);
}

/*
 * Drop the GPU's own caches of code and data the CPU has rewritten: the
 * USSE instruction cache, the texture and data caches and the PDS code and
 * data caches.  Sony's microkernel issues these writes itself (A 0x104f..
 * 0x105c, inside lock #7) and polls the PDS cache status; here the host
 * does it while the GPU is idle.  UNVERIFIED on the hardware from the CPU
 * side; the completion bits are those Sony's polls test.
 */
int psp2sgx_hw_invalidate_caches(struct psp2sgx_device *sgx)
{
	int err = 0;
	unsigned int n;

	sgx_write(sgx, SGX_DCU_ICTRL, 1);
	sgx_write(sgx, SGX_TCU_ICTRL, 1);
	sgx_write(sgx, SGX_USE_CACHE, 1);
	/* the other cores' data, texture and USSE code caches as well */
	for (n = 0; n < PSP2SGX_NUM_CORES; n++) {
		sgx_write(sgx, SGX_CORE_BANK(n) + SGX_DCU_ICTRL, 1);
		sgx_write(sgx, SGX_CORE_BANK(n) + SGX_TCU_ICTRL, 1);
		sgx_write(sgx, SGX_CORE_BANK(n) + SGX_USE_CACHE, 1);
	}
	/*
	 * Both PDS data caches: 0 before a TA, 1 before a 3D pass, as Sony's
	 * microkernel invalidates them (a: shell register trace, INV1 with
	 * TSP_PARAMETER_CACHE and host clear 5 before every render).  Every
	 * job's PDS data sits at the same address (0xd0000000), so a stale
	 * DSC1 line runs the last job's pixel programs, with its surface
	 * addresses (c: on the DevKit, cores 2 and 3 faulted at
	 * 0x01010000, a surface of the previous address space, while on the
	 * pixel event program).
	 */
	sgx_write(sgx, SGX_TSP_PARAMETER_CACHE, 1);
	sgx_write(sgx, SGX_PDS_INV0, 1);
	sgx_write(sgx, SGX_PDS_INV1, 1);
	sgx_write(sgx, SGX_PDS_INV_CSC, 1);
	if (poll_bits(sgx, SGX_PDS_CACHE_STATUS,
		      SGX_PDS_CACHE_CSC_INV | SGX_PDS_CACHE_DSC_INV0 |
		      SGX_PDS_CACHE_DSC_INV1, NULL))
		err = -ETIMEDOUT;
	sgx_write(sgx, SGX_PDS_CACHE_HOST_CLEAR,
		  SGX_PDS_CACHE_CSC_INV | SGX_PDS_CACHE_DSC_INV0 |
		  SGX_PDS_CACHE_DSC_INV1);
	return err;
}

/*
 * Point every requestor at vm's page directory and drop the BIF's TLBs.
 */
void psp2sgx_hw_set_vm(struct psp2sgx_device *sgx, struct psp2sgx_vm *vm)
{
	unsigned int n;

	if (sgx->current_vm != vm) {
		psp2sgx_vm_put(sgx->current_vm);
		sgx->current_vm = psp2sgx_vm_get(vm);
	}
	sgx_write(sgx, SGX_BIF_DIR_LIST_BASE(0), vm->pd_dma);
	sgx_write(sgx, SGX_BIF_DIR_LIST_BASE(7), vm->pd_dma);
	/*
	 * as the microkernel invalidates (A 0x0c89: str [0xc34] = #8), from 0:
	 * the register reads back 8 afterwards, so rewriting 8 may not start
	 * another invalidate.  On the DevKit cores 2 and 3 faulted at a page
	 * whose PTE was valid (BIF_FAULT 0x01010000, PTE 0x7a677001) after
	 * the directory and 8 had been written to every bank.
	 */
	sgx_write(sgx, SGX_BIF_CTRL_INVAL, 0);
	sgx_write(sgx, SGX_BIF_CTRL_INVAL, SGX_BIF_CTRL_INVAL_ALL);
	/*
	 * Sony keeps one GPU address space, this driver switches per process.
	 * On the DevKit the first job of a second process faulted on cores 2
	 * and 3 only (BIF_FAULT 0x01010000, invalid page) while cores 0 and 1
	 * ran it: the bank 0 write may not reach the other cores' TLBs.  Each
	 * core and the master get the directory and the invalidate too.
	 */
	sgx_write(sgx, SGX_MASTER_BIF_CTRL + SGX_BIF_DIR_LIST_BASE(0) - 0x0c00,
		  vm->pd_dma);
	sgx_write(sgx, SGX_MASTER_BIF_CTRL + SGX_BIF_DIR_LIST_BASE(7) - 0x0c00,
		  vm->pd_dma);
	sgx_write(sgx, SGX_MASTER_BIF_CTRL + SGX_BIF_CTRL_INVAL - 0x0c00, 0);
	sgx_write(sgx, SGX_MASTER_BIF_CTRL + SGX_BIF_CTRL_INVAL - 0x0c00,
		  SGX_BIF_CTRL_INVAL_ALL);
	for (n = 0; n < PSP2SGX_NUM_CORES; n++) {
		u32 bank = SGX_CORE_BANK(n);

		sgx_write(sgx, bank + SGX_BIF_DIR_LIST_BASE(0), vm->pd_dma);
		sgx_write(sgx, bank + SGX_BIF_DIR_LIST_BASE(7), vm->pd_dma);
		sgx_write(sgx, bank + SGX_BIF_CTRL_INVAL, 0);
		sgx_write(sgx, bank + SGX_BIF_CTRL_INVAL, SGX_BIF_CTRL_INVAL_ALL);
	}
	wmb();
}

/* Core 0's event kick: increment the kicker word and run the event PDS
 * program, which starts the microkernel.  The kicker word lives in the
 * kernel region, so flush the SLC before the kick so the GPU sees it. */
void psp2sgx_hw_kick(struct psp2sgx_device *sgx)
{
	u32 *kicker = (u32 *)((u8 *)sgx->kregion + SGX_MK_KICKER);

	wmb();
	/* Sony: increment the event kicker word before each kick. */
	*kicker = (*kicker + 1) & 0xff;
	psp2sgx_hw_slc_flush(sgx);
	wmb();
	sgx_write(sgx, SGX_CORE_BANK(0) + SGX_EVENT_KICK2, 1);
}

static u32 code_base(u32 va, u32 dm)
{
	return (va >> 6) | dm << SGX_USE_CODE_BASE_DM_SHIFT;
}

/*
 * The genuine init script's writes after the start sequence, in its order
 * (an MMIO trace), with our addresses.  The values without a comment are the
 * traced ones, which the DevKit ran.  The values
 * without a comment are the traced ones, which the DevKit ran.
 */
int psp2sgx_hw_init(struct psp2sgx_device *sgx)
{
	static const u32 zeroed[] = {
		SGX_BIF_CTRL, SGX_BIF_BANK_SET, SGX_BIF_BANK0, SGX_BIF_DIR_LIST_BASE(0),
		0x0c38, 0x0c3c, 0x0c40, 0x0c44, 0x0c48, 0x0c4c, 0x0c50,
	};
	const u32 kva = SGX_KREGION_VA;
	unsigned int n;

	for (n = 0; n < ARRAY_SIZE(zeroed); n++)
		sgx_write(sgx, zeroed[n], 0);
	/* every data master's requests go through directory 7 or 0; both hold
	 * the same */
	sgx_write(sgx, SGX_BIF_BANK0, 7);
	psp2sgx_hw_set_vm(sgx, sgx->kernel_vm);
	sgx_write(sgx, 0x001c, 0);
	sgx_write(sgx, SGX_EVENT_HOST_ENABLE, SGX_EVENT_SW_EVENT);
	sgx_write(sgx, SGX_EVENT_HOST_ENABLE2, 0xc00);
	sgx_write(sgx, SGX_PDS_CACHE_HOST_ENABLE, 0);
	sgx_write(sgx, 0x0a7c, 0);
	sgx_write(sgx, 0x0a80, 0);
	sgx_write(sgx, SGX_DMS_CTRL, 0x208200);
	sgx_write(sgx, 0x0abc, 0x40);
	sgx_write(sgx, 0x0ab8, 0);
	sgx_write(sgx, SGX_TSP_CONFIG, 0x10);
	sgx_write(sgx, SGX_DCU_CTRL, 0);
	sgx_write(sgx, SGX_TCU_CTRL, 0x5e0);
	sgx_write(sgx, SGX_DCU_PCTRL, 0xffff);
	sgx_write(sgx, 0x0a00, 0x7c000);
	/*
	 * Code bases: 0 and 1 are the vertex data master, tiling engine,
	 * MTE, ISP, DPM, TPU and pixel event registers the DDK's microkernel
	 * programs for a TA/3D kick, the BIF requestor bases (addresses the
	 * MMU still translates), and the event clears for what a render's
	 * operations cannot (a: shell register trace, every kick):
	 * bank 0's event program is never kicked; it gets ours so it is valid.
	 */
	for (n = 0; n < 15; n++)
		sgx_write(sgx, SGX_USE_CODE_BASE(n), code_base(kva + SGX_MK_CODE, 0));
	sgx_write(sgx, SGX_USE_CODE_BASE(0), code_base(PSP2SGX_CODE_VERTEX_BASE, 0));
	sgx_write(sgx, SGX_USE_CODE_BASE(1), code_base(PSP2SGX_CODE_FRAGMENT_BASE, 1));
	sgx_write(sgx, SGX_USE_CODE_BASE(SGX_MK_CODE_BASE),
		  code_base(kva + SGX_MK_CODE, 3));
	sgx_write(sgx, 0x0b30, 0x100);
	sgx_write(sgx, SGX_EVENT_KICKER, kva + SGX_MK_KICKER);
	/* bank 0's event program is never kicked; it gets ours so it is valid */
	sgx_write(sgx, SGX_EVENT_OTHER_PDS_EXEC, kva + SGX_MK_EVENT_PDS);
	sgx_write(sgx, SGX_EVENT_OTHER_PDS_DATA, SGX_PDS_EVENT_UNITS);
	sgx_write(sgx, SGX_EVENT_OTHER_PDS_INFO, 0x12001);
	sgx_write(sgx, SGX_CORE_BANK(0) + SGX_EVENT_OTHER_PDS_EXEC,
		  kva + SGX_MK_EVENT_PDS);
	sgx_write(sgx, SGX_CORE_BANK(0) + SGX_EVENT_OTHER_PDS_DATA,
		  SGX_PDS_EVENT_UNITS);
	sgx_write(sgx, SGX_CORE_BANK(0) + SGX_EVENT_OTHER_PDS_INFO, 0x12001);
	sgx_write(sgx, SGX_CORE_BANK(0) + SGX_EVENT_PDS_ENABLE, 0x2200c000);
	/*
	 * Sony's microkernel start-up (shell register trace): TA_CLK_GATE
	 * START_TO_END in every core, as every microkernel thread sets.
	 */
	for (n = 0; n < PSP2SGX_NUM_CORES; n++)
		sgx_write(sgx, SGX_CORE_BANK(n) + SGX_TA_CLK_GATE, 1);
	dev_info(sgx->dev, "%u of %u cores in use (psp2sgx.cores)\n",
		 clamp(cores, 1u, 4u), PSP2SGX_NUM_CORES);
	sgx_write(sgx, 0x4a58, 0x804300f);
	sgx_write(sgx, 0x4808, 4);
	sgx_write(sgx, 0x4144, 0x103e8);
	sgx_write(sgx, 0x414c, 0);
	/* start from a clean host interrupt state */
	sgx_write(sgx, SGX_EVENT_HOST_CLEAR, SGX_EVENT_SW_EVENT);
	wmb();
	return 0;
}

/*
 * After a hang: restart the GPU and program it again.
 */
int psp2sgx_hw_reset(struct psp2sgx_device *sgx)
{
	u32 *hc = psp2sgx_hostctl(sgx);
	unsigned int n;

	/*
	 * The master soft reset in psp2sgx_start() does not stop a core that
	 * hung in a TA or 3D pass: on the DevKit every job after a
	 * failed one failed too, with the cores' pixel units still clocked.
	 * Each core's own units are held in reset over the start sequence.
	 */
	sgx_write(sgx, SGX_SOFT_RESET, SGX_SOFT_RESET_ALL);
	for (n = 0; n < PSP2SGX_NUM_CORES; n++)
		sgx_write(sgx, SGX_CORE_BANK(n) + SGX_SOFT_RESET,
			  SGX_SOFT_RESET_ALL);
	wmb();
	udelay(10);
	psp2sgx_start(sgx);
	for (n = 0; n < PSP2SGX_NUM_CORES; n++)
		sgx_write(sgx, SGX_CORE_BANK(n) + SGX_SOFT_RESET, 0);
	sgx_write(sgx, SGX_SOFT_RESET, 0);
	wmb();
	udelay(10);
	/*
	 * The run left unclaimed must not run on the next event:
	 * mark it taken and done.  The GPU is idle, so the host may write
	 * the microkernel's block; the SLC is emptied around it so no old
	 * line of the block is written back over it or read in its place.
	 */
	psp2sgx_hw_slc_flush(sgx);
	WRITE_ONCE(hc[SGX_HC_CLAIMED], READ_ONCE(psp2sgx_hostpost(sgx)[SGX_HP_POSTED]));
	WRITE_ONCE(hc[SGX_HC_DONE_SEQNO], hc[SGX_HC_CLAIMED]);
	WRITE_ONCE(hc[SGX_HC_STATE], SGX_MK_IDLE);
	wmb();
	psp2sgx_hw_slc_flush(sgx);
	return psp2sgx_hw_init(sgx);
}
