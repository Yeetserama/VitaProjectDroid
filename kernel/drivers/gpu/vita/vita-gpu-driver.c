/*
 * Vita SGX543MP4+ GPU driver (G4 research skeleton)
 *
 * Status: research skeleton only. Powers the GPU on, runs the init register
 * script, reads ID/revision registers, sets up command-ring infrastructure
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
 * SGX register offsets within master bank (from sgx543_v1.164defs.h):
 *   0x0000 CLKGATECTL, 0x0004 CLKGATECTL2, 0x0008 CLKGATESTATUS,
 *   0x000C CLKGATECTLOVR, 0x001C POWER, 0x0020 CORE_ID,
 *   0x0024 CORE_REVISION, 0x0080 SOFT_RESET,
 *   0x0110 EVENT_HOST_ENABLE2, 0x0130 EVENT_HOST_ENABLE,
 *   0x0134 EVENT_HOST_CLEAR, 0x0144 TIMER,
 *   0x0AC8 EVENT_KICKER,
 *   0x4000 MASTER_CORE, 0x4004 (core enable mask),
 *   0x4010 MASTER_CORE_ID, 0x4014 MASTER_CORE_REVISION,
 *   0x4080 MASTER_SOFT_RESET,
 *   0x4C00 MASTER_BIF_CTRL, 0x4CD0 MASTER_BIF_MMU_CTRL,
 *   0x4D00 MASTER_SLC_CTRL, 0x4D04 MASTER_SLC_CTRL_BYPASS
 *
 * Per-core bank offset: (core + 2) << 14 = 0x4000 per core
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

/* Offsets within reset/gate pages */
#define PERSIST_OFFSET_GPU        0x10   /* GPU reset/gate control */
#define PERSIST_OFFSET_MISC_ISO   0x34   /* gpuIsolation in misc page */
#define PERSIST_OFFSET_BASECLK    0x10   /* gpuClock in baseclk page */

/* SGX MMIO (shoui520/psp2 sgx_regs.h) */
#define SGX_REG_BASE   0xE8400000
#define SGX_REG_SIZE   0x20000

/* Per-core bank offset */
#define SGX_CORE_BANK_SHIFT   14
#define SGX_CORE_BANK_SIZE    (1U << SGX_CORE_BANK_SHIFT)
#define SGX_CORE_BANK_BASE    2
#define SGX_NUM_CORES         4

/* Command ring sizes */
#define VITA_GPU_CMD_RING_SIZE   0x4000
#define VITA_GPU_CMD_RING_COUNT  2

/* SGX register offsets (from sgx543_v1.164defs.h) */
#define SGX_SYS_CLKGATECTL       0x0000
#define SGX_SYS_CLKGATECTL2      0x0004
#define SGX_SYS_CLKGATESTATUS    0x0008
#define SGX_SYS_POWER            0x001C
#define SGX_SYS_CORE_ID          0x0020
#define SGX_SYS_CORE_REVISION    0x0024
#define SGX_SYS_SOFT_RESET       0x0080
#define SGX_SYS_EVENT_HOST_EN    0x0130
#define SGX_SYS_EVENT_HOST_EN2   0x0110
#define SGX_SYS_EVENT_HOST_CLR   0x0134
#define SGX_SYS_EVENT_KICKER     0x0AC8
#define SGX_SYS_TIMER            0x0144
#define SGX_MASTER_CORE          0x4000
#define SGX_MASTER_CORE_ID       0x4010
#define SGX_MASTER_CORE_REV      0x4014
#define SGX_MASTER_SOFT_RESET    0x4080

