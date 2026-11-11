# Audio A1 - WM1803E codec bring-up

Status: kernel changes on `re/audio`; CI compile check in progress. Hardware
test pending (the console is off/unreachable).

## Goal

Bind the WM1803E (WM8962-compatible) codec on I2C0 address `0x1A` and read
chip ID `0x6243` from Linux. This proves the codec and clock path from Linux;
it does not play audio yet.

## Hardware facts

| Item | Value |
| --- | --- |
| Codec | WM1803E, WM8962-compatible |
| I2C bus / address | i2c0, `0x1A` (7-bit) |
| Chip ID register | `0x00` (software reset), expected `0x6243` |
| AudioClk source | P1P40167 clockgen, i2c0 `0x69`, register 1 |
| AudioClk enable | register 1, bit 4 |
| Rate select | register 1, bit 0: 1 = 48 kHz family, 24.576 MHz |
| MCLK for A1 | 24.576 MHz |

## Why a clock provider

`wm8962_i2c_probe()` reads the ID before any sound card exists, and the codec
does not answer until AudioClk is running. If the syscon driver only enabled
the clock bit at probe time, the I2C child could be probed first. The syscon
driver now registers a real clock (`vita-audio-clk`); the codec node
references it, so the I2C core defers the codec until the provider exists.
By then AudioClk is enabled.

The clock's `.prepare`/`.unprepare` ops own the I2C access, because clock
`.enable`/`.disable` callbacks must not sleep.

## Changes

| File | Change |
| --- | --- |
| `kernel/patches/0001-mfd-vita-syscon-add-audio-clock-provider.patch` | AudioClk helper, `vita-audio-clk` provider, enable at probe when `vita,audio-clk-always-on` |
| `kernel/patches/0002-ARM-dts-vita-add-WM8962-codec.patch` | clock cells on syscon; `wlf,wm8962` node at `0x1A` on i2c0 |
| `kernel/config/vita-android.fragment` | `CONFIG_SOUND`, `CONFIG_SND`, `CONFIG_SND_SOC`, `CONFIG_SND_SOC_WM8962` |
| `scripts/apply-kernel.sh` | applies `kernel/patches/*.patch` idempotently before configuring |

## Test

After booting the `re/audio` kernel:

```sh
adb shell dmesg | grep -i -E 'wm8962|audio clock|clockgen'
```

Expected:

- `vita-syscon ... clockgen I2C adapter: ...`
- `wm8962 0-001a: customer id 0 revision ...`
- no `Device is not a WM8962, ID ... != 0x6243`

The codec supplies are hardwired on the Vita. The driver logs
`supply ... not found, using dummy regulator` once per rail; on a DT system
the regulator core permits this. It is expected, not a failure.

Optional chip-ID read once i2c-tools exist:

```sh
i2cget -y 0 0x1a 0x00 w
```

## Failure modes

- No `wm8962` line: check that i2c0 is enabled and that the clock provider
  registered (`/sys/kernel/debug/clk/vita-audio-clk`).
- Deferred forever: the syscon probe never registered the provider; check the
  clockgen I2C adapter lookup in dmesg.
- `ID ... != 0x6243`: AudioClk is not running, or the codec is held in reset.
- I2C timeouts: stop. Do not poke unknown reset registers.

## Next

A2: locate the SoC I2S/DMA controller from `audio.skprx`; see
[`../../docs/reverse-engineering.md`](../../docs/reverse-engineering.md).
