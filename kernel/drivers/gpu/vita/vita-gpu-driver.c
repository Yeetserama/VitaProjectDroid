/*
 * Vita SGX543MP4+ GPU driver (G4 research skeleton)
 *
 * Status: research skeleton only. Powers the GPU on, maps registers,
 * reads ID/revision registers, sets up command-ring infrastructure
 * placeholders, and registers an IRQ stub. Does NOT submit jobs,
 * does not load firmware, does not expose GLES/EGL.
 *
 * Gate: G2 read-only probe must pass on hardware before this driver
 * is enabled. G3 (firmware) and userspace UMD/GLSL compiler are
 * blocked and not provided here.
 *
 * Built-in only (CONFIG_MODULES=off in Vita kernel).
 *
 * Hardware facts (from shoui520/psp2 clean-room reimplementation):
 *   Pervasive Reset page   : 0xE3101000, GPU field at offset 0x10
 *   Pervasive Gate page    : 0xE3102000, GPU field at offset 0x10
 *   Pervasive BaseClk page : 0xE3103000, gpuClock at offset 0x10
 *   Pervasive Misc page    : 0xE3100000, gpuIsolation at offset 0x34
 *   SGX MMIO base          : 0xE8400000, size 0x20000
 *   Per-core bank n        : 0xE8400000 + (n + 2) * 0x4000 (4 cores)
 *   GPU IRQ                : 0x34 (52)
 *
 * SGX register offsets within master bank (from openpvrsgx + Vita3K):
 *   0x0000 SYS_ID        Core ID / Revision
 *   0x0004 SYS_STATUS    Status / Revision
 *   0x0008 SYS_CONFIG    Configuration
 *   0x0010 SYS_CLOCK_GATE Clock gating control
 *   0x0014 SYS_RESET     Soft reset control
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/slab.h>

#define DRV_NAME "vita-gpu-driver"

/* Pervasive block physical addresses (shoui520/psp2 pervasive_regs.h) */
#define PERVASIVE_MISC_BASE    0xE3100000
#define PERVASIVE_RESET_BASE   0xE3101000
#define PERVASIVE_GATE_BASE    0xE3102000
#define PERVASIVE_BASECLK_BASE 0xE3103000

/* Offsets within reset/gate pages (pervasive device register struct) */
#define PERSIST_OFFSET_GPU        0x10   /* GPU reset/gate control */
#define PERSIST_OFFSET_MISC_ISO   0x34   /* gpuIsolation in misc page */
#define PERSIST_OFFSET_BASECLK    0x10   /* gpuClock in baseclk page */

/* SGX MMIO (shoui520/psp2 sgx_regs.h) */
#define SGX_REG_BASE   0xE8400000
#define SGX_REG_SIZE   0x20000

/* SGX register offsets within master bank */
#define SGX_SYS_ID            0x0000
#define SGX_SYS_STATUS        0x0004
#define SGX_SYS_CONFIG        0x0008
#define SGX_SYS_CLOCK_GATE    0x0010
#define SGX_SYS_RESET         0x0014
#define SGX_SYS_IRQ_STATUS    0x5000
#define SGX_SYS_IRQ_ACK       0x5008

/* Per-core bank offset */
#define SGX_CORE_BANK_SHIFT   14
#define SGX_CORE_BANK_SIZE    (1U << SGX_CORE_BANK_SHIFT)
#define SGX_CORE_BANK_BASE    2
#define SGX_NUM_CORES         4

/* Expected SGX543MP4+ ID values */
#define SGX543_CORE_ID        0x5430
#define SERIES5XT_MARKER      0x5000

/* Command ring sizes */
#define VITA_GPU_CMD_RING_SIZE   0x4000   /* 16 KB ring buffer */
#define VITA_GPU_CMD_RING_COUNT  2        /* double-buffered */

/* ------------------------------------------------------------------ */
/* Driver state                                                       */
/* ------------------------------------------------------------------ */

struct vita_gpu_core {
	void __iomem *bank;          /* per-core MMIO bank */
	u32 id;                      /* core ID */
	u32 revision;                /* core revision */
};

struct vita_gpu_driver {
	void __iomem *pers_misc;
	void __iomem *pers_reset;
	void __iomem *pers_gate;
	void __iomem *pers_baseclk;
	void __iomem *sgx_master;

	struct vita_gpu_core cores[SGX_NUM_CORES];

