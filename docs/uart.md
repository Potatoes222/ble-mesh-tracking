# UART commands

All nodes use 115200 baud.

## Gateway

| Key  | Action                                        |
|------|-----------------------------------------------|
| `1`  | Node table.                                   |
| `2`  | Tag and zone view.                            |
| `3`  | MQTT and mesh stats.                          |
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

| Key  | Action                          |
|------|---------------------------------|
| `i`  | Change the legacy scanner id.   |

## Tag

| Key  | Action                                                       |
|------|--------------------------------------------------------------|
| `1`  | Status: current sequence, last HMAC, advertising state.      |
