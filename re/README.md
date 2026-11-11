# Reverse-engineering workspace

This directory holds the Vita GPU/audio research. The plan is in
[`../docs/reverse-engineering.md`](../docs/reverse-engineering.md).

## Layout

| Path | Tracked | Contents |
| --- | --- | --- |
| `notes/` | yes | Original findings, register maps, manifests |
| `scripts/` | yes | Parsers and helpers written for this project |
| `dumps/` | no | Decrypted modules dumped from the user's own console |
| `ghidra/` | no | Ghidra project databases |
| `tmp/` | no | Scratch output |

## Rules

- Only dump firmware from a console you own. Do not download or share
  someone else's firmware, keys or decrypted modules.
- Never commit anything under `dumps/`, `ghidra/` or `tmp/`. The `.gitignore`
  enforces this; check `git status` before every commit anyway.
- Record hashes and module paths in `notes/manifest.md` rather than committing
  binaries.
- Notes may contain addresses, register values and short pseudocode. Do not
  paste long disassembly listings or Sony code.
- Hardware experiments belong on `re/*` branches, not on the boot-kit branch.

## Getting started

1. Read the plan.
2. Put dumps in `dumps/<firmware>/<module>/`.
3. Fill in `notes/manifest.md`.
4. Open the ELF in Ghidra and import the NID names from the Vita wiki.
