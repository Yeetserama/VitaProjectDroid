# Bluetooth on the Vita: SD8787 function 2

Status: research note, 2026-11-13. No working Bluetooth yet.
Hardware: PCH-1000, SD8787 combo card, LinuxOnVita 6.12 (`a33b819`).
Related: [drivers-and-firmware.md](drivers-and-firmware.md),
[reverse-engineering.md](reverse-engineering.md).

## 1. What the hardware exposes

The Vita's Wi-Fi/Bluetooth chip is a Marvell SD8787 behind SDIO. The card
presents the usual Marvell function layout:

| SDIO function | Device ID | Role | Linux driver |
| --- | --- | --- | --- |
| 1 | `0x9119` (`SDIO_DEVICE_ID_MARVELL_8787_WLAN`) | WLAN | `mwifiex_sdio` |
| 2 | `0x911a` (`..._8787_BT`), `0x911b` (`..._8787_BT_AMP`) | Bluetooth HCI | `btmrvl_sdio` |

There is no separate Bluetooth power switch in the device tree. Both functions
share the same power rail, reset and reference clock. The
`pwrseq_vita_wlan` driver in `drivers/mmc/core/pwrseq_vita_wlan.c` owns them:

1. enable the 27 MHz WlanBt clock in the P1P40167 clockgen (I2C bus 0,
   address `0x69`, register 1, bit 3);
2. power the wireless block through Ernie syscon command `0x88A` (data 1);
3. de-assert the WLANBT reset through Ernie command `0x88F`
   (data `1 | (0x10 << 8)`: mode 1 = de-assert, mask `0x10` = WLANBT).

Power-off is the reverse: assert reset, power off, disable the clock. This is
why a failed Bluetooth probe can take Wi-Fi down: both functions live on the
same powered block, and a card reset or power cycle is shared.

The pwrseq code proves BT is powered and out of reset whenever WLAN is up. The
missing piece is firmware state, not a GPIO.

## 2. What the Linux driver does

`btmrvl_sdio` is built around the SD8787 "combo firmware" model. Both
`mwifiex_sdio` and `btmrvl_sdio` request the same image:

```
mrvl/sd8787_uapsta.bin
```

Only one function is expected to download it; this is the "winner" handshake.
On this repository the blob is the 466,592-byte `linux-firmware` file embedded
in the initramfs at `/lib/firmware/mrvl/sd8787_uapsta.bin`.

The 8787 register window used by `btmrvl_sdio` is:

| Register | Offset | Notes |
| --- | --- | --- |
| `cfg` | `0x00` | host power-up / CMD53 finish bits |
| `host_int_mask` | `0x02` | `0xff` disables, `BIT(0)|BIT(1)` enables |
| `host_intstatus` | `0x03` | upload / download interrupt bits |
| `card_status` | `0x30` | `DN_LD_CARD_RDY = BIT(0)`, `CARD_IO_READY = BIT(3)` |
| `sq_read_base_addr` | `0x40`/`0x41` | firmware download length |
| `card_revision` | `0x5c` | |
| `card_fw_status0/1` | `0x60`/`0x61` | little-endian status; `0xfedc` = ready |
| `card_rx_len/unit` | `0x62`/`0x63` | |
| `io_port_0/1/2` | `0x78`-`0x7a` | CMD53 data port |

`btmrvl_sdio_download_fw()` in v6.12 runs this sequence:

1. read `0x60`/`0x61` once; if it is already `0xfedc`, assume the other
   function downloaded the firmware and continue;
2. otherwise read `0x60` once more:
   - `0`: this function is the winner, download the firmware
     (`btmrvl_sdio_download_fw_w_helper`);
   - nonzero: "BT not the winner", skip the download and wait for the WLAN
     side to finish;
3. poll `0x60`/`0x61` for `0xfedc`; the non-winner path multiplies the retry
   count by 10, so it waits 1000 x 100 ms (~100 seconds).