	/* Command ring (DMA-coherent) */
	dma_addr_t cmd_ring_dma[VITA_GPU_CMD_RING_COUNT];
	void *cmd_ring_cpu[VITA_GPU_CMD_RING_COUNT];
	u32 cmd_ring_write_idx[VITA_GPU_CMD_RING_COUNT];
	u32 cmd_ring_read_idx[VITA_GPU_CMD_RING_COUNT];

	int irq;
	bool powered_on;
	bool id_verified;
};

static struct vita_gpu_driver g_gpu;

/* ------------------------------------------------------------------ */
/* Pervasive power sequencing                                         */
/* ------------------------------------------------------------------ */

static int vita_gpu_power_on(void)
{
	struct vita_gpu_driver *drv = &g_gpu;
	u32 val;

	/* Read current pervasive states */
	if (drv->pers_misc) {
		val = readl(drv->pers_misc + PERSIST_OFFSET_MISC_ISO);
		pr_info("%s: Pervasive misc gpuIsolation: 0x%08x\n", DRV_NAME, val);
	}
	if (drv->pers_reset) {
		val = readl(drv->pers_reset + PERSIST_OFFSET_GPU);
		pr_info("%s: Pervasive reset GPU: 0x%08x (0=not in reset)\n",
			DRV_NAME, val);
	}
	if (drv->pers_gate) {
		val = readl(drv->pers_gate + PERSIST_OFFSET_GPU);
		pr_info("%s: Pervasive gate GPU: 0x%08x (non-zero=clock enabled)\n",
			DRV_NAME, val);
	}
	if (drv->pers_baseclk) {
		val = readl(drv->pers_baseclk + PERSIST_OFFSET_BASECLK);
		pr_info("%s: Pervasive baseclk GPU: 0x%08x (core<<16|mp)\n",
			DRV_NAME, val);
	}

	/* Deassert GPU reset: clear GPU bit in reset page */
	if (drv->pers_reset) {
		val = readl(drv->pers_reset + PERSIST_OFFSET_GPU);
		pr_info("%s: Deasserting GPU reset (reset+0x10 = 0x0)\n", DRV_NAME);
		writel(0x0, drv->pers_reset + PERSIST_OFFSET_GPU);
		val = readl(drv->pers_reset + PERSIST_OFFSET_GPU);
		pr_info("%s: Reset register now: 0x%08x\n", DRV_NAME, val);
	}

	/* Enable GPU clock: set GPU bit in gate page */
	if (drv->pers_gate) {
		val = readl(drv->pers_gate + PERSIST_OFFSET_GPU);
		pr_info("%s: Enabling GPU clock (gate+0x10 = 0x1)\n", DRV_NAME);
		writel(0x1, drv->pers_gate + PERSIST_OFFSET_GPU);
		val = readl(drv->pers_gate + PERSIST_OFFSET_GPU);
		pr_info("%s: Gate register now: 0x%08x\n", DRV_NAME, val);
	}

	/* Wait for GPU to stabilize */
	udelay(100);
	msleep(10);

	drv->powered_on = true;
	pr_info("%s: GPU power-on sequence complete\n", DRV_NAME);

	return 0;
}

static void vita_gpu_power_off(void)
{
	struct vita_gpu_driver *drv = &g_gpu;

	if (!drv->powered_on)
		return;

	if (drv->pers_reset) {
		pr_info("%s: Asserting GPU reset\n", DRV_NAME);
		writel(0x1, drv->pers_reset + PERSIST_OFFSET_GPU);
	}
	if (drv->pers_gate) {
		pr_info("%s: Disabling GPU clock\n", DRV_NAME);
		writel(0x0, drv->pers_gate + PERSIST_OFFSET_GPU);
	}
	drv->powered_on = false;
}

/* ------------------------------------------------------------------ */
/* SGX register access                                                */
/* ------------------------------------------------------------------ */

