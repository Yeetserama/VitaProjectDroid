/*
 * Vita GPU Read-Only Probe (G2)
 *
 * Purpose: Power on SGX block, read ID/revision/status registers, print to dmesg.
 * NO GPU WRITES beyond clock/reset enable via pervasive block MMIO.
 * Built-in only (CONFIG_MODULES=off in Vita kernel).
 *
 * Gate: Plausible SGX core ID + no bus hang.
 *
 * Hardware facts (from shoui520/psp2 clean-room reimplementation):
 *   Pervasive Reset page : 0xE3101000, GPU field at offset 0x10
 *   Pervasive Gate page  : 0xE3102000, GPU field at offset 0x10
 *   Pervasive BaseClk page: 0xE3103000, gpuClock at offset 0x10
 *   Pervasive Misc page  : 0xE3100000, gpuIsolation at offset 0x34
 *   SGX MMIO base        : 0xE8400000, size 0x20000
 *   Per-core bank n      : 0xE8400000 + (n + 2) * 0x4000 (4 cores)
 *
 * Register offsets within SGX block (from openpvrsgx + Vita3K):
 *   0x0000 SYS_ID       Core ID / Revision
 *   0x0004 SYS_STATUS   Status / Revision
 *   0x0008 SYS_CONFIG   Configuration
 *   0x0010 SYS_CLOCK_GATE Clock gating control
 *   0x0014 SYS_RESET    Soft reset control
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>

#define DRV_NAME "vita-gpu-probe"

/* Pervasive block physical addresses (shoui520/psp2 pervasive_regs.h) */
#define PERVASIVE_MISC_BASE   0xE3100000
#define PERVASIVE_RESET_BASE  0xE3101000
#define PERVASIVE_GATE_BASE   0xE3102000
#define PERVASIVE_BASECLK_BASE 0xE3103000

/* Offsets within reset/gate pages (pervasive device register struct) */
#define PERSIST_OFFSET_GPU        0x10   /* GPU reset/gate control */
#define PERSIST_OFFSET_MISC_ISO   0x34   /* gpuIsolation in misc page */
#define PERSIST_OFFSET_BASECLK    0x10   /* gpuClock in baseclk page */

/* SGX MMIO (shoui520/psp2 sgx_regs.h) */
#define SGX_REG_BASE  0xE8400000
#define SGX_REG_SIZE  0x20000

/* SGX register offsets within the master bank */
#define SGX_SYS_ID            0x0000  /* Core ID / Revision */
#define SGX_SYS_STATUS        0x0004  /* Status register */
#define SGX_SYS_CONFIG        0x0008  /* Configuration */
#define SGX_SYS_CLOCK_GATE    0x0010  /* Clock gating control */
#define SGX_SYS_RESET         0x0014  /* Soft reset control */

/* Per-core bank offset */
#define SGX_CORE_BANK_SHIFT   14
#define SGX_CORE_BANK_SIZE    (1U << SGX_CORE_BANK_SHIFT)
#define SGX_CORE_BANK_BASE    2       /* first core bank index */

/* Expected SGX543MP4+ ID values */
#define SGX543_CORE_ID        0x5430
#define SGX544_CORE_ID        0x5440
#define SERIES5XT_MARKER      0x5000

static void __iomem *g_pers_misc;
static void __iomem *g_pers_reset;
static void __iomem *g_pers_gate;
static void __iomem *g_pers_baseclk;
static void __iomem *g_sgx_regs;
static bool g_power_on;

