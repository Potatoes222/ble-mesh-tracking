# Changelog

## 2026-07-31

Opensource release preparation. Scope: docs, comments, licensing, community files. Firmware behavior unchanged.

### Licensing and community

22. Apache License 2.0. Added `LICENSE` (full text, copyright "Cao Trọng Phước, Tăng Sĩ Thông"), `NOTICE` (third-party attributions for ESP-IDF, mbedTLS, ThingsBoard, nginx, FreeRTOS), and README badge + License section. Any redistribution now has explicit terms and a patent grant.
23. Added `CONTRIBUTING.md` (build setup, code style, `[TAG] subject` commit convention, PR flow, layout guide) and `.github/` (`workflows/build.yml` matrix-builds all four ESP-IDF apps on push/PR + issue and PR templates).
24. Added `tools/README.md` explaining `format.sh` and `build-all.sh`.

### Docs

25. New `docs/14-nrf52840-beacon.md` explains the previously undocumented `apps/Beacon_{ProMicro,Xiao}Nrf52840` variants (coin-cell tag on Zephyr/west) and links to the per-board READMEs.
26. `docs/01-architecture.md` gained a **Physical layout** section (scanner placement rules, when to add a relay) and a **Where the layers live in the repo** table. The duplicated ASCII data flow is removed in favor of the Mermaid diagram in the root README.
27. Root README data flow rewritten as a Mermaid `sequenceDiagram` (accurate to source: 1 h epoch key, 8 dBm hysteresis, `v1/gateway/telemetry`, optional Relay hop as `alt` block, parallel attribute/timeseries save as `par` block).
28. `docs/00-quickstart.md` — fixed hardware list (was wrong: 1x S3 + 5x ESP32; actual: 3x S3 + 3x ESP32) and added explicit "Where OTA `.bin` files come from" section.
29. `docs/03-algorithms.md` Kalman description now matches the code: `q` is fixed, `r` is adaptive (EMA α=0.1, clamped `[1.0, 20.0]`).
30. `docs/04-thingsboard-setup.md` and `docs/05-thingsboard-mqtt.md` clarified that firmware verifies **CN**, not SAN; the server cert has both set to `bmt-tb.local`.
31. `thingsboard/import.md` — fixed wrong OTA URL (http://:8081 → https://:8443) to match `docker-compose.yml`. Also linked from README next to the English quickstart.

### Cleanup

32. Removed all thesis materials (`thesis/`, `docs/13-thesis-outline.md`, thesis-specific tools) — this repo is code, not academic artefact.
33. Removed `docs/legacy/` (~2700 lines of pre-refactor monolithic `gateway_main.c` + `scanner_main.c`) — git history has them if needed.
34. Removed 75+ lines of historical `[FIX]/[ADD]/[REVERTED]/[TEST]` tagged comments from active apps. Rule now enforced in `CONTRIBUTING.md`: comments explain why, not history.
35. Removed accidentally committed `ble-mesh-tracking.zip` (3.4 MB nested self-copy with stale IP + private keys) and `firmware/*.bin` (4.5 MB pre-built binaries baked with a personal LAN IP). Both patterns are now gitignored — `.bin` regenerates on `idf.py build`.

### Refactor and format

36. `components/bmt_ota/bmt_ota.c` — 180-line `ota_wifi_task` split into `ota_wifi_bring_up` / `ota_check_and_flash` / `ota_wifi_tear_down` helpers. Behavior unchanged.
37. Full repo now passes `clang-format --dry-run --Werror` against the root `.clang-format` (Allman, tabs, `ColumnLimit: 0`), including the Zephyr Beacon apps.

## 2026-07-18

Security hardening on top of the 2026-07-09 rewrite. Scope: `apps/gateway`, `apps/relay`, `apps/scanner`, `apps/tag`, shared `components/bmt_ota`, and `thingsboard/`. Does not touch `apps/beacon` (separate board, still experimental).

### Security

16. Secure Boot V2 (RSA-3072) on all four apps. Bootloader and app image are signed at build time (`CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=y`); an unsigned or tampered image will not boot. One key file signs the whole fleet (`secure_boot_keys/bmt_fleet_rsa3072.pem`, gitignored, never committed) — see [13-secure-boot.md](13-secure-boot.md).
17. Flash Encryption (AES-128, Development mode) on the same four apps. Added an `nvs_key` partition (`encrypted` flag) to every `partitions.csv` so NVS content — mesh keys, HMAC key, WiFi/ThingsBoard credentials — is encrypted at rest, not just in transit.
18. OTA fileserver moved from plain HTTP (`python -m http.server 8080`) to HTTPS on port 8443: an `nginx` container (`ota-fileserver` in `thingsboard/docker-compose.yml`) now serves `firmware/`, reusing the same TLS cert/key pair as MQTTS. `bmt_ota.c` (gateway self-update, and shared `components/bmt_ota` used by relay/scanner) sets `cert_pem`/`common_name` on `esp_http_client_config_t` to verify the server before downloading a `.bin`. Rationale and old HTTP tradeoff: [06-http-tls.md](06-http-tls.md).

### Reliability

19. New `bmt_factory_reset` component on gateway, relay, and scanner: holding the BOOT button (GPIO0) for 10 seconds erases NVS (node table, NetKey/AppKey, all other config) and reboots. Does not touch firmware.
20. Gateway publishes OTA result (`bmt_tb_pub_gateway_ota_result`) to ThingsBoard before rebooting on self-update success, and on failure. `ota_result` now reflects gateway self-update, not just node OTA.
21. Device identifier for mesh nodes changed from mesh address (`bmt_node_0x%04x`, changes on every re-provision) to MAC-based (`bmt_node_%02x%02x%02x%02x%02x%02x`, fixed per physical device). Fixes ThingsBoard accumulating a new device entry every time a node resets or re-provisions.

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
