# SceGpuEs4 GPU interrupt

Module: `gpu_es4.skprx` (SceGpuEs4), SHA-256
`d11047f57598463c14df1e508bd92ff23551b6ac781f94e92d06f19259830732`.

The ELF is linked at `0x81000000` with a 0xa0-byte file header, so
`VA = 0x81000000 + (file_offset - 0xa0)`.

## Result

| Item | Value |
| --- | --- |
| VitaOS interrupt code | `0x34` (52) |
| GIC hardware IRQ | 52 |
| GIC SPI | `0x34 - 0x20` = **20** |
| Device tree | `interrupts = <GIC_SPI 20 IRQ_TYPE_LEVEL_HIGH>` |

## Derivation

The module info is at VA `0x8100db80`; the import library table starts at
`0x8100dc3c` (0x24-byte entries). `SceIntrmgrForDriver` is the entry at
`0x8100dca8`:

- NID table `0x8100e280`, in order: `sceKernelMaskIntr` `0x180435EC`,
  `sceKernelRegisterIntrHandler` `0x5C1FEB29`,
  `sceKernelUnmaskIntr` `0x7117E827`,
  `sceKernelReleaseIntrHandler` `0xD6009B98`.
- Stub table `0x8100e430`, so `sceKernelRegisterIntrHandler` is the ARM
  import stub at `0x8100d858`.

The GPU device context is the BSS object at `0x810151a4`. Its init function
at `0x8100d1c4` writes the interrupt code at `0x8100d218`:

```
movs r3, #0x34
str  r3, [r4, #0x24]      ; r4 = 0x810151a4, ctx->intr_code = 0x34
```

The same `+0x24` field is read back for `sceKernelMaskIntr` at `0x8100d24a`.
The IRQ setup function `0x8100727c(dev, intr_code)` is called from
`0x8100d29c` with `r1 = ctx->intr_code` (`0x8100d2bc`) and registers the
handler at `0x810072cc`:

```
mov  r0, r6               ; intr_code = 0x34
ldr  r1, = "SceGpuLisr"
ldr  r3, = 0x8100715b     ; handler
blx  0x8100d858           ; sceKernelRegisterIntrHandler
```

It also creates the event flag `SceGpuIsr` at `0x810072ea`.

## Mapping

The `vita.dtsi` convention (verified against the I2C and SDIF nodes) is
`VitaOS intr code = GIC hardware IRQ ID` and `GIC_SPI = code - 32`.
For the GPU: `0x34` -> hardware IRQ 52 -> **SPI 20**.

## Device tree

`scripts/apply-kernel.sh` adds the following to `/soc/gpu` for Android
4.2/4.2.2 (both `vita.dtb` and `vita-selinux-disabled.dtb`):

```
interrupts = <0 20 4>;   /* GIC_SPI 20, level high */
```
