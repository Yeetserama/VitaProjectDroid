# G0: openpvrsgx vs Vita SGX543MP4+ ABI Comparison

**Status**: Desk research — no hardware needed  
**Source**: `openpvrsgx-devgroup/linux_openpvrsgx` (drivers/gpu/drm/pvr/)  
**Vita target**: `SceGpuEs4` (PVRSRV bridge, kernel module)

---

## 1. openpvrsgx Driver Structure

| Platform | SoC | GPU | DDK Version | Status |
|----------|-----|-----|-------------|--------|
| OMAP5 (ti) | OMAP5432 | SGX544MP2 | 1.11 / 1.17 | Mainlined |
| Allwinner (sunxi) | A80 | SGX544MP1 | 1.11 | Mainlined |
| JZ4780 (ingenic) | JZ4780 | SGX540 | 1.11 | Mainlined |

**Kernel driver location**: `drivers/gpu/drm/pvr/`
- `pvr_bridged_*.c` — PVRSRV bridge (KM/UM communication)
- `pvr_device.c` — device probe, power, reset, clock
- `pvr_mmu.c` — SGX MMU programming (v4/v5 formats)
- `pvr_firmware.c` — firmware load (`.img` containers)
- `pvr_job.c` — command submission (TA/3D/CDM queues)
- `pvr_sync.c` — fence/sync primitives

---

## 2. PVRSRV Bridge ABI (KM ↔ UM)

Exported by `SceGpuEs4` (per Vita wiki):

| Function | openpvrsgx equivalent | Notes |
|----------|----------------------|-------|
| `PVRSRVEnumerateDevicesKM` | `pvr_bridged_enumerate_devices` | Device discovery |
| `PVRSRVAllocDeviceMemKM` | `pvr_bridged_alloc_device_mem` | Device memory alloc |
| `PVRSRVGetMiscInfoKM` | `pvr_bridged_get_misc_info` | Device caps/info |
| `PVRSRVBridgeDispatchKM` | `pvr_bridged_dispatch` | Main ioctl entry |
| `PVRSRVConnectKM` | `pvr_bridged_connect` | Process context |
| `PVRSRVDisconnectKM` | `pvr_bridged_disconnect` | Cleanup |

**Vita-specific**: Bridge uses same function names → **structurally compatible** with openpvrsgx.

---

## 3. SGX5 Register Map (from openpvrsgx + Vita3K)

| Block | Offset Range | Description |
|-------|-------------|-------------|
| **System** | `0x0000–0x0FFF` | ID, revision, status, clock gate, reset |
| **MMU** | `0x1000–0x1FFF` | Page table base, fault addr, fault status |
| **TA (Tiling Accelerator)** | `0x2000–0x2FFF` | TA queue control, status, pointers |
| **3D (Rendering)** | `0x3000–0x3FFF` | 3D queue control, status, pointers |
| **CDM (Compute)** | `0x4000–0x4FFF` | CDM queue (Series5XT+) |
| **Event/IRQ** | `0x5000–0x5FFF` | Interrupt status, mask, acknowledge |
| **Power** | `0x6000–0x6FFF` | Power domain control, status |
| **Firmware** | `0x7000–0x7FFF` | Firmware load addr, status, version |

**Vita wiki MMU heaps** (unverified):
- General: `0x7000000 → 0x40000000`
- TA: `0x7000001 → 0x50000000`

---

## 4. MMU Format (SGX5 v4/v5)

From `pvr_mmu.c`:
- **v4** (SGX530/535/540): 2-level, 4KB pages, 1MB superpages
- **v5** (SGX543/544): 3-level, 4KB/64KB/16MB pages, **ASID support**

Vita SGX543MP4+ → **v5 MMU with 4 cores** (each core has TA+3D, shared CDM?).

---

## 5. Firmware Container (`.img`)

openpvrsgx expects:
```
Header:  "PVR" magic + version + build info
Segments: [code, data, bss] with load addresses
Meta:     PDA (per-device data), SCC (shader compiler config)
```

**Vita difference**: Firmware inside `SceGpuEs4` is **encrypted/signed** (Sony container). May not be loadable by openpvrsgx `pvr_firmware.c`.

---

## 6. Vita-Specific Unknowns (G1 Targets)

| Unknown | Where to find in `SceGpuEs4` |
|---------|------------------------------|
| GPU physical base address | Init sequence, `ioremap` calls |
| IRQ number | `request_irq` / interrupt handler |
| Clock ID/name | `clk_get` / `clk_prepare_enable` |
| Reset bit (syscon) | `vita_reset_probe` / syscon register writes |
| Power domain sequence | PM callbacks, regulator calls |
| Firmware load protocol | `PVRSRVLoadFirmwareKM` equivalent |
| SGX core revision | ID register read at probe |

---

## 7. Compatibility Assessment

| Component | Compatible? | Risk |
|-----------|-------------|------|
| PVRSRV bridge ABI | **Yes** — same exports | Low |
| SGX5 register layout | **Likely** — same architecture | Medium (Vita custom SoC) |
| MMU v5 format | **Yes** — SGX543MP4+ uses v5 | Low |
| Firmware loading | **Unknown** — Sony encryption | **HIGH** (G3 gate) |
| Userspace UMD | **No** — Sony `libgxm` only | Requires new GLSL compiler |
| Android glue | **No** — needs EGL/gralloc/HWC | Separate project |

---

## 8. Next Steps (G0 → G1)

1. **Dump `gpu_es4.skprx` + `gpuinit_es4.skprx` from console** (FAGDec)
2. **Ghidra analysis** — locate:
   - `PVRSRVEnumerateDevicesKM` → device struct → base/IRQ/clock/reset
   - Firmware load function → container format
   - MMU init → heap descriptors
   - Power sequence → syscon register bits
3. **Cross-reference** with openpvrsgx `pvr_device.c` probe sequence
4. **Document** in `re/notes/gpu-hardware-config.md`

---

## 9. Reference Links

- openpvrsgx: https://github.com/openpvrsgx-devgroup/linux_openpvrsgx
- Vita wiki SceGpuEs4: https://wiki.henkaku.xyz/vita/SceGpuEs4
- Vita wiki GPU: https://wiki.henkaku.xyz/vita/GPU
- Vita3K SGX ISA: https://github.com/Vita3K/Vita3K/tree/master/src/vulkan/gpu
- PVRSRV bridge docs (OMAP): `drivers/gpu/drm/pvr/pvr_bridge.h`