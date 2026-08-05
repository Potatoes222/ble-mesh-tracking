# nRF52840 Beacon (coin-cell tag variant)

The [`apps/Beacon_ProMicroNrf52840`](../apps/Beacon_ProMicroNrf52840) and [`apps/Beacon_XiaoNrf52840`](../apps/Beacon_XiaoNrf52840) folders hold a **second implementation of the tag firmware**, targeting Nordic nRF52840 boards instead of ESP32-S3. Same on-the-wire beacon format, same HMAC epoch-key scheme — the ESP32 scanners cannot tell them apart. Different silicon, different build system.

## Why this exists

`apps/tag` on ESP32-S3 works fine on USB or Li-Ion but is not battery-friendly enough for a wearable coin-cell tag: even at BLE-only workload the S3 draws several mA average. nRF52840 was designed for low-power BLE — a properly stripped-down beacon draws under 1 mA average, so a CR2032 / LIR2032 coin cell lasts weeks to months instead of days.

Pick this variant when:

- You need a wearable, coin-cell tag (hospital wristband, key fob, asset sticker).
- Battery life matters more than reflash-over-USB convenience.

Stick with `apps/tag` on ESP32-S3 when:

- You are prototyping the system for the first time and already own ESP32-S3 boards.
- The tag has a USB/Li-Ion power source anyway.

Both variants can coexist in the same deployment.

## Two boards, same firmware core

| Folder | Board | Battery path | Charge status |
|---|---|---|---|
| `apps/Beacon_ProMicroNrf52840` | ProMicro nRF52840 (nice!nano v2 compatible) | Battery straight into VDDH, read via internal `VDDHDIV5` | Not exposed on GPIO |
| `apps/Beacon_XiaoNrf52840` | Seeed XIAO nRF52840 | External 1M / 510k divider on `P0.31`, gated by `P0.14` | `P0.17` (LOW when charging) |

The beacon and auth code is identical byte-for-byte between the two — the only real differences are the battery-read routine and the Devicetree overlay. Both READMEs cross-link to each other.

## Build and flash

Build system is **Zephyr / west**, not ESP-IDF. You need a Zephyr install (`west`, `nRF Connect SDK`, toolchain) — see the [Zephyr getting-started guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).

```powershell
# ProMicro
cd apps/Beacon_ProMicroNrf52840
west build -b promicro_nrf52840/nrf52840/uf2 -d build . --pristine

# XIAO
cd apps/Beacon_XiaoNrf52840
west build -b xiao_ble/nrf52840 -d build . --pristine
```

The bootloader is **MCUboot with ECDSA-P256 signature verification**. **Do not flash the unsigned `zephyr.uf2` directly** — MCUboot silently rejects it and the board goes dark. Read the flashing sections of the two per-board READMEs before you copy any file:

- [`apps/Beacon_ProMicroNrf52840/README.md`](../apps/Beacon_ProMicroNrf52840/README.md)
- [`apps/Beacon_XiaoNrf52840/README.md`](../apps/Beacon_XiaoNrf52840/README.md)

They cover the full flow: sign the app, produce the merged UF2, drag-and-drop, `debug.conf` for logs, and the low-power tradeoffs.

## Interoperability with the ESP32 scanners

Wire-level compatibility is 100% intentional. Both nRF52840 and ESP32 tag variants share:

- CID `0x02E5` (Espressif) in the manufacturer data — the scanner filters by this and does not care which chip produced the beacon.
- The 24-byte payload struct (`uuid + major + minor + tx_power + sequence + mac16`) laid out identically.
- HMAC computation and epoch-key derivation (`bmt_auth.c`) — the master key must be the **same byte-for-byte** on Tag and Scanner regardless of which tag variant is running.

You can rebuild any scanner without touching it — it will start recognising nRF52840 tags the moment one comes in range, no config change required.

## Status

Marked **experimental** in [13-secure-boot.md](13-secure-boot.md) because these apps do not go through the ESP-IDF Secure Boot V2 pipeline (they use MCUboot's own signing chain). The signing key material (`mcuboot_keys/`) is separate from the ESP fleet key (`secure_boot_keys/`). If you plan to deploy nRF52840 tags in production, rotate both key sets and keep the private parts out of the repo.

## Not covered here

CI, Docker packaging, OTA-over-mesh for the nRF52840 tag — none are wired up. The tag OTA path in `apps/tag` (ESP-IDF) is not shared with this variant.
