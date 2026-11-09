#!/usr/bin/env python3
"""Apply the small, text-only kernel-tree changes needed by this project.

The ashmem driver itself is copied into drivers/misc/ashmem by
apply-kernel.sh; this script only wires it into Kconfig/Makefile and adds the
Vita DTBs to the ARM dts Makefile.  All edits are idempotent.
"""

from __future__ import annotations

import argparse
import os
import sys


def read(path: str) -> str:
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def write(path: str, text: str) -> None:
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def patch_kconfig(tree: str) -> bool:
    path = os.path.join(tree, "drivers/misc/Kconfig")
    text = read(path)
    if 'source "drivers/misc/ashmem/Kconfig"' in text:
        return False

    lines = text.splitlines()
    indices = [i for i, line in enumerate(lines) if line.strip() == "endmenu"]
    if not indices:
        raise ValueError(f"{path}: no endmenu found")
    index = indices[-1]
    lines.insert(index, 'source "drivers/misc/ashmem/Kconfig"')
    write(path, "\n".join(lines) + "\n")
    return True


def patch_makefile(tree: str) -> bool:
    path = os.path.join(tree, "drivers/misc/Makefile")
    text = read(path)
    if "ashmem/" in text:
        return False
    if not text.endswith("\n"):
        text += "\n"
    text += "obj-$(CONFIG_ASHMEM) += ashmem/\n"
    write(path, text)
    return True


def patch_dts_makefile(tree: str) -> bool:
    path = os.path.join(tree, "arch/arm/boot/dts/Makefile")
    text = read(path)
    if "vita1000.dtb" in text:
        return False
    if not text.endswith("\n"):
        text += "\n"
    text += "\n# AndroidOnVita: Sony PlayStation Vita boards\ndtb-y += vita1000.dtb vita2000.dtb pstv.dtb\n"
    write(path, text)
    return True


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel_tree", help="Linux kernel source tree")
    args = parser.parse_args(argv)

    tree = os.path.abspath(args.kernel_tree)
    if not os.path.isfile(os.path.join(tree, "Makefile")):
        print(f"patch-kernel-tree: {tree}: not a kernel tree", file=sys.stderr)
        return 1

    try:
        changed = []
        if patch_kconfig(tree):
            changed.append("drivers/misc/Kconfig")
        if patch_makefile(tree):
            changed.append("drivers/misc/Makefile")
        if patch_dts_makefile(tree):
            changed.append("arch/arm/boot/dts/Makefile")
    except (OSError, ValueError) as exc:
        print(f"patch-kernel-tree: {exc}", file=sys.stderr)
        return 1

    if changed:
        print("patch-kernel-tree: patched " + ", ".join(changed))
    else:
        print("patch-kernel-tree: already patched")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
