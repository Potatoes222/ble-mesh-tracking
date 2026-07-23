# UART commands

All nodes use 115200 baud.

## Gateway

| Key  | Action                                        |
|------|-----------------------------------------------|
| `1`  | Node table.                                   |
| `2`  | Tag and zone view.                            |
| `3`  | MQTT and mesh stats.                          |
| `4`  | Show status / help menu (reprints command list). |
| `s`  | Scan for unprovisioned nodes (MANUAL mode).   |
| `p`  | Provision the scanned list.                   |
| `a`  | Switch to AUTO mode.                          |
| `m`  | Switch to MANUAL mode.                        |
| `u`  | Start OTA for scanners and relays.            |
| `g`  | Start OTA for the gateway itself.             |
| `0`  | Soft reset.                                   |
| `9`  | Full reset (wipe and re-provision).           |

## Scanner and Relay

| Key  | Action                          |
|------|---------------------------------|
| `1`  | Status with MAC.                |
| `r`  | Reset mesh to unprovisioned.    |

## Scanner extras

| Key  | Action                                         |
|------|------------------------------------------------|
| `i`  | Change the legacy scanner id.                  |
| `o`  | Manually trigger WiFi OTA (self-update, bypasses the mesh beacon path — useful when the gateway is not available). |

## Tag

| Key  | Action                                                       |
|------|--------------------------------------------------------------|
| `1`  | Status: current sequence, last HMAC, advertising state.      |

## Factory reset (BOOT button)

Physical recovery on **gateway, relay, and scanner** (not the tag — it has no such component). It matters more now that flash is encrypted: you can no longer pull NVS off the chip externally, so this button is the sanctioned way to wipe a node in the field. Implemented in the `bmt_factory_reset` component (`bmt_factory_reset.c`).

### How to trigger

While the app is running normally (not at power-on), press and **hold the BOOT button (GPIO0) continuously for 10 seconds**. The board polls every 100 ms and the hold must be unbroken — releasing the button resets the counter to zero immediately, so a brief release means you start over.

Do not confuse BOOT with the EN/RST button. Holding EN/RST just keeps the chip in reset with no code running; nothing is counted.

### What you see on serial (115200)

At boot, once per run:

```
Factory reset watcher started (giu nut BOOT 10s de kich hoat)
```

The moment you press BOOT, then a countdown every second:

```
[FACTORY RESET] Dang giu nut BOOT...
[FACTORY RESET] Con 9s se xoa toan bo NVS...
[FACTORY RESET] Con 8s se xoa toan bo NVS...
...
[FACTORY RESET] Con 1s se xoa toan bo NVS...
```

At 10 s it erases and reboots:

```
==================================================
[FACTORY RESET] Giu du 10s -> xoa toan bo NVS...
==================================================
[FACTORY RESET] Xong, dang reboot...
```

If the erase itself fails you get `nvs_flash_erase that bai: <error>` instead — rare, usually means flash trouble, not a config issue.

### What it erases and the consequences

It runs `nvs_flash_erase()` — wipes the **entire NVS partition**, nothing else. Firmware is untouched (the signed, encrypted app stays exactly as flashed). Gone after a reset:

- **Gateway**: the whole provisioned node table plus the network's NetKey/AppKey and the HMAC OTA-beacon key. The gateway comes back up as a fresh provisioner and must re-provision every node from scratch — plan for a full bring-up, not a quick reboot.
- **Relay / scanner**: its mesh membership (it becomes unprovisioned again). Its NetKey/AppKey and stored keys are gone.

### Next steps after a reset

- Reset a **relay or scanner**: on reboot it sends an unprovisioned beacon; a running gateway in AUTO mode re-provisions it on its own (see [07-operation.md](07-operation.md#self-healing)). Provision one node at a time.
- Reset the **gateway**: bring the mesh back up in order — gateway first, then relay, then scanners one at a time, tag last — exactly like a fresh flash ([00-quickstart.md](00-quickstart.md#6-run)).

This is distinct from the UART resets above: `9` (gateway) and `r` (scanner/relay) do a software-triggered mesh reset while connected to a console; the BOOT-button path needs no console and is the fallback when a node is deployed with no serial cable attached.
