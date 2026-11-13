# Audio A2 - I2S/DMA probe plan

Status: register bases confirmed from shoui520/psp2; probe not yet implemented.

## Confirmed register bases

| Block | Base | Size | Channels |
| --- | --- | --- | --- |
| I2S0 | `0xE0420000` | 0x1000 | — |
| I2S1 | `0xE0430000` | 0x1000 | — |
| I2S2 | `0xE0440000` | 0x1000 | — |
| I2S3 | `0xE0450000` | 0x1000 | — |
| I2S4 | `0xE0470000` | 0x1000 | — |
| I2S5 | `0xE0460000` | 0x1000 | — |
| I2S7 | `0xE0490000` | 0x1000 | — |
| SRCMIX0-2 | `0xE04A0000` + n*0x10000 | 0x1000 each | — |
| SPDIF | `0xE04D0000` | 0x4000 | — |
| ACLKGEN | `0xE04DC000` | 0x1000 | — |
| DMAC4 | `0xE0400000` | 0x1000 | 16 |
| DMAC5 | `0xE0410000` | 0x1000 | 2 |

## Pervasive clock/reset for I2S

From shoui520/psp2 `pervasive_regs.h`:

| Page | Base | I2S field | Offset |
| --- | --- | --- | --- |
| Reset | `0xE3101000` | `i2s[port]` | `0xC0 + port*4` |
| Gate | `0xE3102000` | `i2s[port]` | `0xC0 + port*4` |

So I2S0 reset is at `0xE31010C0`, gate at `0xE31020C0`.

## I2S register layout (from shoui520/psp2 `audio_regs.h` / `i2s.c`)

| Offset | Register | Notes |
| --- | --- | --- |
| 0x04 | control | Init: 0x100 then 1 |
| 0x10 | formatControl | Init: 0x2810 |
| 0x14 | interruptStatus | Read-only |
| 0x18 | clockControl | Init: 1 |
| 0x20 | transferMode | Init: 2 |
| 0x24 | transferEnable | Init: 1 |
| 0x100 | data | DMA data port |

Init sequence (from `audioEnableI2s`):
```
formatControl = 0x2810
interruptControl = 0
control = 0x100
clockControl = 1
transferEnable = 1
transferMode = 2
control = 1
```

## I2S interrupt mapping

| Port | IRQ | Clock index | Clock port |
| --- | --- | --- | --- |
| 0 | 193 | 0 | 3 |
| 1 | 194 | 1 | 3 |
| 2 | 196 | 2 | 5 |
| 3 | 0 | 3 | 3 |
| 4 | 0 | 5 | 5 |
| 5 | 0 | 4 | 5 |
| 6 | 0 | 255 | 0 |
| 7 | 0 | 7 | 3 |

## Probe plan (A2)

1. Map `0xE31010C0 + port*4` (reset I2S port) and `0xE31020C0 + port*4` (gate I2S port)
2. Deassert I2S reset: clear bit in reset page
3. Enable I2S clock: set bit in gate page
4. Map `0xE0420000` size `0x1000` (I2S0)
5. Read all registers, compare against init values above
6. Print to dmesg

No writes beyond reset/clock enable. This is read-only verification that the
registers match the shoui520/psp2 init values.

## DMAC register layout (from shoui520/psp2 `dmac_regs.h`)

Channel regs (0x80 bytes each, starting at base):

| Offset | Register | Notes |
| --- | --- | --- |
| 0x00 | src | source address |
| 0x04 | dst | destination address |
| 0x08 | len | transfer length |
| 0x0C | cmd | command |
| 0x1C | ctrl | 0 stops the channel |
| 0x24 | stat | status |
| 0x28 | intrStatus | written back to clear |

DMAC4: 16 channels at `0xE0400000`, interrupt gates at `0x900` (IRQs 0x78-0x7b).
DMAC0-3, DMAC5-6: 2 channels each.

Pervasive for DMAC: `dmac[n]` at `0xE3101050 + n*4` (reset) / `0xE3102050 + n*4` (gate).

## Deliverable

`re/notes/audio-registers.md` with base, register map, DMA descriptor layout,
IRQ and init sequence.

Gate: a register map that explains playback start, periodic DMA and stop.

## What is NOT needed

`audio.skprx` is NOT required for A2 — all register bases and layouts come from
shoui520/psp2 clean-room reimplementation. The earlier scan-mmio.py results
(returning no candidates) are superseded by this confirmed data.
