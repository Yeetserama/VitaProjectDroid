#!/usr/bin/env python3
"""Unpack a newc/crc cpio archive, optionally gzip/bzip2/xz compressed.

This exists so the initramfs build does not depend on a host ``cpio`` binary.
It preserves directories, regular files, symlinks, hardlinks, FIFOs and device
nodes.  Symlinks are never followed, which matters for the Android ramdisk:
``/sbin/ueventd`` and friends are symlinks back to ``/init``.

Usage:
    unpack_cpio.py ARCHIVE DESTDIR
    unpack_cpio.py --dry-run ARCHIVE
"""

from __future__ import annotations

import argparse
import bz2
import gzip
import lzma
import os
import stat
import sys
from typing import BinaryIO

MAGIC_NEWC = b"070701"
MAGIC_CRC = b"070702"
TRAILER = b"TRAILER!!!"

HEADER_LEN = 110


def open_archive(path: str) -> BinaryIO:
    with open(path, "rb") as probe:
        magic = probe.read(6)
    if magic[:2] == b"\x1f\x8b":
        return gzip.open(path, "rb")
    if magic[:3] == b"BZh":
        return bz2.open(path, "rb")
    if magic[:6] == b"\xfd7zXZ\x00":
        return lzma.open(path, "rb")
    if magic in (MAGIC_NEWC, MAGIC_CRC):
        return open(path, "rb")
    raise ValueError(
        f"{path}: not a newc cpio archive or a supported compressed stream "
        f"(magic {magic!r})"
    )


def read_exact(f: BinaryIO, count: int, what: str) -> bytes:
    data = f.read(count)
    if len(data) != count:
        raise ValueError(f"truncated {what}: wanted {count} bytes, got {len(data)}")
    return data


def read_entry(f: BinaryIO):
    magic = read_exact(f, 6, "cpio magic")
    if magic not in (MAGIC_NEWC, MAGIC_CRC):
        raise ValueError(f"bad cpio magic {magic!r}")

    values = []
    for field in (
        "ino",
        "mode",
        "uid",
        "gid",
        "nlink",
        "mtime",
        "filesize",
        "devmajor",
        "devminor",
        "rdevmajor",
        "rdevminor",
        "namesize",
        "check",
    ):
        raw = read_exact(f, 8, f"cpio {field}")
        try:
            values.append(int(raw, 16))
        except ValueError as exc:
            raise ValueError(f"invalid hex in cpio {field}: {raw!r}") from exc

    header = dict(zip(
        (
            "ino",
            "mode",
            "uid",
            "gid",
            "nlink",
            "mtime",
            "filesize",
            "devmajor",
            "devminor",
            "rdevmajor",
            "rdevminor",
            "namesize",
            "check",
        ),
        values,
    ))

    namesize = header["namesize"]
    if namesize < 1:
        raise ValueError("cpio entry has an empty name")
    name_raw = read_exact(f, namesize, "cpio name")
    if name_raw[-1:] != b"\0":
        raise ValueError(f"cpio name is not NUL terminated: {name_raw!r}")
    name = name_raw[:-1]

    # The header plus the NUL-terminated name is padded to a 4-byte boundary.
    name_pad = (-(HEADER_LEN + namesize)) % 4
    if name_pad:
        read_exact(f, name_pad, "cpio name padding")

    data = read_exact(f, header["filesize"], f"cpio data for {name!r}")
    data_pad = (-header["filesize"]) % 4
    if data_pad:
        read_exact(f, data_pad, f"cpio data padding for {name!r}")

    return header, name, data


def safe_relative_path(name: bytes) -> str:
    text = name.decode("utf-8", "surrogateescape")
    if text.startswith("/"):
        raise ValueError(f"absolute path in archive: {text!r}")
    if "\\" in text:
        raise ValueError(f"backslash in archive path: {text!r}")
    parts = [part for part in text.split("/") if part not in ("", ".")]
    if any(part == ".." for part in parts):
        raise ValueError(f"path traversal in archive: {text!r}")
    return os.path.join(*parts) if parts else ""


def extract(archive: str, dest: str, dry_run: bool = False) -> int:
    dest_abs = os.path.abspath(dest)
    if not dry_run:
        os.makedirs(dest_abs, exist_ok=True)

    dir_modes: list[tuple[str, int]] = []
    hardlinks: dict[tuple[int, int, int], str] = {}
    count = 0
    total_bytes = 0

    with open_archive(archive) as f:
        while True:
            header, name, data = read_entry(f)
            if name == TRAILER:
                break

            rel = safe_relative_path(name)
            if not rel:
                continue
            target = os.path.abspath(os.path.join(dest_abs, rel))
            if os.path.commonpath((dest_abs, target)) != dest_abs:
                raise ValueError(f"archive entry escapes destination: {name!r}")

            mode = header["mode"]
            count += 1
            total_bytes += len(data)

            if dry_run:
                if stat.S_ISDIR(mode):
                    pass
                continue

            parent = os.path.dirname(target)
            if parent:
                os.makedirs(parent, exist_ok=True)

            if stat.S_ISDIR(mode):
                os.makedirs(target, exist_ok=True)
                dir_modes.append((target, mode))
            elif stat.S_ISLNK(mode):
                if os.path.lexists(target):
                    os.unlink(target)
                os.symlink(os.fsdecode(data), target)
            elif stat.S_ISREG(mode):
                key = (header["devmajor"], header["devminor"], header["ino"])
                if (
                    header["nlink"] > 1
                    and key in hardlinks
                    and os.path.exists(hardlinks[key])
                ):
                    if os.path.lexists(target):
                        os.unlink(target)
                    try:
                        os.link(hardlinks[key], target)
                    except OSError:
                        with open(target, "wb") as out:
                            out.write(data)
                else:
                    with open(target, "wb") as out:
                        out.write(data)
                    hardlinks.setdefault(key, target)
                os.chmod(target, stat.S_IMODE(mode))
            elif stat.S_ISFIFO(mode):
                os.mkfifo(target, stat.S_IMODE(mode))
            elif stat.S_ISCHR(mode) or stat.S_ISBLK(mode):
                if not hasattr(os, "mknod"):
                    raise ValueError(f"cannot create device node on this host: {rel}")
                os.mknod(
                    target,
                    mode,
                    os.makedev(header["rdevmajor"], header["rdevminor"]),
                )
            else:
                print(f"unpack_cpio: skipping unsupported file type: {rel}", file=sys.stderr)

            if header["mtime"]:
                try:
                    os.utime(target, (header["mtime"], header["mtime"]), follow_symlinks=False)
                except (NotImplementedError, OSError):
                    pass

    if not dry_run:
        # Permissions are applied last: some directories are not writable after
        # their final mode is set, and children have to be created first.
        for target, mode in sorted(
            dir_modes, key=lambda item: item[0].count(os.sep), reverse=True
        ):
            os.chmod(target, stat.S_IMODE(mode))

    print(
        f"unpack_cpio: {count} entries, {total_bytes} bytes of file data"
        + (" (dry run)" if dry_run else f" -> {dest_abs}")
    )
    return count


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="parse only, do not write")
    parser.add_argument("archive")
    parser.add_argument("dest", nargs="?", help="destination directory")
    args = parser.parse_args(argv)

    if not args.dry_run and not args.dest:
        parser.error("DESTDIR is required unless --dry-run is used")

    try:
        extract(args.archive, args.dest or "", args.dry_run)
    except (OSError, ValueError) as exc:
        print(f"unpack_cpio: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