static int vita_gpu_read_sgx_id(int core, const char *tag)
{
	void __iomem *base;
	u32 id, rev, config, status;

	if (core < 0) {
		base = g_sgx_regs;
	} else {
		base = g_sgx_regs + ((core + SGX_CORE_BANK_BASE) * SGX_CORE_BANK_SIZE);
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
		pr_info("%s:   %s: Series5XT family (0x%04x)\n", DRV_NAME, tag, id & 0xFFFF);
		if ((id & 0xFFFF) == SGX543_CORE_ID)
			pr_info("%s:   %s: Matches SGX543 core ID\n", DRV_NAME, tag);
		else if ((id & 0xFFFF) == SGX544_CORE_ID)
			pr_info("%s:   %s: Matches SGX544 core ID\n", DRV_NAME, tag);
		else
			pr_info("%s:   %s: Unknown Series5XT variant: 0x%04x\n", DRV_NAME, tag, id & 0xFFFF);
		return 0;
	}

	pr_warn("%s:   %s: Unexpected ID 0x%08x (expected Series5XT 0x5xxx)\n",
		DRV_NAME, tag, id);
	return -ENODEV;
}

static int __init vita_gpu_power_on(void)
{
	u32 val;

	/* Read current pervasive states before changing anything */
	if (g_pers_misc) {
		val = readl(g_pers_misc + PERSIST_OFFSET_MISC_ISO);
		pr_info("%s: Pervasive misc gpuIsolation: 0x%08x\n", DRV_NAME, val);
	}
	if (g_pers_reset) {
		val = readl(g_pers_reset + PERSIST_OFFSET_GPU);
		pr_info("%s: Pervasive reset GPU: 0x%08x (0=not in reset)\n", DRV_NAME, val);
	}
	if (g_pers_gate) {
		val = readl(g_pers_gate + PERSIST_OFFSET_GPU);
		pr_info("%s: Pervasive gate GPU: 0x%08x (non-zero=clock enabled)\n", DRV_NAME, val);
	}
	if (g_pers_baseclk) {
		val = readl(g_pers_baseclk + PERSIST_OFFSET_BASECLK);
		pr_info("%s: Pervasive baseclk GPU: 0x%08x (core<<16|mp)\n", DRV_NAME, val);
	}

	/* Deassert GPU reset: clear the GPU bit in reset page */
	if (g_pers_reset) {
		val = readl(g_pers_reset + PERSIST_OFFSET_GPU);
		pr_info("%s: Deasserting GPU reset (writing 0x0 to reset+0x10)\n", DRV_NAME);
		writel(0x0, g_pers_reset + PERSIST_OFFSET_GPU);
		val = readl(g_pers_reset + PERSIST_OFFSET_GPU);
		pr_info("%s: Reset register now: 0x%08x\n", DRV_NAME, val);
	}

	/* Enable GPU clock: set the GPU bit in gate page */
	if (g_pers_gate) {
		val = readl(g_pers_gate + PERSIST_OFFSET_GPU);
		pr_info("%s: Enabling GPU clock (writing 0x1 to gate+0x10)\n", DRV_NAME);
		writel(0x1, g_pers_gate + PERSIST_OFFSET_GPU);
		val = readl(g_pers_gate + PERSIST_OFFSET_GPU);
		pr_info("%s: Gate register now: 0x%08x\n", DRV_NAME, val);
	}

	/* Wait for GPU to stabilize */
	udelay(100);
	msleep(10);

	g_power_on = true;
	pr_info("%s: GPU power-on sequence complete\n", DRV_NAME);

	return 0;
}

static void __exit vita_gpu_power_off(void)
{
	if (!g_power_on)
		return;

	if (g_pers_reset) {
		pr_info("%s: Asserting GPU reset\n", DRV_NAME);
		writel(0x1, g_pers_reset + PERSIST_OFFSET_GPU);
	}
	if (g_pers_gate) {
		pr_info("%s: Disabling GPU clock\n", DRV_NAME);
		writel(0x0, g_pers_gate + PERSIST_OFFSET_GPU);
	}
	g_power_on = false;
}

