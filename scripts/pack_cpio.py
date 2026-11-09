#!/usr/bin/env python3
"""Create an uncompressed newc cpio archive for the kernel initramfs.

Every entry is written as uid 0 / gid 0, independent of who runs the build.
The kernel treats ``CONFIG_INITRAMFS_SOURCE`` as a direct cpio image when the
file name ends in ``.cpio`` (see usr/Makefile), so no host cpio or fakeroot is
needed.

Usage:
    pack_cpio.py ROOT OUT.cpio [--mtime EPOCH]
"""

from __future__ import annotations

import argparse
import os
import stat
import sys

MAGIC = b"070701"
TRAILER = b"TRAILER!!!"
HEADER_LEN = 110


def iter_entries(root: str):
    """Yield (relative name, absolute path, lstat) with parents before children."""

    def walk(directory: str, prefix: str):
        with os.scandir(directory) as scan:
            entries = sorted(scan, key=lambda entry: entry.name)
        for entry in entries:
            name = f"{prefix}/{entry.name}" if prefix else entry.name
            st = entry.stat(follow_symlinks=False)
            yield name, entry.path, st
            if stat.S_ISDIR(st.st_mode):
                yield from walk(entry.path, name)

    yield from walk(root, "")


def entry_bytes(name: str, ino: int, mode: int, mtime: int, data: bytes,
                rdevmajor: int = 0, rdevminor: int = 0) -> bytes:
    name_bytes = name.encode("utf-8", "surrogateescape") + b"\0"
    fields = (
        ino,
        mode,
        0,  # uid
        0,  # gid
        1,  # nlink (we do not emit hardlinks; duplicate data instead)
        mtime,
        len(data),
        0,  # devmajor
        0,  # devminor
        rdevmajor,
        rdevminor,
        len(name_bytes),
        0,  # check
    )
    for value in fields:
        if value < 0 or value > 0xFFFFFFFF:
            raise ValueError(f"cpio field out of range for {name!r}: {value}")
    header = MAGIC + b"".join(f"{value:08x}".encode("ascii") for value in fields)
    if len(header) != HEADER_LEN:
        raise AssertionError("bad newc header length")

    out = bytearray(header)
    out += name_bytes
    out += b"\0" * ((-len(out)) % 4)
    out += data
    out += b"\0" * ((-len(data)) % 4)
    return bytes(out)


def pack(root: str, output: str, mtime: int) -> tuple[int, int]:
    root_abs = os.path.abspath(root)
    if not os.path.isdir(root_abs):
        raise ValueError(f"{root_abs}: not a directory")

    entries = 0
    data_bytes = 0
    ino = 1
    tmp = output + ".tmp"

    with open(tmp, "wb") as out:
        for name, path, st in iter_entries(root_abs):
            mode = st.st_mode & 0xFFFF
            if stat.S_ISREG(st.st_mode):
                with open(path, "rb") as src:
                    data = src.read()
            elif stat.S_ISLNK(st.st_mode):
                data = os.readlink(path).encode("utf-8", "surrogateescape")
            elif stat.S_ISDIR(st.st_mode):
                data = b""
            elif stat.S_ISFIFO(st.st_mode):
                data = b""
            elif stat.S_ISCHR(st.st_mode) or stat.S_ISBLK(st.st_mode):
                data = b""
            else:
                raise ValueError(f"{name}: unsupported file type (mode {mode:o})")

            rdevmajor = os.major(st.st_rdev) if stat.S_ISCHR(st.st_mode) or stat.S_ISBLK(st.st_mode) else 0
            rdevminor = os.minor(st.st_rdev) if stat.S_ISCHR(st.st_mode) or stat.S_ISBLK(st.st_mode) else 0

            out.write(entry_bytes(name, ino, mode, mtime, data, rdevmajor, rdevminor))
            entries += 1
            data_bytes += len(data)
            ino += 1

        out.write(entry_bytes("TRAILER!!!", 0, 0, 0, b""))

    os.replace(tmp, output)
    return entries, data_bytes


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", help="staging directory")
    parser.add_argument("output", help="output .cpio file")
    parser.add_argument(
        "--mtime",
        type=int,
        default=None,
        help="mtime for every entry (default: SOURCE_DATE_EPOCH or 0)",
    )
    args = parser.parse_args(argv)

    mtime = args.mtime
    if mtime is None:
        mtime = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))

    try:
        entries, data_bytes = pack(args.root, args.output, mtime)
    except (OSError, ValueError) as exc:
        print(f"pack_cpio: {exc}", file=sys.stderr)
        return 1

    size = os.path.getsize(args.output)
    print(
        f"pack_cpio: {entries} entries, {data_bytes} bytes of file data, "
        f"{size} byte archive -> {os.path.abspath(args.output)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
