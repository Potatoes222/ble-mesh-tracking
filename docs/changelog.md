# Changelog

## 2026-07-09

Diff from the old `main` branch (2026-06-28). The old branch had one big `main.c`, computed the zone on the gateway, and set scanner IDs by hand.

### Architecture

1. The gateway only forwards data. Zone logic moved to ThingsBoard. The rule chain does hysteresis and leaky-bucket debounce. Stale RSSI falls back to the last shown value instead of `-999`.
2. Scanners use their MAC as their ID. One firmware fits every scanner. UART `1` prints the MAC.
3. ThingsBoard CE runs on Docker with MQTTS. The self-signed CA is verified by CN `bmt-tb.local`, so IP changes do not need new certs.
4. Gateway code moved from one `main.c` into `bmt_*` modules.

### Reliability

5. Turned on `CONFIG_BLE_MESH_SETTINGS=y` on the gateway. Fixes the "gateway power loss kills the whole mesh" bug. Before, each boot made a new random NetKey, so nodes with the old key could no longer reach the gateway (`Failed to find Dst`). Now keys, node list, devkey, and sequence all persist.
6. Grew NVS from 24 KB to 64 KB. Added error logs for node-table save and load. Warns clearly if NVS has to be erased (`NO_FREE_PAGES`).
7. Nodes rejoin on their own. If a provisioned UUID shows up as unprovisioned again, the gateway drops the old entry and re-provisions.
8. Fixed a race in node reset. `esp_ble_mesh_node_local_reset()` is async. The old code used a fixed delay before reboot, so sometimes NVS was not fully erased. Now the node waits for `PROV_RESET_EVT` before rebooting, with a 5-second fallback.
9. Gateway watchdog: waits 15 seconds after boot. Does not wipe if all five `RESET_CMD` sends fail. Relay ACKs also count as "mesh alive". Only `error_code == 0` counts.
10. Grew the mesh publish buffer from 12 to 20 bytes. Before, pushing the 16-byte HMAC key failed silently with `Too small publication msg size`, so key rotation was broken.
11. Only the relay is pinged. Pinging every node with a shared config client made ONLINE and OFFLINE flap.

### Security

12. NetKey and AppKey are random per network.
13. Static OOB authentication during provisioning. A rogue device without the OOB value cannot join even if it fakes the UUID.
14. HMAC-16 on tag beacons. The key rotates every 24 hours over mesh. Scanners drop out-of-order sequence numbers to block replays.
15. MQTTS to ThingsBoard. WiFi and token secrets in the repo are placeholders (`YOUR_WIFI_*`, `YOUR_TB_GATEWAY_TOKEN`). The TLS certs in `tls/` are dev only. Run `tls/gen_certs.sh` for production.