static int __init vita_gpu_probe_init(void)
{
	int ret;
	int i;

	pr_info("%s: Vita SGX543MP4+ read-only probe (G2) starting\n", DRV_NAME);

	/* Map pervasive pages (direct MMIO, no DT/clk/reset framework needed) */
	g_pers_misc = ioremap_nocache(PERVASIVE_MISC_BASE, 0x1000);
	if (!g_pers_misc) {
		pr_err("%s: Failed to ioremap pervasive misc at 0x%08x\n",
			DRV_NAME, PERVASIVE_MISC_BASE);
		return -ENOMEM;
	}
	pr_info("%s: Mapped pervasive misc at 0x%08x\n", DRV_NAME, PERVASIVE_MISC_BASE);

	g_pers_reset = ioremap_nocache(PERVASIVE_RESET_BASE, 0x1000);
	if (!g_pers_reset) {
		pr_err("%s: Failed to ioremap pervasive reset at 0x%08x\n",
			DRV_NAME, PERVASIVE_RESET_BASE);
		return -ENOMEM;
	}
	pr_info("%s: Mapped pervasive reset at 0x%08x\n", DRV_NAME, PERVASIVE_RESET_BASE);

	g_pers_gate = ioremap_nocache(PERVASIVE_GATE_BASE, 0x1000);
	if (!g_pers_gate) {
		pr_err("%s: Failed to ioremap pervasive gate at 0x%08x\n",
			DRV_NAME, PERVASIVE_GATE_BASE);
		return -ENOMEM;
	}
	pr_info("%s: Mapped pervasive gate at 0x%08x\n", DRV_NAME, PERVASIVE_GATE_BASE);

	g_pers_baseclk = ioremap_nocache(PERVASIVE_BASECLK_BASE, 0x1000);
	if (!g_pers_baseclk) {
		pr_err("%s: Failed to ioremap pervasive baseclk at 0x%08x\n",
			DRV_NAME, PERVASIVE_BASECLK_BASE);
		return -ENOMEM;
	}
	pr_info("%s: Mapped pervasive baseclk at 0x%08x\n", DRV_NAME, PERVASIVE_BASECLK_BASE);

	/* Power on sequence: deassert reset, enable clock */
	ret = vita_gpu_power_on();
	if (ret) {
		pr_err("%s: GPU power-on failed: %d\n", DRV_NAME, ret);
		return ret;
	}

	/* Map SGX MMIO register block */
	g_sgx_regs = ioremap_nocache(SGX_REG_BASE, SGX_REG_SIZE);
	if (!g_sgx_regs) {
		pr_err("%s: Failed to ioremap SGX registers at 0x%08x (size 0x%x)\n",
			DRV_NAME, SGX_REG_BASE, SGX_REG_SIZE);
		vita_gpu_power_off();
		return -ENOMEM;
	}
	pr_info("%s: Mapped SGX registers at 0x%08x (size 0x%x)\n",
		DRV_NAME, SGX_REG_BASE, SGX_REG_SIZE);

	/* Read ID from master bank */
	ret = vita_gpu_read_sgx_id(-1, "master");
	if (ret) {
		pr_err("%s: SGX master ID read failed — stopping probe\n", DRV_NAME);
		vita_gpu_power_off();
		return ret;
	}

	/* Read ID from each core bank */
	for (i = 0; i < 4; i++) {
		char tag[16];
		snprintf(tag, sizeof(tag), "core%d", i);
		vita_gpu_read_sgx_id(i, tag);
	}

	/* Read additional master status registers for documentation */
	pr_info("%s: SGX master ClockGate: 0x%08x\n",
		DRV_NAME, readl(g_sgx_regs + SGX_SYS_CLOCK_GATE));
	pr_info("%s: SGX master Reset:     0x%08x\n",
		DRV_NAME, readl(g_sgx_regs + SGX_SYS_RESET));

	pr_info("%s: Read-only probe completed successfully\n", DRV_NAME);

	/* Power off cleanly — leave GPU as we found it */
	vita_gpu_power_off();

	/* Success — but don't keep anything bound. This is a one-shot probe. */
	return 0;
}

static void __exit vita_gpu_probe_exit(void)
{
	vita_gpu_power_off();
	/* iounmap not strictly needed for built-in, but clean */
}

late_initcall(vita_gpu_probe_init);
module_exit(vita_gpu_probe_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AndroidOnVita");
MODULE_DESCRIPTION("Vita SGX543MP4+ Read-Only Probe (G2)");