If the final poll times out, probe returns `-ENODEV`. `sdio_disable_func()` is
called on function 2, and on this hardware Wi-Fi has been observed to drop
shortly afterwards. That is the current, deliberate reason
`CONFIG_BT_MRVL` and `CONFIG_BT_MRVL_SDIO` are off in
`kernel/config/vita-android.fragment`.

## 3. The failure signature

On the Vita, with `mwifiex` bound first, the Bluetooth probe reports the
non-winner path and then times out after roughly 100 seconds. The register at
`0x60` is nonzero, but it never becomes `0xfedc`. The exact value has not been
captured yet; that is the first thing the diagnostic build must record.

This rules out the simple cases:

- The SDIO function is enumerated; the ID matches `btmrvl_sdio`, otherwise the
  driver would never probe.
- Power, reset and the 27 MHz clock are already on; the WLAN function is
  working on the same block.
- The firmware file is present and is the one the driver asks for.

It does not yet distinguish "firmware downloaded but BT core not started" from
"the status register means something else on this card".

## 4. Hypotheses, in order

**H1 - the combo image does not bring up the BT core.** `mwifiex` downloads
the image through function 1. The SD8787 starts WLAN but leaves function 2 in
a state that never reports `0xfedc`. Possible causes: the Vita uses a Sony
firmware variant, the `uapsta` image is WLAN-only for this silicon revision, or
BT needs an additional start command.

**H2 - BT must be the winner.** If function 2 downloads the firmware first,
both functions may come up correctly. The current boot order always lets
`mwifiex` win because function 1 probes first. A BT-first boot is the cheapest
experiment that tests this.

**H3 - a missing host action before the ready status.** The driver waits for
`0xfedc` before it enables host interrupts or sends any HCI command. If this
firmware expects `HOST_POWER_UP` (`cfg` bit 1) or an interrupt mask write
first, the wait is simply too early. The 100-second timeout would then be a
sequencing bug, not a firmware mismatch.

**H4 - different register meaning.** The Vita's firmware may not implement the
generic 8787 status registers at `0x60`. In that case the nonzero value is
unrelated and polling can never succeed.

## 5. Diagnostic plan

All of this belongs on a `re/bt` branch. Do not merge it into a boot kit. The
first build must be read-only: no firmware download, no register writes beyond
the normal SDIO function enable, and no 100-second poll.

### D1 - capture the function-2 register window

Add a probe quirk (module parameter or kernel command line, e.g.
`btmrvl.probe_only=1`) that:

- enables function 2 and reads `0x00`-`0x7f`;
- prints the values with `dev_info` to dmesg/boot.log;
- reads `0x60`/`0x61` a few times over 2 seconds to see whether it changes;
- returns success immediately, leaving function 2 enabled and skipping the
  firmware wait. Returning an error or calling `sdio_disable_func()` is the
  suspected trigger for the shared card dropping Wi-Fi.

Boot with WLAN working and capture `ur0:/android/boot.log`. This gives the
actual status value and the card revision with no risk to Wi-Fi.

Implementation on the `re/bt` branch:

- `scripts/patch-btmrvl-diag.py` inserts the probe-only path into
  `drivers/bluetooth/btmrvl_sdio.c`. It enables function 2, dumps registers
  `0x00`-`0x7f` and function-0 registers `0x00`-`0x09`, polls `0x60`/`0x61`
  twenty times at 100 ms intervals while releasing the host around each read,
  then leaves function 2 enabled and returns success. Keeping the probe
  successful avoids the error path that disables the shared function.
- `kernel/config/vita-bt-diag.fragment` re-enables `CONFIG_BT`,
  `CONFIG_BT_BREDR`, `CONFIG_BT_MRVL` and `CONFIG_BT_MRVL_SDIO` after the
  Android fragment turns them off.
- `BT_DIAG=1 scripts/apply-kernel.sh ...` applies the patch and merges the
  fragment. The probe defaults to on; `btmrvl_sdio.probe_only=0` restores the
  normal probe.