/* Init script register values (from shoui520/psp2 gpuinit_es4/devices/sgx/sgxinit.c) */
#define VITA_CLKGATECTL_VAL      0x2aaaaa
#define VITA_CLKGATECTL2_VAL     0xa8a8aaa
#define VITA_CLKGATESTATUS_MASK  0xffff77ff
#define VITA_MASTER_CLK_GATE_REG 0x4008
#define VITA_MASTER_CLK_GATE_MASK 15
#define VITA_MASTER_CLK_GATE2_REG 0x4024
#define VITA_MASTER_CLK_GATE2_MASK 30
#define VITA_EDM_TASK_REG0       0xb10
#define VITA_EDM_TASK_REG1       0xb1c
#define VITA_CORE_MASK           0xa2   /* cores 0,2,4,6 -> bits 1,3,5,7 */
#define VITA_POWER_CONFIG        0x2a9

struct vita_gpu_core {
	void __iomem *bank;
	u32 id;
	u32 revision;
};

struct vita_gpu_driver {
	void __iomem *pers_misc;
	void __iomem *pers_reset;
	void __iomem *pers_gate;
	void __iomem *pers_baseclk;
	void __iomem *sgx_master;
	struct device *dev;

	struct vita_gpu_core cores[SGX_NUM_CORES];

	dma_addr_t cmd_ring_dma[VITA_GPU_CMD_RING_COUNT];
	void *cmd_ring_cpu[VITA_GPU_CMD_RING_COUNT];
	u32 cmd_ring_write_idx[VITA_GPU_CMD_RING_COUNT];
	u32 cmd_ring_read_idx[VITA_GPU_CMD_RING_COUNT];

	int irq;
	bool powered_on;
	bool id_verified;
};

static struct vita_gpu_driver g_gpu;

static int vita_gpu_power_on(void)
{
	struct vita_gpu_driver *drv = &g_gpu;
	u32 val;

	if (drv->pers_misc) {
		val = readl(drv->pers_misc + PERSIST_OFFSET_MISC_ISO);
		pr_info("%s: gpuIsolation: 0x%08x\n", DRV_NAME, val);
	}
	if (drv->pers_reset) {
		val = readl(drv->pers_reset + PERSIST_OFFSET_GPU);
		pr_info("%s: reset GPU: 0x%08x\n", DRV_NAME, val);
	}
	if (drv->pers_gate) {
		val = readl(drv->pers_gate + PERSIST_OFFSET_GPU);
		pr_info("%s: gate GPU: 0x%08x\n", DRV_NAME, val);
	}
	if (drv->pers_baseclk) {
		val = readl(drv->pers_baseclk + PERSIST_OFFSET_BASECLK);
		pr_info("%s: baseclk GPU: 0x%08x\n", DRV_NAME, val);
	}

	/* Deassert GPU reset */
	if (drv->pers_reset) {
		writel(0x0, drv->pers_reset + PERSIST_OFFSET_GPU);
		val = readl(drv->pers_reset + PERSIST_OFFSET_GPU);
		pr_info("%s: reset now: 0x%08x\n", DRV_NAME, val);
	}

	/* Enable GPU clock */
	if (drv->pers_gate) {
		writel(0x1, drv->pers_gate + PERSIST_OFFSET_GPU);
		val = readl(drv->pers_gate + PERSIST_OFFSET_GPU);
		pr_info("%s: gate now: 0x%08x\n", DRV_NAME, val);
	}

	udelay(100);
	msleep(10);

	drv->powered_on = true;
	pr_info("%s: power-on complete\n", DRV_NAME);
	return 0;
}

static void vita_gpu_power_off(void)
{
	struct vita_gpu_driver *drv = &g_gpu;

	if (!drv->powered_on)
		return;

	if (drv->pers_reset)
		writel(0x1, drv->pers_reset + PERSIST_OFFSET_GPU);
	if (drv->pers_gate)
		writel(0x0, drv->pers_gate + PERSIST_OFFSET_GPU);
	drv->powered_on = false;
}

