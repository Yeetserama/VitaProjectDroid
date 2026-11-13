#!/usr/bin/env python3
"""Apply the small, text-only kernel-tree changes needed by this project.

The ashmem, logger, and lowmemorykiller drivers are copied into
drivers/misc/ashmem, drivers/misc/logger, drivers/misc/lowmemorykiller by
apply-kernel.sh; this script wires them into Kconfig/Makefile, adds the Vita
DTBs to the ARM dts Makefile, and selects the 32-bit binder ABI.  All edits
are idempotent.
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


def patch_misc_kconfig(tree: str) -> bool:
    path = os.path.join(tree, "drivers/misc/Kconfig")
    text = read(path)
    changed = False
    if 'source "drivers/misc/ashmem/Kconfig"' not in text:
        lines = text.splitlines()
        indices = [i for i, line in enumerate(lines) if line.strip() == "endmenu"]
        if not indices:
            raise ValueError(f"{path}: no endmenu found")
        index = indices[-1]
        lines.insert(index, 'source "drivers/misc/ashmem/Kconfig"')
        text = "\n".join(lines) + "\n"
        changed = True

    if 'source "drivers/misc/logger/Kconfig"' not in text:
        lines = text.splitlines()
        indices = [i for i, line in enumerate(lines) if line.strip() == "endmenu"]
        if not indices:
            raise ValueError(f"{path}: no endmenu found")
        index = indices[-1]
        lines.insert(index, 'source "drivers/misc/logger/Kconfig"')
        text = "\n".join(lines) + "\n"
        changed = True

    if 'source "drivers/misc/lowmemorykiller/Kconfig"' not in text:
        lines = text.splitlines()
        indices = [i for i, line in enumerate(lines) if line.strip() == "endmenu"]
        if not indices:
            raise ValueError(f"{path}: no endmenu found")
        index = indices[-1]
        lines.insert(index, 'source "drivers/misc/lowmemorykiller/Kconfig"')
        text = "\n".join(lines) + "\n"
        changed = True

    if changed:
        write(path, text)
    return changed


def patch_misc_makefile(tree: str) -> bool:
    path = os.path.join(tree, "drivers/misc/Makefile")
    text = read(path)
    changed = False
    if "ashmem/" not in text:
        if not text.endswith("\n"):
            text += "\n"
        text += "obj-$(CONFIG_ASHMEM) += ashmem/\n"
        changed = True
    if "logger/" not in text:
        if not text.endswith("\n"):
            text += "\n"
        text += "obj-$(CONFIG_LOG) += logger/\n"
        changed = True
    if "lowmemorykiller/" not in text:
        if not text.endswith("\n"):
            text += "\n"
        text += "obj-$(CONFIG_ANDROID_LOW_MEMORY_KILLER) += lowmemorykiller/\n"
        changed = True
    if changed:
        write(path, text)
    return changed


def patch_gpu_probe_kconfig(tree: str) -> bool:
    path = os.path.join(tree, "drivers/gpu/Kconfig")
    if not os.path.exists(path):
        # GPU subsystem may not exist in this kernel config; create minimal Kconfig
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write("# SPDX-License-Identifier: GPL-2.0\n\nmenu \"GPU drivers\"\n\nendmenu\n")
    text = read(path)
    if 'source "drivers/gpu/vita/Kconfig"' in text:
        return False
    lines = text.splitlines()
    indices = [i for i, line in enumerate(lines) if line.strip() == "endmenu"]
    if not indices:
        raise ValueError(f"{path}: no endmenu found")
    index = indices[-1]
    lines.insert(index, 'source "drivers/gpu/vita/Kconfig"')
    write(path, "\n".join(lines) + "\n")
    return True


def patch_gpu_probe_makefile(tree: str) -> bool:
    path = os.path.join(tree, "drivers/gpu/Makefile")
    if not os.path.exists(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write("# SPDX-License-Identifier: GPL-2.0\n\n")
    text = read(path)
    if "vita/" in text:
        return False
    if not text.endswith("\n"):
        text += "\n"
    text += "obj-$(CONFIG_VITA_GPU_PROBE) += vita/\n"
    write(path, text)
    return True


def patch_psp2sgx_kconfig(tree: str) -> bool:
    path = os.path.join(tree, "drivers/gpu/drm/Kconfig")
    if not os.path.exists(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write("# SPDX-License-Identifier: GPL-2.0\n\nmenu \"DRM\"\n\nendmenu\n")
    text = read(path)
    if 'source "drivers/gpu/drm/psp2sgx/Kconfig"' in text:
        return False
    lines = text.splitlines()
    indices = [i for i, line in enumerate(lines) if line.strip() == "endmenu"]
    if not indices:
        raise ValueError(f"{path}: no endmenu found")
    index = indices[-1]
    lines.insert(index, 'source "drivers/gpu/drm/psp2sgx/Kconfig"')
    write(path, "\n".join(lines) + "\n")
    return True


def patch_psp2sgx_makefile(tree: str) -> bool:
    path = os.path.join(tree, "drivers/gpu/drm/Makefile")
    if not os.path.exists(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write("# SPDX-License-Identifier: GPL-2.0\n\n")
    text = read(path)
    if "psp2sgx/" in text:
        return False
    if not text.endswith("\n"):
        text += "\n"
    text += "obj-$(CONFIG_DRM_PSP2SGX) += psp2sgx/\n"
    write(path, text)
    return True


def patch_binder_makefile(tree: str) -> bool:
    """Select the 32-bit binder ABI.

    Android 4.2/5.1 userspace is 32-bit and libbinder rejects any driver whose
    protocol version is not 7.  Linux 6.12's binder.h only reports 7 (and only
    uses 32-bit binder_uintptr_t/binder_size_t) when BINDER_IPC_32BIT is
    defined.  Mainline no longer exposes a Kconfig symbol for it, so define it
    for every object in drivers/android/.
    """
    path = os.path.join(tree, "drivers/android/Makefile")
    text = read(path)
    if "BINDER_IPC_32BIT" in text:
        return False
    if not text.endswith("\n"):
        text += "\n"
    text += (
        "\n# AndroidOnVita: 32-bit ARM Android userspace expects binder\n"
        "# protocol 7 and 32-bit binder_uintptr_t/binder_size_t.\n"
        "ccflags-y += -DBINDER_IPC_32BIT\n"
    )
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
        if patch_misc_kconfig(tree):
            changed.append("drivers/misc/Kconfig")
        if patch_misc_makefile(tree):
            changed.append("drivers/misc/Makefile")
        if patch_gpu_probe_kconfig(tree):
            changed.append("drivers/gpu/Kconfig")
        if patch_gpu_probe_makefile(tree):
            changed.append("drivers/gpu/Makefile")
        if patch_psp2sgx_kconfig(tree):
            changed.append("drivers/gpu/drm/Kconfig")
        if patch_psp2sgx_makefile(tree):
            changed.append("drivers/gpu/drm/Makefile")
        if patch_binder_makefile(tree):
            changed.append("drivers/android/Makefile")
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