- The `re/bt` GitHub Actions job uploads `android-5.1.1-bt-diag`. It contains
  only the 5.1 kernel, DTBs and initramfs. Keep the existing verified 5.1.1
  images on ur0 and replace `ux0:/linux/zImage` with the artifact's `zImage`.
  Building on 5.1.1 keeps the only new variable the Bluetooth probe.

After boot, `dmesg` (or `ur0:/android/boot.log`) should contain `BT-DIAG:`
register rows and the 2-second status poll. No firmware download or 100-second
wait is attempted, so Wi-Fi must stay up.

### D2 - compare power states

Repeat D1 after:

- a normal WLAN boot;
- `echo 0 > $(find /sys -name wlan_power | head -n1)` followed by `echo 1`
  (the sysfs attribute exposed by `vita-syscon`);
- a cold boot where `mwifiex` never associates.

If `0x60` is constant regardless of WLAN state, H4 becomes likely. If it
changes when WLAN starts, the handshake is real but stuck.

### D3 - BT-first boot

Make function 1 probe later than function 2. The SDIO core probes functions
in card order, so the cheap way is to patch `mwifiex_sdio_probe()` to return
`-EPROBE_DEFER` while `vita.bt_first=1` is set and the first 15 seconds have
not elapsed. Function 2 then probes against a powered card with no WLAN
firmware loaded, `0x60` should read 0, and `btmrvl_sdio` should download
`sd8787_uapsta.bin` itself.

Outcomes:

- **`0xfedc` and `hci0` appear:** the firmware and BT core are usable; the
  problem is the winner handshake. Then test whether WLAN can attach after BT,
  or whether one function must be chosen at boot.
- **still timeout:** either the image does not match the Vita's BT core (H1)
  or the driver sequence is wrong (H3). Try the `HOST_POWER_UP` write and the
  interrupt-mask ordering before concluding H1.

### D4 - if BT-first works but WLAN does not

Try a controlled sequence in one driver or a small coordinator:

1. power the block with the existing pwrseq;
2. let `btmrvl_sdio` download the firmware and reach `0xfedc`;
3. let `mwifiex` bind function 1 again (unbind/rebind or a card rescan, but a
   full rescan also re-enumerates function 2, so prefer a targeted reprobe);
4. check `mlan0` and `hci0` together.

If the combo image can only run one function, Bluetooth and Wi-Fi become a
boot-time choice. That is a product decision, not a kernel bug, and should be
documented before writing an Android HAL.

### D5 - Android side, only after `hci0` exists

Android 5.1/6.0 need more than a kernel HCI device:

- the Bluedroid stack (`bluetooth.default.so`, `libbluetooth_jni.so`,
  `net_bt_*` users and permissions) must be present in the SDK image;
- `init.vita.rc` must set `ro.bluetooth.*` properties and grant the
  `bluetooth` user access to `/dev/hci0` (or the rfkill node);
- a `bt_vendor` implementation or the standard H4 UART path must match how
  `hci0` is exposed.

None of that is worth doing until D3 produces a working `hci0`. The Android
image's Bluetooth packages still need to be verified; the emulator SDK image
may omit the hardware HAL even though it ships the framework classes.

## 6. Sources

- `drivers/bluetooth/btmrvl_sdio.c` and `btmrvl_sdio.h`, Linux v6.12.
- `drivers/mmc/core/pwrseq_vita_wlan.c`, LinuxOnVita `a33b819`.
- `drivers/mfd/vita-syscon.c`, LinuxOnVita `a33b819`
  (`vita_syscon_wlan_power_on/off`, Ernie commands `0x88A`/`0x88F`).
- `include/linux/mmc/sdio_ids.h`, Linux v6.12 (8787 IDs and status bits).
- `linux-firmware` blob `mrvl/sd8787_uapsta.bin`, vendored under
  `vendor/vita-linux/lib/firmware/`.