static int vita_gpu_run_init_script(void)
{
	struct vita_gpu_driver *drv = &g_gpu;
	void __iomem *regs = drv->sgx_master;

	/* Part 1: enable cores and clocks */
	writel(3, regs + SGX_MASTER_CORE);            /* EUR_CR_MASTER_CORE = 3 */
	writel(VITA_POWER_CONFIG, regs + 0x4020);     /* core clock config */
	writel(VITA_CORE_MASK, regs + 0x4004);        /* core enable mask */

	/* Part 2: events, code bases, microkernel PDS */
	writel(0x4000, regs + SGX_SYS_EVENT_HOST_EN);      /* EVENT_HOST_ENABLE */
	writel(0xc00,  regs + SGX_SYS_EVENT_HOST_EN2);     /* EVENT_HOST_ENABLE2 */
	writel(0,      regs + 0x13c);
	writel(0,      regs + 0xa7c);
	writel(0,      regs + 0xa80);
	writel(0x208200, regs + 0xa74);
	writel(0x40,   regs + 0xabc);
	writel(0,      regs + 0xab8);
	writel(0x10,   regs + 0x800);
	writel(0,      regs + 0x818);
	writel(0x5e0,  regs + 0x804);
	writel(0xffff, regs + 0x814);
	writel(0x7c000,regs + 0xa00);
	writel(0x100,  regs + 0xb30);
	writel(drv->cmd_ring_dma[0], regs + SGX_SYS_EVENT_KICKER);

	/* Clock gating control values */
	writel(VITA_CLKGATECTL_VAL,  regs + SGX_SYS_CLKGATECTL);
	writel(VITA_CLKGATECTL2_VAL, regs + SGX_SYS_CLKGATECTL2);

	/* Master clock gate registers */
	writel(VITA_MASTER_CLK_GATE_MASK,  regs + VITA_MASTER_CLK_GATE_REG);
	writel(VITA_MASTER_CLK_GATE2_MASK, regs + VITA_MASTER_CLK_GATE2_REG);

	pr_info("%s: init script executed\n", DRV_NAME);
	return 0;
}

static int vita_gpu_read_core_id(int core)
{
	struct vita_gpu_driver *drv = &g_gpu;
	void __iomem *base;
	u32 id, rev, config;
	const char *tag = core < 0 ? "master" : "";

	if (core < 0)
		base = drv->sgx_master;
	else
		base = drv->sgx_master + ((core + SGX_CORE_BANK_BASE) * SGX_CORE_BANK_SIZE);

	if (!base) {
		pr_err("%s: SGX %s not mapped\n", DRV_NAME, tag);
		return -ENODEV;
	}

	id   = readl(base + SGX_SYS_CORE_ID);
	rev  = readl(base + SGX_SYS_CORE_REVISION);
	config = readl(base + SGX_SYS_POWER);

	pr_info("%s: SGX %s ID: 0x%08x Rev: 0x%08x Pwr: 0x%08x\n",
		DRV_NAME, tag, id, rev, config);

	if ((id & 0xF000) == 0x5000) {
		pr_info("%s:   %s: Series5XT (0x%04x)\n", DRV_NAME, tag, id & 0xFFFF);
		if ((id & 0xFFFF) == 0x5430)
			pr_info("%s:   %s: SGX543\n", DRV_NAME, tag);
		return 0;
	}
	pr_warn("%s:   %s: Unexpected ID 0x%08x\n", DRV_NAME, tag, id);
	return -ENODEV;
}

static int vita_gpu_cmd_ring_init(void)
{
	struct vita_gpu_driver *drv = &g_gpu;
	int i;

	for (i = 0; i < VITA_GPU_CMD_RING_COUNT; i++) {
		drv->cmd_ring_cpu[i] = dma_alloc_coherent(NULL,
				VITA_GPU_CMD_RING_SIZE, &drv->cmd_ring_dma[i], GFP_KERNEL);
		if (!drv->cmd_ring_cpu[i]) {
			pr_err("%s: command ring %d alloc failed\n", DRV_NAME, i);
			return -ENOMEM;
		}
		memset(drv->cmd_ring_cpu[i], 0, VITA_GPU_CMD_RING_SIZE);
		drv->cmd_ring_write_idx[i] = 0;
		drv->cmd_ring_read_idx[i] = 0;
		pr_info("%s: ring %d: cpu=0x%p dma=0x%pad size=0x%x\n",
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
				drv->cmd_ring_cpu[i], drv->cmd_ring_dma[i]);
			drv->cmd_ring_cpu[i] = NULL;
		}
	}
}