static int vita_gpu_read_core_id(int core)
{
	struct vita_gpu_driver *drv = &g_gpu;
	void __iomem *base;
	u32 id, rev, config, status;
	const char *tag = core < 0 ? "master" : "";

	if (core < 0) {
		base = drv->sgx_master;
	} else {
		base = drv->sgx_master + ((core + SGX_CORE_BANK_BASE) * SGX_CORE_BANK_SIZE);
	}

	if (!base) {
		pr_err("%s: SGX %s registers not mapped\n", DRV_NAME, tag);
		return -ENODEV;
	}

	id      = readl(base + SGX_SYS_ID);
	rev     = readl(base + SGX_SYS_STATUS);
	config  = readl(base + SGX_SYS_CONFIG);
	status  = readl(base + SGX_SYS_STATUS);

	pr_info("%s: SGX %s ID:      0x%08x\n", DRV_NAME, tag, id);
	pr_info("%s: SGX %s Status:  0x%08x\n", DRV_NAME, tag, rev);
	pr_info("%s: SGX %s Config:  0x%08x\n", DRV_NAME, tag, config);

	if ((id & 0xF000) == SERIES5XT_MARKER) {
		pr_info("%s:   %s: Series5XT family (0x%04x)\n",
			DRV_NAME, tag, id & 0xFFFF);
		if ((id & 0xFFFF) == SGX543_CORE_ID)
			pr_info("%s:   %s: Matches SGX543 core ID\n", DRV_NAME, tag);
		else
			pr_info("%s:   %s: Unknown Series5XT variant: 0x%04x\n",
				DRV_NAME, tag, id & 0xFFFF);
		if (core >= 0) {
			drv->cores[core].id = id;
			drv->cores[core].revision = rev;
		}
		return 0;
	}

	pr_warn("%s:   %s: Unexpected ID 0x%08x (expected Series5XT 0x5xxx)\n",
		DRV_NAME, tag, id);
	return -ENODEV;
}

/* ------------------------------------------------------------------ */
/* Command ring infrastructure (placeholders)                         */
/* ------------------------------------------------------------------ */

static int vita_gpu_cmd_ring_init(void)
{
	struct vita_gpu_driver *drv = &g_gpu;
	int i;

	for (i = 0; i < VITA_GPU_CMD_RING_COUNT; i++) {
		/* Allocate DMA-coherent ring buffer */
		drv->cmd_ring_cpu[i] = dma_alloc_coherent(NULL,
				VITA_GPU_CMD_RING_SIZE,
				&drv->cmd_ring_dma[i], GFP_KERNEL);
		if (!drv->cmd_ring_cpu[i]) {
			pr_err("%s: Failed to allocate command ring %d\n",
				DRV_NAME, i);
			return -ENOMEM;
		}
		memset(drv->cmd_ring_cpu[i], 0, VITA_GPU_CMD_RING_SIZE);
		drv->cmd_ring_write_idx[i] = 0;
		drv->cmd_ring_read_idx[i] = 0;

		pr_info("%s: Command ring %d: cpu=0x%p dma=0x%pad size=0x%x\n",
			DRV_NAME, i, drv->cmd_ring_cpu[i],
			&drv->cmd_ring_dma[i], VITA_GPU_CMD_RING_SIZE);
	}

	return 0;
}

static void vita_gpu_cmd_ring_cleanup(void)
{
	struct vita_gpu_driver *drv = &g_gpu;
	int i;

	for (i = 0; i < VITA_GPU_CMD_RING_COUNT; i++) {
		if (drv->cmd_ring_cpu[i]) {
			dma_free_coherent(NULL, VITA_GPU_CMD_RING_SIZE,
					drv->cmd_ring_cpu[i],
					drv->cmd_ring_dma[i]);
			drv->cmd_ring_cpu[i] = NULL;
		}
	}
}

/* ------------------------------------------------------------------ */
/* IRQ handler stub                                                   */
/* ------------------------------------------------------------------ */

