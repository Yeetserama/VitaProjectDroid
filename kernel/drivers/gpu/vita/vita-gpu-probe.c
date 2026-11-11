/*
 * Vita GPU Read-Only Probe (G2)
 *
 * Purpose: Power on SGX block, read ID/revision/status registers, print to dmesg.
 * NO GPU WRITES beyond clock/reset enable.
 * Built-in only (CONFIG_MODULES=off in Vita kernel).
 *
 * Gate: Plausible SGX core ID + no bus hang.
 *
 * Based on openpvrsgx pvr_device.c probe sequence.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#define DRV_NAME "vita-gpu-probe"

/* SGX System Register Offsets (from openpvrsgx + Vita3K) */
#define SGX_SYS_ID            0x0000  /* Core ID / Revision */
#define SGX_SYS_STATUS        0x0004  /* Status register */
#define SGX_SYS_CONFIG        0x0008  /* Configuration */
#define SGX_SYS_CLOCK_GATE    0x0010  /* Clock gating control */
#define SGX_SYS_RESET         0x0014  /* Soft reset control */
#define SGX_SYS_MMU_BASE      0x1000  /* MMU page table base */
#define SGX_SYS_MMU_FAULT     0x1004  /* MMU fault address */
#define SGX_SYS_MMU_STATUS    0x1008  /* MMU fault status */
#define SGX_SYS_TA_CTRL       0x2000  /* TA queue control */
#define SGX_SYS_TA_STATUS     0x2004  /* TA queue status */
#define SGX_SYS_3D_CTRL       0x3000  /* 3D queue control */
#define SGX_SYS_3D_STATUS     0x3004  /* 3D queue status */
#define SGX_SYS_CDM_CTRL      0x4000  /* CDM queue control (Series5XT+) */
#define SGX_SYS_CDM_STATUS    0x4004  /* CDM queue status */
#define SGX_SYS_IRQ_STATUS    0x5000  /* Interrupt status */
#define SGX_SYS_IRQ_MASK      0x5004  /* Interrupt mask */
#define SGX_SYS_IRQ_ACK       0x5008  /* Interrupt acknowledge */
#define SGX_SYS_POWER_CTRL    0x6000  /* Power domain control */
#define SGX_SYS_POWER_STATUS  0x6004  /* Power domain status */
#define SGX_SYS_FW_BASE       0x7000  /* Firmware load base */
#define SGX_SYS_FW_STATUS     0x7004  /* Firmware status */

/* Expected SGX543MP4+ ID values (from openpvrsgx) */
#define SGX543_CORE_ID        0x5430  /* SGX543 core ID */
#define SGX544_CORE_ID        0x5440  /* SGX544 core ID */
#define SERIES5XT_MARKER      0x5000  /* Series5XT family */

struct vita_gpu_probe {
	void __iomem *regs;
	struct clk *clk;
	struct reset_control *rst;
	struct device *dev;
	phys_addr_t phys_base;
	resource_size_t size;
	int irq;
};

static int vita_gpu_probe_read_id(struct vita_gpu_probe *probe)
{
	u32 id, rev, status, config;

	if (!probe->regs) {
		dev_err(probe->dev, "Registers not mapped\n");
		return -ENODEV;
	}

	/* Read core identification registers */
	id      = readl(probe->regs + SGX_SYS_ID);
	rev     = readl(probe->regs + SGX_SYS_STATUS);  /* Often holds revision */
	config  = readl(probe->regs + SGX_SYS_CONFIG);
	status  = readl(probe->regs + SGX_SYS_STATUS);

	dev_info(probe->dev, "SGX ID register:      0x%08x\n", id);
	dev_info(probe->dev, "SGX Status/Revision:  0x%08x\n", rev);
	dev_info(probe->dev, "SGX Config:           0x%08x\n", config);
	dev_info(probe->dev, "SGX Status:           0x%08x\n", status);

	/* Check for plausible SGX5 family ID */
	if ((id & 0xF000) == SERIES5XT_MARKER) {
		dev_info(probe->dev, "✓ Detected Series5XT family (0x%04x)\n", id & 0xFFFF);
		if ((id & 0xFFFF) == SGX543_CORE_ID)
			dev_info(probe->dev, "  → Matches SGX543 core ID\n");
		else if ((id & 0xFFFF) == SGX544_CORE_ID)
			dev_info(probe->dev, "  → Matches SGX544 core ID\n");
		else
			dev_info(probe->dev, "  → Unknown Series5XT variant: 0x%04x\n", id & 0xFFFF);
		return 0;
	}

	dev_warn(probe->dev, "⚠ Unexpected ID register value: 0x%08x (expected Series5XT 0x5xxx)\n", id);
	return -ENODEV;
}

static int vita_gpu_probe_power_on(struct vita_gpu_probe *probe)
{
	int ret;

	/* 1. Enable clock */
	if (probe->clk) {
		ret = clk_prepare_enable(probe->clk);
		if (ret) {
			dev_err(probe->dev, "Failed to enable clock: %d\n", ret);
			return ret;
		}
		dev_info(probe->dev, "Clock enabled\n");
	}

	/* 2. Deassert reset */
	if (probe->rst) {
		ret = reset_control_deassert(probe->rst);
		if (ret) {
			dev_err(probe->dev, "Failed to deassert reset: %d\n", ret);
			if (probe->clk)
				clk_disable_unprepare(probe->clk);
			return ret;
		}
		dev_info(probe->dev, "Reset deasserted\n");
	}

	/* 3. Wait for GPU to stabilize */
	udelay(100);
	msleep(10);

	/* 4. Check power status if register exists */
	if (probe->regs) {
		u32 pwr_status = readl(probe->regs + SGX_SYS_POWER_STATUS);
		dev_info(probe->dev, "Power status: 0x%08x\n", pwr_status);
	}

	return 0;
}

