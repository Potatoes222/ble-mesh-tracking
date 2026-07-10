# Changelog

## 2026-07-09

Compared to the old `main` branch (2026-06-28), which had a monolithic `main.c`, gateway-side zone computation, and manual scanner IDs.

### Architecture

1. The gateway is a pure relay. Zone logic moved to ThingsBoard. The rule chain uses hysteresis and leaky-bucket debounce. Stale RSSI falls back to the last shown value instead of `-999`.
2. Scanners identify by MAC instead of a manual ID. One firmware fits every scanner. UART `1` prints the MAC.
3. ThingsBoard CE is self-hosted with Docker and MQTTS. Self-signed CA is verified by CN `bmt-tb.local`, so IP changes do not require new certs.
4. Gateway code is split from a single `main.c` into `bmt_*` modules.

### Reliability

5. Enabled `CONFIG_BLE_MESH_SETTINGS=y` on the gateway. Fixes the "gateway power loss kills the whole mesh" bug: before, every boot generated a new random NetKey, so nodes with the old key could no longer talk to the gateway (`Failed to find Dst`). Now keys, node list, devkey, and sequence all persist.
6. Grew NVS from 24 KB to 64 KB. Added error logs for node-table save/load. Show a clear warning if NVS has to be erased (`NO_FREE_PAGES`).
7. Nodes rejoin on their own. If a provisioned UUID sends an unprovisioned beacon again, the gateway drops the old entry and re-provisions.
8. Fixed a race in node reset: `esp_ble_mesh_node_local_reset()` is async. The old code used a fixed delay before reboot, so sometimes NVS was not fully erased. Now the node waits for `PROV_RESET_EVT` before rebooting, with a 5-second fallback.
9. Gateway watchdog: 15-second stabilize wait after boot; no wipe if all five `RESET_CMD` sends fail; ACKs from the relay count as proof of life; only `error_code == 0` counts.
10. Fixed the mesh publish buffer (12 to 20 bytes). Before, pushing the 16-byte HMAC key silently failed with `Too small publication msg size`, breaking key rotation.
11. Only the relay is pinged. Pinging every node with a shared config client caused ONLINE/OFFLINE flapping.

### Security

12. NetKey and AppKey are random per network.
13. Static OOB authentication during provisioning. A rogue device without the OOB value cannot join even if it fakes the UUID.
14. HMAC-16 on tag beacons, with a 24-hour key rotation pushed over mesh. Scanners reject out-of-order sequence numbers to block replays.
15. MQTTS to ThingsBoard. WiFi and token secrets in the repo are placeholders (`YOUR_WIFI_*`, `YOUR_TB_GATEWAY_TOKEN`). The TLS certs in `tls/` are dev only. Run `tls/gen_certs.sh` for production.