static irqreturn_t vita_gpu_irq(int irq, void *dev_id)
{
	struct vita_gpu_driver *drv = dev_id;
	u32 status;

	/* Read SGX IRQ status register */
	status = readl(drv->sgx_master + SGX_SYS_IRQ_STATUS);
	pr_info("%s: GPU IRQ %d: status=0x%08x\n", DRV_NAME, irq, status);

	/* Acknowledge (stub - actual ack register TBD) */
	writel(status, drv->sgx_master + SGX_SYS_IRQ_ACK);

	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------ */
/* Probe                                                              */
/* ------------------------------------------------------------------ */

static int __init vita_gpu_probe(void)
{
	struct vita_gpu_driver *drv = &g_gpu;
	int ret;
	int i;

	pr_info("%s: Vita SGX543MP4+ driver probe starting\n", DRV_NAME);

	/* Map pervasive pages (direct MMIO) */
	drv->pers_misc = ioremap_nocache(PERVASIVE_MISC_BASE, 0x1000);
	if (!drv->pers_misc) {
		pr_err("%s: Failed to ioremap pervasive misc\n", DRV_NAME);
		return -ENOMEM;
	}
	pr_info("%s: Mapped pervasive misc at 0x%08x\n",
		DRV_NAME, PERVASIVE_MISC_BASE);

	drv->pers_reset = ioremap_nocache(PERVASIVE_RESET_BASE, 0x1000);
	if (!drv->pers_reset) {
		pr_err("%s: Failed to ioremap pervasive reset\n", DRV_NAME);
		return -ENOMEM;
	}
	pr_info("%s: Mapped pervasive reset at 0x%08x\n",
		DRV_NAME, PERVASIVE_RESET_BASE);

	drv->pers_gate = ioremap_nocache(PERVASIVE_GATE_BASE, 0x1000);
	if (!drv->pers_gate) {
		pr_err("%s: Failed to ioremap pervasive gate\n", DRV_NAME);
		return -ENOMEM;
	}
	pr_info("%s: Mapped pervasive gate at 0x%08x\n",
		DRV_NAME, PERVASIVE_GATE_BASE);

	drv->pers_baseclk = ioremap_nocache(PERVASIVE_BASECLK_BASE, 0x1000);
	if (!drv->pers_baseclk) {
		pr_err("%s: Failed to ioremap pervasive baseclk\n", DRV_NAME);
		return -ENOMEM;
	}
	pr_info("%s: Mapped pervasive baseclk at 0x%08x\n",
		DRV_NAME, PERVASIVE_BASECLK_BASE);

	/* Power on sequence */
	ret = vita_gpu_power_on();
	if (ret) {
		pr_err("%s: GPU power-on failed: %d\n", DRV_NAME, ret);
		return ret;
	}

	/* Map SGX master register block */
	drv->sgx_master = ioremap_nocache(SGX_REG_BASE, SGX_REG_SIZE);
	if (!drv->sgx_master) {
		pr_err("%s: Failed to ioremap SGX master at 0x%08x\n",
			DRV_NAME, SGX_REG_BASE);
		vita_gpu_power_off();
		return -ENOMEM;
	}
	pr_info("%s: Mapped SGX master at 0x%08x (size 0x%x)\n",
		DRV_NAME, SGX_REG_BASE, SGX_REG_SIZE);

	/* Read ID from master bank */
	ret = vita_gpu_read_core_id(-1);
	if (ret) {
		pr_err("%s: SGX master ID read failed — stopping probe\n", DRV_NAME);
		vita_gpu_power_off();
		return ret;
	}
	drv->id_verified = true;

	/* Read ID from each core bank */
	for (i = 0; i < SGX_NUM_CORES; i++) {
		vita_gpu_read_core_id(i);
	}

	/* Print SGX master clock gate and reset registers */
	pr_info("%s: SGX master ClockGate: 0x%08x\n",
		DRV_NAME, readl(drv->sgx_master + SGX_SYS_CLOCK_GATE));
	pr_info("%s: SGX master Reset:     0x%08x\n",
		DRV_NAME, readl(drv->sgx_master + SGX_SYS_RESET));

	/* Initialize command ring infrastructure */
	ret = vita_gpu_cmd_ring_init();
	if (ret) {
		pr_err("%s: Command ring init failed: %d\n", DRV_NAME, ret);
		vita_gpu_cmd_ring_cleanup();
		vita_gpu_power_off();
		return ret;
	}

	/* Request IRQ (stub handler) */
	ret = request_irq(52, vita_gpu_irq, 0, DRV_NAME, drv);
	if (ret) {
		pr_err("%s: Failed to request IRQ 52: %d\n", DRV_NAME, ret);
		vita_gpu_cmd_ring_cleanup();
		vita_gpu_power_off();
		return ret;
	}
	drv->irq = 52;
	pr_info("%s: Registered IRQ 52\n", DRV_NAME);

	pr_info("%s: GPU driver probe completed (skeleton — no job submission)\n",
		DRV_NAME);

	return 0;
}

static void __exit vita_gpu_exit(void)
{
	struct vita_gpu_driver *drv = &g_gpu;

	vita_gpu_cmd_ring_cleanup();
	vita_gpu_power_off();

	if (drv->irq >= 0)
		free_irq(52, drv);
}

module_init(vita_gpu_probe);
module_exit(vita_gpu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AndroidOnVita");
MODULE_DESCRIPTION("Vita SGX543MP4+ GPU driver skeleton (G4 research)");