static void vita_gpu_probe_power_off(struct vita_gpu_probe *probe)
{
	if (probe->rst) {
		reset_control_assert(probe->rst);
		dev_info(probe->dev, "Reset asserted\n");
	}
	if (probe->clk) {
		clk_disable_unprepare(probe->clk);
		dev_info(probe->dev, "Clock disabled\n");
	}
}

static int vita_gpu_probe_dt(struct platform_device *pdev, struct vita_gpu_probe *probe)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource res;
	int ret;

	/* Get register base from DT */
	ret = of_address_to_resource(np, 0, &res);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get register resource: %d\n", ret);
		return ret;
	}
	probe->phys_base = res.start;
	probe->size = resource_size(&res);

	/* Get clock */
	probe->clk = devm_clk_get(&pdev->dev, "gpu");
	if (IS_ERR(probe->clk)) {
		dev_warn(&pdev->dev, "No GPU clock in DT: %ld\n", PTR_ERR(probe->clk));
		probe->clk = NULL;
	}

	/* Get reset */
	probe->rst = devm_reset_control_get(&pdev->dev, "gpu");
	if (IS_ERR(probe->rst)) {
		dev_warn(&pdev->dev, "No GPU reset in DT: %ld\n", PTR_ERR(probe->rst));
		probe->rst = NULL;
	}

	/* Get IRQ (optional for read-only probe) */
	probe->irq = of_irq_get(np, 0);
	if (probe->irq < 0)
		dev_info(&pdev->dev, "No IRQ in DT (read-only probe doesn't need it)\n");
	else
		dev_info(&pdev->dev, "IRQ: %d\n", probe->irq);

	return 0;
}

static int vita_gpu_probe_probe(struct platform_device *pdev)
{
	struct vita_gpu_probe *probe;
	int ret;

	probe = devm_kzalloc(&pdev->dev, sizeof(*probe), GFP_KERNEL);
	if (!probe)
		return -ENOMEM;

	probe->dev = &pdev->dev;
	platform_set_drvdata(pdev, probe);

	/* Parse DT */
	ret = vita_gpu_probe_dt(pdev, probe);
	if (ret)
		return ret;

	/* Map registers READ-ONLY */
	probe->regs = devm_ioremap(&pdev->dev, probe->phys_base, probe->size);
	if (!probe->regs) {
		dev_err(&pdev->dev, "Failed to ioremap registers at 0x%pa (size 0x%zx)\n",
			&probe->phys_base, probe->size);
		return -ENOMEM;
	}
	dev_info(&pdev->dev, "Registers mapped at 0x%pa (size 0x%zx)\n",
		 &probe->phys_base, probe->size);

	/* Power on sequence */
	ret = vita_gpu_probe_power_on(probe);
	if (ret)
		return ret;

	/* Read ID registers (THE MAIN TEST) */
	ret = vita_gpu_probe_read_id(probe);
	if (ret) {
		dev_err(&pdev->dev, "ID read failed — stopping probe\n");
		vita_gpu_probe_power_off(probe);
		return ret;
	}

	/* Read additional status registers for documentation */
	if (probe->regs) {
		dev_info(&pdev->dev, "MMU Base:      0x%08x\n", readl(probe->regs + SGX_SYS_MMU_BASE));
		dev_info(&pdev->dev, "MMU Status:    0x%08x\n", readl(probe->regs + SGX_SYS_MMU_STATUS));
		dev_info(&pdev->dev, "TA Status:     0x%08x\n", readl(probe->regs + SGX_SYS_TA_STATUS));
		dev_info(&pdev->dev, "3D Status:     0x%08x\n", readl(probe->regs + SGX_SYS_3D_STATUS));
		dev_info(&pdev->dev, "IRQ Status:    0x%08x\n", readl(probe->regs + SGX_SYS_IRQ_STATUS));
		dev_info(&pdev->dev, "FW Base:       0x%08x\n", readl(probe->regs + SGX_SYS_FW_BASE));
		dev_info(&pdev->dev, "FW Status:     0x%08x\n", readl(probe->regs + SGX_SYS_FW_STATUS));
	}

	dev_info(&pdev->dev, "✓ Read-only probe completed successfully\n");

	/* Power off cleanly */
	vita_gpu_probe_power_off(probe);

	/* Success — but we don't bind permanently; this is a one-shot probe */
	return -ENODEV;  /* Don't keep driver bound */
}

static const struct of_device_id vita_gpu_probe_of_match[] = {
	{ .compatible = "img,sgx543mp4", .data = NULL },  /* Expected Vita GPU */
	{ .compatible = "img,sgx543", .data = NULL },
	{ .compatible = "img,sgx544", .data = NULL },
	{ .compatible = "img,sgx", .data = NULL },
	{},
};
MODULE_DEVICE_TABLE(of, vita_gpu_probe_of_match);

static struct platform_driver vita_gpu_probe_driver = {
	.probe = vita_gpu_probe_probe,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = vita_gpu_probe_of_match,
	},
};

/* Built-in only — no module_init/module_exit */
builtin_platform_driver(vita_gpu_probe_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AndroidOnVita");
MODULE_DESCRIPTION("Vita SGX543MP4+ Read-Only Probe (G2)");
MODULE_ALIAS("platform:vita-gpu-probe");