static irqreturn_t vita_gpu_irq(int irq, void *dev_id)
{
	struct vita_gpu_driver *drv = dev_id;
	u32 status, enable;

	status = readl(drv->sgx_master + SGX_SYS_EVENT_HOST_EN);
	enable = readl(drv->sgx_master + SGX_SYS_EVENT_HOST_EN);
	status &= enable;
	pr_info("%s: IRQ %d: status=0x%08x enable=0x%08x\n",
		DRV_NAME, irq, status, enable);
	writel(status, drv->sgx_master + SGX_SYS_EVENT_HOST_CLR);
	return IRQ_HANDLED;
}

static int __init vita_gpu_probe(void)
{
	struct vita_gpu_driver *drv = &g_gpu;
	int ret, i;

	pr_info("%s: Vita SGX543MP4+ driver probe starting\n", DRV_NAME);

	drv->pers_misc   = ioremap_nocache(PERVASIVE_MISC_BASE, 0x1000);
	if (!drv->pers_misc) { pr_err("%s: misc ioremap fail\n", DRV_NAME); return -ENOMEM; }
	drv->pers_reset  = ioremap_nocache(PERVASIVE_RESET_BASE, 0x1000);
	if (!drv->pers_reset) { pr_err("%s: reset ioremap fail\n", DRV_NAME); return -ENOMEM; }
	drv->pers_gate   = ioremap_nocache(PERVASIVE_GATE_BASE, 0x1000);
	if (!drv->pers_gate) { pr_err("%s: gate ioremap fail\n", DRV_NAME); return -ENOMEM; }
	drv->pers_baseclk= ioremap_nocache(PERVASIVE_BASECLK_BASE, 0x1000);
	if (!drv->pers_baseclk) { pr_err("%s: baseclk ioremap fail\n", DRV_NAME); return -ENOMEM; }

	ret = vita_gpu_power_on();
	if (ret) { pr_err("%s: power-on failed\n", DRV_NAME); return ret; }

	drv->sgx_master = ioremap_nocache(SGX_REG_BASE, SGX_REG_SIZE);
	if (!drv->sgx_master) {
		pr_err("%s: SGX ioremap fail\n", DRV_NAME);
		vita_gpu_power_off(); return -ENOMEM;
	}
	pr_info("%s: SGX mapped at 0x%08x size 0x%x\n", DRV_NAME, SGX_REG_BASE, SGX_REG_SIZE);

	ret = vita_gpu_read_core_id(-1);
	if (ret) { pr_err("%s: master ID read failed\n", DRV_NAME); vita_gpu_power_off(); return ret; }
	drv->id_verified = true;
	for (i = 0; i < SGX_NUM_CORES; i++) vita_gpu_read_core_id(i);

	ret = vita_gpu_run_init_script();
	if (ret) { vita_gpu_power_off(); return ret; }

	ret = vita_gpu_cmd_ring_init();
	if (ret) { vita_gpu_cmd_ring_cleanup(); vita_gpu_power_off(); return ret; }

	ret = request_irq(52, vita_gpu_irq, 0, DRV_NAME, drv);
	if (ret) { pr_err("%s: IRQ 52 failed\n", DRV_NAME); vita_gpu_cmd_ring_cleanup(); vita_gpu_power_off(); return ret; }
	drv->irq = 52;

	pr_info("%s: probe complete (skeleton, no job submission)\n", DRV_NAME);
	return 0;
}

static void __exit vita_gpu_exit(void)
{
	struct vita_gpu_driver *drv = &g_gpu;

	vita_gpu_cmd_ring_cleanup();
	vita_gpu_power_off();
	if (drv->irq >= 0) free_irq(52, drv);
}

module_init(vita_gpu_probe);
module_exit(vita_gpu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AndroidOnVita");
MODULE_DESCRIPTION("Vita SGX543MP4+ GPU driver skeleton (G4 research)");
