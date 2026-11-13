#!/usr/bin/env python3
"""Add a read-only SD8787 Bluetooth probe to btmrvl_sdio.c.

This patch is only used by the re/bt diagnostic build (BT_DIAG=1).  It:

  - makes the new ``probe_only`` parameter default to true;
  - enables SDIO function 2 and dumps registers 0x00-0x7f;
  - polls the firmware status registers 0x60/0x61 for two seconds;
  - returns -ENODEV without claiming an IRQ or downloading firmware.

The kernel tree is fetched as a tarball, so there is no git history to apply
a context diff against.  Exact-match insertions fail loudly if the pinned
source changes.

Usage:
    patch-btmrvl-diag.py path/to/drivers/bluetooth/btmrvl_sdio.c
"""

from __future__ import annotations

import pathlib
import sys

PARAM_ANCHOR = '#define VERSION "1.0"\n'

PARAM_BLOCK = '''
/*
 * re/bt D1 diagnostic: leave the card alone after a read-only register dump.
 * The default is true so the diagnostic build needs no extra boot arguments;
 * pass btmrvl_sdio.probe_only=0 to restore the normal probe.
 */
static bool probe_only = true;
module_param(probe_only, bool, 0444);
MODULE_PARM_DESC(probe_only,
		 "Dump SD8787 Bluetooth registers and skip firmware download");
'''

FUNCS_ANCHOR = '''static int btmrvl_sdio_unregister_dev(struct btmrvl_sdio_card *card)
{
'''

FUNCS_BLOCK = '''static void btmrvl_sdio_diag_dump(struct btmrvl_sdio_card *card)
{
	struct sdio_func *func = card->func;
	char buf[80];
	char *ptr;
	int i, ret;
	unsigned int reg;
	u8 data;

	sdio_claim_host(func);

	dev_info(&func->dev,
		 "BT-DIAG: function %u vendor=0x%04x device=0x%04x\\n",
		 func->num, func->vendor, func->device);

	for (reg = 0; reg <= 0x7f; reg += 16) {
		ptr = buf;
		ptr += sprintf(ptr, "BT-DIAG: %02x:", reg);
		for (i = 0; i < 16; i++) {
			data = sdio_readb(func, reg + i, &ret);
			if (ret)
				ptr += sprintf(ptr, " ??");
			else
				ptr += sprintf(ptr, " %02x", data);
		}
		dev_info(&func->dev, "%s\\n", buf);
	}

	sdio_release_host(func);

	dev_info(&func->dev, "BT-DIAG: polling fw status 0x%02x/0x%02x for 2 s\\n",
		 card->reg->card_fw_status0, card->reg->card_fw_status1);
	for (i = 0; i < 20; i++) {
		u8 fws0, fws1;
		int err0, err1;

		/* Do not hold the host across msleep(): function 1 shares it. */
		sdio_claim_host(func);
		fws0 = sdio_readb(func, card->reg->card_fw_status0, &err0);
		fws1 = sdio_readb(func, card->reg->card_fw_status1, &err1);
		sdio_release_host(func);

		dev_info(&func->dev,
			 "BT-DIAG: t=%3d ms fw0=%02x(%d) fw1=%02x(%d) status=0x%04x\\n",
			 i * 100, fws0, err0, fws1, err1,
			 (u16)((fws1 << 8) | fws0));
		msleep(100);
	}

	sdio_claim_host(func);
	ptr = buf;
	ptr += sprintf(ptr, "BT-DIAG: f0:");
	for (reg = 0; reg <= 9; reg++) {
		data = sdio_f0_readb(func, reg, &ret);
		if (ret)
			ptr += sprintf(ptr, " ??");
		else
			ptr += sprintf(ptr, " %02x", data);
	}
	dev_info(&func->dev, "%s\\n", buf);
	sdio_release_host(func);
}

static int btmrvl_sdio_probe_only(struct btmrvl_sdio_card *card)
{
	struct sdio_func *func = card->func;
	int ret;

	dev_info(&func->dev, "BT-DIAG: probe-only build, no firmware download\\n");

	sdio_claim_host(func);
	ret = sdio_enable_func(func);
	sdio_release_host(func);
	if (ret) {
		dev_err(&func->dev, "BT-DIAG: sdio_enable_func failed: %d\\n", ret);
		/*
		 * Still return success.  A failed probe or a disabled
		 * function can disturb Wi-Fi on this shared combo card.
		 */
		return 0;
	}

	btmrvl_sdio_diag_dump(card);

	dev_info(&func->dev,
		 "BT-DIAG: dump complete; leaving function 2 enabled and returning success\\n");

	return 0;
}

'''

PROBE_ANCHOR = '''	if (btmrvl_sdio_register_dev(card) < 0) {
'''

PROBE_BLOCK = '''	if (probe_only)
		return btmrvl_sdio_probe_only(card);

'''

MARKER = "BT-DIAG: probe-only build"


def replace_once(text: str, old: str, new: str, what: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(
            f"patch-btmrvl-diag: expected one {what}, found {count}"
        )
    return text.replace(old, new, 1)


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2

    path = pathlib.Path(sys.argv[1])
    text = path.read_text(encoding="utf-8")

    if MARKER in text:
        print(f"patch-btmrvl-diag: {path} is already patched")
        return 0

    text = replace_once(text, PARAM_ANCHOR, PARAM_ANCHOR + PARAM_BLOCK,
                        "VERSION define")
    text = replace_once(text, FUNCS_ANCHOR, FUNCS_BLOCK + FUNCS_ANCHOR,
                        "unregister_dev definition")
    text = replace_once(text, PROBE_ANCHOR, PROBE_BLOCK + PROBE_ANCHOR,
                        "register_dev call in probe")

    path.write_text(text, encoding="utf-8")
    print(f"patch-btmrvl-diag: patched {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
